#include "web_tls.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#include "esp_https_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"
#include "nvs.h"
#include "ota_pull.h"
#include "psa/crypto.h"
#include "sdkconfig.h"

namespace web_tls {

namespace {

const char *TAG = "web_tls";
constexpr char kNvsNamespace[] = "web_tls";
constexpr char kNvsKey[] = "key";  // private key, PEM with its NUL
constexpr char kNvsCert[] = "crt";  // certificate chain, PEM with its NUL
constexpr size_t kKeyMax = 512;    // a P-256 key is ~230 bytes of PEM
constexpr size_t kCertMax = 6144;  // leaf + intermediate is ~2.5 KB of PEM
constexpr size_t kCsrMax = 1024;

struct CertInfo {
    char subject[64];
    char issuer[64];
    char not_after[24];  // 2026-12-31T23:59:59Z
};

const char *board_name = "";
RegisterFn app_register = nullptr;
mbedtls_pk_context key;
char key_pem[kKeyMax];

// Guarded by lock. Only start() and then the fetch task replace cert_pem.
SemaphoreHandle_t lock;
char *cert_pem = nullptr;  // heap, nullptr until one is installed
CertInfo cert_info = {};
char fetch_result[64] = "not fetched yet";
TickType_t fetch_at = 0;
bool fetched = false;

// The HTTPS server. Only start() and then the fetch task touch it, so it needs no lock;
// lock is never held while it stops, as that waits for its handlers (tls_get takes lock).
httpd_handle_t https = nullptr;
std::atomic<bool> https_up{false};

esp_err_t nvs_get(const char *k, char *buf, size_t len) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &nvs);
    if (err != ESP_OK) return err;
    size_t got = len;
    err = nvs_get_blob(nvs, k, buf, &got);
    nvs_close(nvs);
    if (err == ESP_OK && (got == 0 || buf[got - 1] != 0)) err = ESP_ERR_INVALID_SIZE;
    return err;
}

esp_err_t nvs_put(const char *k, const char *pem) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(nvs, k, pem, strlen(pem) + 1);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}

