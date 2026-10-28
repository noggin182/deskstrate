#include "ota_pull.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "status_led.h"

namespace ota_pull {

namespace {

const char *TAG = "ota_pull";

constexpr int kMaxRedirects = 5;
constexpr int kTimeoutMs = 30000;

struct Last {
    char result[64] = "not checked yet";
    char release[41] = "";
    TickType_t at = 0;
    bool checked = false;
};
Last last;
SemaphoreHandle_t last_lock;

bool reached(TickType_t deadline) { return (int32_t)(xTaskGetTickCount() - deadline) >= 0; }

bool has_ip() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    return netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

// ok false = a failure the status LED shows red until a check succeeds.
portMUX_TYPE progress_lock = portMUX_INITIALIZER_UNLOCKED;
Progress current = {};

void set_progress(Phase phase, uint32_t bytes, uint32_t total, const char *version) {
    taskENTER_CRITICAL(&progress_lock);
    current.phase = phase;
    current.bytes = bytes;
    current.total = total;
    if (version != nullptr) snprintf(current.version, sizeof current.version, "%s", version);
    taskEXIT_CRITICAL(&progress_lock);
}

void set_result(bool ok, const char *release, const char *fmt, ...) {
    status_led::set_fault(status_led::kFaultUpdate, !ok);
    xSemaphoreTake(last_lock, portMAX_DELAY);
    va_list args;
    va_start(args, fmt);
    vsnprintf(last.result, sizeof last.result, fmt, args);
    va_end(args);
    if (release) snprintf(last.release, sizeof last.release, "%s", release);
    last.at = xTaskGetTickCount();
    last.checked = true;
    ESP_LOGI(TAG, "%s", last.result);
    xSemaphoreGive(last_lock);
}

esp_http_client_config_t http_config(const char *url) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = kTimeoutMs;
    cfg.buffer_size = 4096;     // GitHub's redirect headers are long
    cfg.buffer_size_tx = 2048;  // and so are the signed asset URLs they point at
    return cfg;
}

// GETs a small text file into buf, following GitHub's redirects to its asset host.
// ESP_ERR_NOT_FOUND on a 404 (no release published yet).
esp_err_t get_text(const char *url, char *buf, size_t len) {
    esp_http_client_config_t cfg = http_config(url);
    cfg.disable_auto_redirect = true;  // handled below, as esp_https_ota does
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_ERR_NO_MEM;

    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    for (int hop = 0; hop < kMaxRedirects; hop++) {
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) break;
        if (esp_http_client_fetch_headers(client) < 0) {
            err = ESP_FAIL;
            break;
        }
        const int status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            err = esp_http_client_set_redirection(client);
            if (err != ESP_OK) break;
            char drain[128];
            while (esp_http_client_read(client, drain, sizeof drain) > 0) {
            }
            err = ESP_ERR_INVALID_RESPONSE;  // too many redirects if the loop runs out
            continue;
        }
        if (status != 200) {
            err = status == 404 ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_RESPONSE;
            break;
        }
        size_t total = 0;
        int n;
        while (total < len - 1 && (n = esp_http_client_read(client, buf + total, len - 1 - total)) > 0) {
            total += n;
        }
        buf[total] = 0;
        err = ESP_OK;
        break;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

// Copies the value of "key=value" from the manifest into out. False if absent.
bool manifest_value(const char *manifest, const char *key, char *out, size_t len) {
    const size_t key_len = strlen(key);
    for (const char *line = manifest; *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : "") {
        if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') continue;
        const char *value = line + key_len + 1;
        size_t n = strcspn(value, "\r\n");
        if (n >= len) return false;
        memcpy(out, value, n);
        out[n] = 0;
        return n > 0;
    }
    return false;
}

// Tags and versions go into a URL or get compared: only allow what git produces.
bool safe_token(const char *s) {
    if (!*s) return false;
    for (; *s; s++) {
        const char c = *s;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
              c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

bool ends_with(const char *s, const char *suffix) {
    const size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

// Version of the image that last failed to confirm, so it isn't installed again.
bool rolled_back_version(char *out, size_t len) {
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    esp_app_desc_t desc;
    if (!bad || esp_ota_get_partition_description(bad, &desc) != ESP_OK) return false;
    snprintf(out, len, "%s", desc.version);
    return true;
}

void install(const char *tag, const char *version) {
    const esp_app_desc_t *app = esp_app_get_description();
    char url[160];
    snprintf(url, sizeof url, "%s/download/%s/%s.bin", kReleases, tag, app->project_name);
    ESP_LOGI(TAG, "downloading %s", url);

    esp_http_client_config_t http = http_config(url);
    http.keep_alive_enable = true;
    esp_https_ota_config_t cfg = {};
    cfg.http_config = &http;
    esp_https_ota_handle_t ota = nullptr;
    status_led::set_activity(status_led::kUpdating, true);  // until install() returns or reboots
    set_progress(Phase::Downloading, 0, 0, version);
    esp_err_t err = esp_https_ota_begin(&cfg, &ota);
    if (err != ESP_OK) {
        set_progress(Phase::Idle, 0, 0, nullptr);
        set_result(false, tag, "download of %s failed: %s", version, esp_err_to_name(err));
        return;
    }

    // Never install another board's image, or one that isn't what the manifest promised.
    esp_app_desc_t incoming;
    err = esp_https_ota_get_img_desc(ota, &incoming);
    if (err == ESP_OK && strncmp(incoming.project_name, app->project_name, sizeof incoming.project_name) != 0) {
        esp_https_ota_abort(ota);
        set_progress(Phase::Idle, 0, 0, nullptr);
        set_result(false, tag, "refused: image is %.32s, not %.32s", incoming.project_name, app->project_name);
        return;
    }
    if (err == ESP_OK && strncmp(incoming.version, version, sizeof incoming.version) != 0) {
        esp_https_ota_abort(ota);
        set_progress(Phase::Idle, 0, 0, nullptr);
        set_result(false, tag, "refused: image version %.32s != manifest", incoming.version);
        return;
    }

    if (err == ESP_OK) {
        const int total = esp_https_ota_get_image_size(ota);
        do {
            err = esp_https_ota_perform(ota);  // one chunk per call
            const int read = esp_https_ota_get_image_len_read(ota);
            set_progress(Phase::Downloading, read > 0 ? read : 0, total > 0 ? total : 0, nullptr);
        } while (err == ESP_ERR_HTTPS_OTA_IN_PROGRESS);
    }
    if (err == ESP_OK && !esp_https_ota_is_complete_data_received(ota)) err = ESP_ERR_INVALID_SIZE;
    if (err != ESP_OK) {
        esp_https_ota_abort(ota);
        set_progress(Phase::Idle, 0, 0, nullptr);
        set_result(false, tag, "download of %s failed: %s", version, esp_err_to_name(err));
        return;
    }
    err = esp_https_ota_finish(ota);  // validates the image and sets it as the boot slot
    if (err != ESP_OK) {
        set_progress(Phase::Idle, 0, 0, nullptr);
        set_result(false, tag, "install of %s failed: %s", version, esp_err_to_name(err));
        return;
    }
    set_result(true, tag, "installed %s, rebooting", version);
    status_led::set_activity(status_led::kUpdating, false);
    status_led::set_activity(status_led::kRestarting, true);
    set_progress(Phase::Restarting, 0, 0, nullptr);
    vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));
    esp_restart();
}

void check(bool doorbell) {
    if (!has_ip()) {
        set_result(true, nullptr, "skipped: no network");
        return;
    }
    char url[96];
    snprintf(url, sizeof url, "%s/latest/download/manifest.txt", kReleases);
    char manifest[512];
    const esp_err_t err = get_text(url, manifest, sizeof manifest);
    if (err != ESP_OK) {
        const bool none = err == ESP_ERR_NOT_FOUND;  // nothing published yet isn't a fault
        set_result(none, nullptr, "manifest: %s", none ? "no release yet" : esp_err_to_name(err));
        return;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    char tag[41], version[33], bad[33];
    if (!manifest_value(manifest, "tag", tag, sizeof tag) || !safe_token(tag)) {
        set_result(false, nullptr, "manifest: no valid tag");
        return;
    }
    if (!manifest_value(manifest, app->project_name, version, sizeof version) || !safe_token(version)) {
        set_result(false, tag, "no %.32s image in %s", app->project_name, tag);
        return;
    }
    if (strcmp(version, app->version) == 0) {
        set_result(true, tag, "up to date (%s)", version);
    } else if (rolled_back_version(bad, sizeof bad) && strcmp(bad, version) == 0) {
        set_result(true, tag, "skipped %s: it rolled back", version);
    } else if (!doorbell && ends_with(app->version, "-dirty")) {
        set_result(true, tag, "kept local build; %s waits for a doorbell", version);
    } else {
        install(tag, version);  // returns only if it failed
        status_led::set_activity(status_led::kUpdating, false);
    }
}

void task(void *arg) {
    const uint16_t port = (uint16_t)(uintptr_t)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (sock < 0 || bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        ESP_LOGE(TAG, "doorbell UDP %u unavailable (errno %d): polling only", port, errno);
        if (sock >= 0) close(sock);
        sock = -1;
    } else {
        timeval tv = {.tv_sec = 1, .tv_usec = 0};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        ESP_LOGI(TAG, "doorbell on UDP %u; polling %s hourly", port, kReleases);
    }

    TickType_t next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(kFirstPollMs);
    TickType_t next_allowed = xTaskGetTickCount();
    bool rung = false;
    for (;;) {
        if (reached(next_allowed) && (rung || reached(next_poll))) {
            check(rung);  // returns only if nothing was installed
            status_led::set_activity(status_led::kDoorbell, false);
            rung = false;
            next_allowed = xTaskGetTickCount() + pdMS_TO_TICKS(kMinGapMs);
            next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(kPollMs);
        }
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        uint8_t buf[16];
        const int n = recv(sock, buf, sizeof buf, 0);
        if (n >= (int)sizeof kDoorbellMagic && memcmp(buf, kDoorbellMagic, sizeof kDoorbellMagic) == 0 && !rung) {
            rung = true;
            status_led::set_activity(status_led::kDoorbell, true);
            ESP_LOGI(TAG, "doorbell: checking %s", reached(next_allowed) ? "now" : "when the rate limit allows");
        }
    }
}

void confirm_task(void *arg) {
    const bool healthy = arg != nullptr;
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(kConfirmTimeoutMs);
    while (healthy && !has_ip() && !reached(deadline)) vTaskDelay(pdMS_TO_TICKS(500));
    if (healthy && has_ip()) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new image confirmed");
    } else {
        ESP_LOGE(TAG, "new image unhealthy (services %s, network %s): rolling back",
                 healthy ? "up" : "down", has_ip() ? "up" : "down");
        status_led::set_activity(status_led::kRestarting, true);
        set_progress(Phase::Restarting, 0, 0, "");
        vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));
        esp_ota_mark_app_invalid_rollback_and_reboot();  // returns only if there's nothing to roll back to
        set_progress(Phase::Idle, 0, 0, nullptr);
        status_led::set_activity(status_led::kRestarting, false);
        ESP_LOGE(TAG, "rollback failed: keeping this image");
    }
    vTaskDelete(nullptr);
}

}  // namespace

