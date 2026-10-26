#include "ota.h"

#include <cstdio>

#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_sta.h"

namespace ota {

namespace {

const char *TAG = "ota";

constexpr size_t kChunk = 4096;
constexpr uint32_t kRebootDelayMs = 500;  // let the HTTP reply go out first
constexpr int kRecvRetries = 5;           // receive timeouts tolerated mid-upload

const char *state_name(esp_ota_img_states_t s) {
    switch (s) {
        case ESP_OTA_IMG_NEW: return "new";
        case ESP_OTA_IMG_PENDING_VERIFY: return "pending-verify";
        case ESP_OTA_IMG_VALID: return "valid";
        case ESP_OTA_IMG_INVALID: return "invalid";
        case ESP_OTA_IMG_ABORTED: return "aborted";
        default: return "undefined";  // flashed over USB: never went through OTA
    }
}

const char *running_state() {
    esp_ota_img_states_t s;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &s) == ESP_OK
               ? state_name(s)
               : "undefined";
}

void reboot_task(void *) {
    vTaskDelay(pdMS_TO_TICKS(kRebootDelayMs));
    esp_restart();
}

esp_err_t reply(httpd_req_t *req, const char *status, const char *json) {
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

esp_err_t fail(httpd_req_t *req, const char *status, const char *error) {
    char buf[96];
    snprintf(buf, sizeof buf, "{\"error\":\"%s\"}", error);
    ESP_LOGW(TAG, "upload failed: %s", error);
    return reply(req, status, buf);
}

esp_err_t status_get(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
    char buf[256];
    snprintf(buf, sizeof buf,
             "{\"running\":\"%s\",\"version\":\"%s\",\"built\":\"%s %s\",\"state\":\"%s\","
             "\"next\":\"%s\"}",
             esp_ota_get_running_partition()->label, app->version, app->date, app->time,
             running_state(), next ? next->label : "");
    return reply(req, "200 OK", buf);
}

// Streams the body into the spare slot. esp_ota_end() checks the image (magic, chip,
// SHA-256) before it can become the boot partition.
esp_err_t upload_post(httpd_req_t *req) {
    const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
    if (!slot) return fail(req, "500 Internal Server Error", "no OTA slot");
    if (req->content_len == 0) return fail(req, "400 Bad Request", "empty body");
    if (req->content_len > slot->size) return fail(req, "413 Payload Too Large", "image too big");

    ESP_LOGI(TAG, "receiving %u bytes into %s", (unsigned)req->content_len, slot->label);
    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK) return fail(req, "500 Internal Server Error", esp_err_to_name(err));

    static char buf[kChunk];  // httpd runs one handler at a time
    size_t left = req->content_len;
    int timeouts = 0;
    while (left > 0) {
        const int n = httpd_req_recv(req, buf, left < kChunk ? left : kChunk);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= kRecvRetries) continue;
        if (n <= 0) {
            esp_ota_abort(ota);
            return fail(req, "408 Request Timeout", "upload interrupted");
        }
        if (left == req->content_len && (uint8_t)buf[0] != ESP_IMAGE_HEADER_MAGIC) {
            esp_ota_abort(ota);
            return fail(req, "400 Bad Request", "not an app image (send firmware.bin)");
        }
        err = esp_ota_write(ota, buf, n);
        if (err != ESP_OK) {
            esp_ota_abort(ota);
            return fail(req, "500 Internal Server Error", esp_err_to_name(err));
        }
        left -= n;
    }

    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        return fail(req, "400 Bad Request",
                    err == ESP_ERR_OTA_VALIDATE_FAILED ? "image failed validation" : esp_err_to_name(err));
    }
    err = esp_ota_set_boot_partition(slot);
    if (err != ESP_OK) return fail(req, "500 Internal Server Error", esp_err_to_name(err));

    ESP_LOGI(TAG, "wrote %s; rebooting into it", slot->label);
    reply(req, "200 OK", "{\"ok\":true,\"rebooting\":true}");
    xTaskCreate(reboot_task, "ota_reboot", 2048, nullptr, 5, nullptr);
    return ESP_OK;
}

void confirm_task(void *arg) {
    const bool web_ok = arg != nullptr;
    if (web_ok && wifi_sta_wait_connected(pdMS_TO_TICKS(kConfirmTimeoutMs))) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new image confirmed");
    } else {
        ESP_LOGE(TAG, "new image not reachable (web %s, Wi-Fi %s): rolling back",
                 web_ok ? "up" : "down", wifi_sta_connected() ? "up" : "down");
        esp_ota_mark_app_invalid_rollback_and_reboot();  // returns only if there's nothing to roll back to
        ESP_LOGE(TAG, "rollback failed: keeping this image");
    }
    vTaskDelete(nullptr);
}

}  // namespace

void log_running() {
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "running %s (%s), version %s, built %s %s",
             esp_ota_get_running_partition()->label, running_state(), app->version, app->date,
             app->time);
}

esp_err_t register_handlers(httpd_handle_t server) {
    const httpd_uri_t uris[] = {
        {.uri = "/api/ota", .method = HTTP_GET, .handler = status_get, .user_ctx = nullptr},
        {.uri = "/api/ota", .method = HTTP_POST, .handler = upload_post, .user_ctx = nullptr},
    };
    for (const httpd_uri_t &u : uris) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &u), TAG, "register %s", u.uri);
    }
    return ESP_OK;
}

void confirm_boot(bool web_ok) {
    esp_ota_img_states_t s;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &s) != ESP_OK ||
        s != ESP_OTA_IMG_PENDING_VERIFY) {
        return;
    }
    ESP_LOGW(TAG, "new image pending verify: confirming once Wi-Fi is up (%lu s max)",
             (unsigned long)(kConfirmTimeoutMs / 1000));
    xTaskCreate(confirm_task, "ota_confirm", 3072, web_ok ? (void *)1 : nullptr, 5, nullptr);
}

}  // namespace ota