// Makes a new P-256 key into key_pem.
esp_err_t generate_key() {
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t id = 0;
    psa_status_t st = psa_generate_key(&attr, &id);
    if (st != PSA_SUCCESS) {
        ESP_LOGE(TAG, "key generation failed (%d)", (int)st);
        return ESP_FAIL;
    }
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_copy_from_psa(id, &pk);
    if (ret == 0) ret = mbedtls_pk_write_key_pem(&pk, (unsigned char *)key_pem, sizeof key_pem);
    mbedtls_pk_free(&pk);
    psa_destroy_key(id);
    if (ret != 0) {
        ESP_LOGE(TAG, "key export failed (-0x%04x)", (unsigned)-ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Loads the stored key, or makes and stores one on first boot, into key and key_pem.
esp_err_t load_key() {
    esp_err_t err = nvs_get(kNvsKey, key_pem, sizeof key_pem);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no stored key (%s): making one", esp_err_to_name(err));
        err = generate_key();
        if (err == ESP_OK) err = nvs_put(kNvsKey, key_pem);
        if (err != ESP_OK) return err;
    }
    mbedtls_pk_init(&key);
    const int ret = mbedtls_pk_parse_key(&key, (const unsigned char *)key_pem, strlen(key_pem) + 1, nullptr, 0);
    if (ret != 0) {
        ESP_LOGE(TAG, "stored key unreadable (-0x%04x)", (unsigned)-ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

// Writes this board's CSR (CN and SAN *.<domain>) as PEM.
esp_err_t make_csr(char *buf, size_t len) {
    char name[80];
    snprintf(name, sizeof name, "*.%s", CONFIG_DESKSTRATE_TLS_DOMAIN);
    char subject[96];
    snprintf(subject, sizeof subject, "CN=%s", name);

    mbedtls_x509_san_list san = {};
    san.node.type = MBEDTLS_X509_SAN_DNS_NAME;
    san.node.san.unstructured_name.p = (unsigned char *)name;
    san.node.san.unstructured_name.len = strlen(name);
    san.next = nullptr;

    mbedtls_x509write_csr csr;
    mbedtls_x509write_csr_init(&csr);
    mbedtls_x509write_csr_set_md_alg(&csr, MBEDTLS_MD_SHA256);
    mbedtls_x509write_csr_set_key(&csr, &key);
    int ret = mbedtls_x509write_csr_set_subject_name(&csr, subject);
    if (ret == 0) ret = mbedtls_x509write_csr_set_subject_alternative_name(&csr, &san);
    if (ret == 0) ret = mbedtls_x509write_csr_pem(&csr, (unsigned char *)buf, len);
    mbedtls_x509write_csr_free(&csr);
    if (ret != 0) ESP_LOGE(TAG, "CSR failed (-0x%04x)", (unsigned)-ret);
    return ret == 0 ? ESP_OK : ESP_FAIL;
}

// Checks that pem is a certificate chain whose first certificate is for this board's key.
// Fills info from it.
esp_err_t check_cert(const char *pem, CertInfo *info) {
    mbedtls_x509_crt crt;
    mbedtls_x509_crt_init(&crt);
    int ret = mbedtls_x509_crt_parse(&crt, (const unsigned char *)pem, strlen(pem) + 1);
    esp_err_t err = ESP_OK;
    if (ret != 0) {
        ESP_LOGW(TAG, "certificate unreadable (%d)", ret);
        err = ESP_ERR_INVALID_ARG;
    } else if (mbedtls_pk_check_pair(&crt.pk, &key) != 0) {
        err = ESP_ERR_INVALID_STATE;
    } else if (info) {
        mbedtls_x509_dn_gets(info->subject, sizeof info->subject, &crt.subject);
        mbedtls_x509_dn_gets(info->issuer, sizeof info->issuer, &crt.issuer);
        const mbedtls_x509_time &t = crt.valid_to;
        snprintf(info->not_after, sizeof info->not_after, "%04d-%02d-%02dT%02d:%02d:%02dZ", t.year, t.mon,
                 t.day, t.hour, t.min, t.sec);
    }
    mbedtls_x509_crt_free(&crt);
    return err;
}

esp_err_t tls_get(httpd_req_t *req);
esp_err_t csr_get(httpd_req_t *req);

// Every request to the plain server is sent here while HTTPS is up (see uri_match).
constexpr char kRedirectUri[] = "\x01redirect";  // can't collide with a real request URI
constexpr httpd_method_t kRedirectMethods[] = {HTTP_GET, HTTP_HEAD, HTTP_POST, HTTP_PUT, HTTP_DELETE};

esp_err_t redirect(httpd_req_t *req) {
    char location[CONFIG_HTTPD_MAX_URI_LEN + 96];
    snprintf(location, sizeof location, "https://%s.%s%s", board_name, CONFIG_DESKSTRATE_TLS_DOMAIN, req->uri);
    httpd_resp_set_status(req, "307 Temporary Redirect");  // keeps the method and body (POST /api/ota)
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");  // so a board without HTTPS isn't skipped later
    return httpd_resp_send(req, nullptr, 0);
}

esp_err_t register_redirect(httpd_handle_t server) {
    static_assert(sizeof kRedirectMethods / sizeof kRedirectMethods[0] + kUriHandlers == kHttpUriHandlers);
    for (const httpd_method_t m : kRedirectMethods) {
        const httpd_uri_t u = {.uri = kRedirectUri, .method = m, .handler = redirect, .user_ctx = nullptr};
        const esp_err_t err = httpd_register_uri_handler(server, &u);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

esp_err_t register_own(httpd_handle_t server) {
    const httpd_uri_t uris[] = {
        {.uri = "/api/tls", .method = HTTP_GET, .handler = tls_get, .user_ctx = nullptr},
        {.uri = "/api/tls/csr", .method = HTTP_GET, .handler = csr_get, .user_ctx = nullptr},
    };
    static_assert(sizeof uris / sizeof uris[0] == kUriHandlers);
    for (const httpd_uri_t &u : uris) {
        const esp_err_t err = httpd_register_uri_handler(server, &u);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

// (Re)starts the HTTPS server with this certificate chain (copied by the server).
void restart_https(const char *pem, const CertInfo &info) {
    if (https) {
        https_up = false;
        httpd_ssl_stop(https);
        https = nullptr;
    }

    httpd_ssl_config_t cfg = HTTPD_SSL_CONFIG_DEFAULT();
    cfg.httpd.max_uri_handlers = 16;
    cfg.httpd.max_open_sockets = 3;  // ~40 KB of RAM each while open
    cfg.servercert = (const uint8_t *)pem;
    cfg.servercert_len = strlen(pem) + 1;
    cfg.prvtkey_pem = (const uint8_t *)key_pem;
    cfg.prvtkey_len = strlen(key_pem) + 1;
    httpd_handle_t h = nullptr;
    esp_err_t err = httpd_ssl_start(&h, &cfg);
    if (err == ESP_OK) err = register_own(h);
    if (err == ESP_OK && app_register) err = app_register(h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTPS not started: %s", esp_err_to_name(err));
        if (h) httpd_ssl_stop(h);
        return;
    }
    https = h;
    https_up = true;
    ESP_LOGI(TAG, "HTTPS on 443 as %s until %s", info.subject, info.not_after);
}

__attribute__((format(printf, 1, 2))) void set_fetch_result(const char *fmt, ...) {
    xSemaphoreTake(lock, portMAX_DELAY);
    va_list args;
    va_start(args, fmt);
    vsnprintf(fetch_result, sizeof fetch_result, fmt, args);
    va_end(args);
    fetch_at = xTaskGetTickCount();
    fetched = true;
    xSemaphoreGive(lock);
    ESP_LOGI(TAG, "certificate fetch: %s", fetch_result);
}

void fetch_once(char *buf, size_t len) {
    char url[128];
    snprintf(url, sizeof url, "%s/download/tls/%s.pem", ota_pull::kReleases, board_name);
    const esp_err_t err = ota_pull::get_text(url, buf, len);
    if (err == ESP_ERR_NOT_FOUND) return set_fetch_result("not published yet");
    if (err != ESP_OK) return set_fetch_result("failed: %s", esp_err_to_name(err));

    xSemaphoreTake(lock, portMAX_DELAY);
    const bool same = cert_pem && strcmp(cert_pem, buf) == 0;
    xSemaphoreGive(lock);
    if (same) return set_fetch_result("unchanged");

    CertInfo info = {};
    const esp_err_t check = check_cert(buf, &info);
    if (check == ESP_ERR_INVALID_STATE) return set_fetch_result("not for this board's key (update tls/%s.csr)", board_name);
    if (check != ESP_OK) return set_fetch_result("unreadable");

    char *copy = strdup(buf);
    if (!copy) return set_fetch_result("out of memory");
    if (nvs_put(kNvsCert, copy) != ESP_OK) ESP_LOGW(TAG, "certificate not saved; it lasts until reboot");

    xSemaphoreTake(lock, portMAX_DELAY);
    char *old = cert_pem;
    cert_pem = copy;
    cert_info = info;
    xSemaphoreGive(lock);
    free(old);
    restart_https(copy, info);  // copy stays valid: only this task replaces cert_pem
    set_fetch_result("installed, valid until %s", info.not_after);
}

TaskHandle_t fetch_handle = nullptr;

// A doorbell (ota_pull) wakes the fetch task early. Called from ota_pull's task.
void doorbell() {
    if (fetch_handle) xTaskNotifyGive(fetch_handle);
}

void fetch_task(void *) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kFirstFetchMs));  // a doorbell cuts this short
    for (;;) {
        std::unique_ptr<char[]> buf(new (std::nothrow) char[kCertMax]);
        if (buf) {
            fetch_once(buf.get(), kCertMax);
        } else {
            set_fetch_result("out of memory");
        }
        buf.reset();
        // At most one fetch per kMinGapMs, however often the doorbell rings; then wait for
        // the next poll or a doorbell, whichever comes first.
        vTaskDelay(pdMS_TO_TICKS(kMinGapMs));
        ulTaskNotifyTake(pdTRUE, 0);  // drop rings that arrived during the gap
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kFetchMs - kMinGapMs));
    }
}

// Escapes a short string for JSON (quotes and backslashes only: DNs have no control chars).
void json_str(char *out, size_t len, const char *in) {
    size_t o = 0;
    for (; *in && o + 2 < len; in++) {
        if (*in == '"' || *in == '\\') out[o++] = '\\';
        out[o++] = (*in >= 0x20) ? *in : ' ';
    }
    out[o] = 0;
}

esp_err_t tls_get(httpd_req_t *req) {
    char buf[512], subject[96], issuer[96], result[96];
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool have_cert = cert_pem != nullptr;
    const bool running = https_up;
    json_str(subject, sizeof subject, cert_info.subject);
    json_str(issuer, sizeof issuer, cert_info.issuer);
    json_str(result, sizeof result, fetch_result);
    const long ago = fetched ? (long)((xTaskGetTickCount() - fetch_at) * portTICK_PERIOD_MS / 1000) : -1;
    char not_after[sizeof cert_info.not_after];
    memcpy(not_after, cert_info.not_after, sizeof not_after);
    xSemaphoreGive(lock);

    int n = snprintf(buf, sizeof buf, "{\"name\":\"%s\",\"domain\":\"%s\",\"https\":%s,\"cert\":", board_name,
                     CONFIG_DESKSTRATE_TLS_DOMAIN, running ? "true" : "false");
    n += have_cert ? snprintf(buf + n, sizeof buf - n, "{\"subject\":\"%s\",\"issuer\":\"%s\",\"not_after\":\"%s\"}",
                              subject, issuer, not_after)
                   : snprintf(buf + n, sizeof buf - n, "null");
    n += snprintf(buf + n, sizeof buf - n, ",\"fetch\":{\"result\":\"%s\",\"ago_s\":%ld}}", result, ago);
    if (n <= 0 || (size_t)n >= sizeof buf) return httpd_resp_send_500(req);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, buf, n);
}

esp_err_t csr_get(httpd_req_t *req) {
    std::unique_ptr<char[]> buf(new (std::nothrow) char[kCsrMax]);
    if (!buf || make_csr(buf.get(), kCsrMax) != ESP_OK) return httpd_resp_send_500(req);
    char disposition[64];
    snprintf(disposition, sizeof disposition, "attachment; filename=\"%s.csr\"", board_name);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", disposition);
    return httpd_resp_sendstr(req, buf.get());
}

}  // namespace

bool uri_match(const char *tmpl, const char *uri, size_t len) {
    const bool is_redirect = strcmp(tmpl, kRedirectUri) == 0;
    if (https_up) return is_redirect;
    return !is_redirect && strlen(tmpl) == len && strncmp(tmpl, uri, len) == 0;
}

esp_err_t start(const char *name, httpd_handle_t http, RegisterFn register_fn) {
    board_name = name;
    app_register = register_fn;
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;

    esp_err_t err = register_redirect(http);
    if (err != ESP_OK) return err;
    err = load_key();
    if (err != ESP_OK) return err;
    err = register_own(http);
    if (err != ESP_OK) return err;

    std::unique_ptr<char[]> buf(new (std::nothrow) char[kCertMax]);
    if (!buf) return ESP_ERR_NO_MEM;
    CertInfo info = {};
    if (nvs_get(kNvsCert, buf.get(), kCertMax) == ESP_OK && check_cert(buf.get(), &info) == ESP_OK) {
        xSemaphoreTake(lock, portMAX_DELAY);
        cert_pem = strdup(buf.get());
        cert_info = info;
        xSemaphoreGive(lock);
        if (cert_pem) restart_https(cert_pem, info);
    } else {
        ESP_LOGI(TAG, "no certificate yet: HTTPS starts once %s.pem is published", name);
    }
    buf.reset();

    if (xTaskCreate(fetch_task, "web_tls", 8192, nullptr, 3, &fetch_handle) != pdPASS) return ESP_ERR_NO_MEM;
    ota_pull::on_doorbell(doorbell);
    return ESP_OK;
}

}  // namespace web_tls