Progress progress() {
    taskENTER_CRITICAL(&progress_lock);
    const Progress p = current;
    taskEXIT_CRITICAL(&progress_lock);
    return p;
}

esp_err_t start(uint16_t doorbell_port) {
    if (!last_lock) last_lock = xSemaphoreCreateMutex();
    if (!last_lock) return ESP_ERR_NO_MEM;
    if (xTaskCreate(task, "ota_pull", 8192, (void *)(uintptr_t)doorbell_port, 3, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void confirm_boot(bool healthy) {
    esp_ota_img_states_t s;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &s) != ESP_OK ||
        s != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    ESP_LOGW(TAG, "new image pending verify: confirming once on the network (%lu s max)",
             (unsigned long)(kConfirmTimeoutMs / 1000));
    xTaskCreate(confirm_task, "ota_confirm", 3072, healthy ? (void *)1 : nullptr, 5, nullptr);
}

int status_json(char *buf, size_t len) {
    if (!last_lock) return snprintf(buf, len, "{\"result\":\"not started\",\"release\":\"\",\"ago_s\":-1}");
    xSemaphoreTake(last_lock, portMAX_DELAY);
    const long ago = last.checked ? (long)((xTaskGetTickCount() - last.at) * portTICK_PERIOD_MS / 1000) : -1;
    const int n = snprintf(buf, len, "{\"result\":\"%s\",\"release\":\"%s\",\"ago_s\":%ld}", last.result,
                           last.release, ago);
    xSemaphoreGive(last_lock);
    return n;
}

}  // namespace ota_pull
