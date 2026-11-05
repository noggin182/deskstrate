// esp-bench: signs of life for the bench ESP32-S3 Mini.
// Rainbow on the WS2812 (GPIO21), Wi-Fi as esp-bench, hello world at http://esp-bench.local/.
// Wi-Fi credentials: menuconfig -> Deskstrate Wi-Fi, or whatever is already in NVS.
#include <cstdio>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "mdns.h"
#include "wifi_sta.h"

namespace {

const char *TAG = "bench";
const char *kHostname = "esp-bench";
constexpr gpio_num_t kLedGpio = GPIO_NUM_21;
constexpr uint8_t kBrightness = 40;  // of 255: plenty for a bare LED on the bench

// Hue 0-1535 round the colour wheel, scaled to kBrightness.
void wheel(uint32_t hue, uint32_t &r, uint32_t &g, uint32_t &b) {
    const uint32_t x = hue % 256;
    switch (hue / 256) {
        case 0: r = 255; g = x; b = 0; break;
        case 1: r = 255 - x; g = 255; b = 0; break;
        case 2: r = 0; g = 255; b = x; break;
        case 3: r = 0; g = 255 - x; b = 255; break;
        case 4: r = x; g = 0; b = 255; break;
        default: r = 255; g = 0; b = 255 - x; break;
    }
    r = r * kBrightness / 255;
    g = g * kBrightness / 255;
    b = b * kBrightness / 255;
}

void rainbow_task(void *arg) {
    auto strip = static_cast<led_strip_handle_t>(arg);
    uint32_t hue = 0;
    for (;;) {
        uint32_t r, g, b;
        wheel(hue, r, g, b);
        led_strip_set_pixel(strip, 0, r, g, b);
        led_strip_refresh(strip);
        hue = (hue + 4) % 1536;  // ~4 s per lap at 10 ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void start_rainbow() {
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = kLedGpio;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    led_strip_handle_t strip = nullptr;
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no LED on GPIO %d: %s", (int)kLedGpio, esp_err_to_name(err));
        return;
    }
    xTaskCreate(rainbow_task, "rainbow", 2048, strip, 2, nullptr);
}

esp_err_t page_get(httpd_req_t *req) {
    wifi_ap_record_t ap = {};
    const int rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
    char page[768];
    const int n = snprintf(page, sizeof page,
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>esp-bench</title>"
        "<style>body{font:16px system-ui,sans-serif;margin:16px;background:#111;color:#ddd}"
        "h1{background:linear-gradient(90deg,#f44,#fb3,#4d4,#4cf,#a6f);-webkit-background-clip:text;"
        "background-clip:text;color:transparent}td{padding:2px 12px 2px 0}td:first-child{color:#888}</style>"
        "</head><body><h1>Hello, world!</h1><p>from esp-bench</p><table>"
        "<tr><td>build</td><td>%s</td></tr><tr><td>uptime</td><td>%lld s</td></tr>"
        "<tr><td>Wi-Fi</td><td>%d dBm</td></tr></table></body></html>",
        esp_app_get_description()->version, (long long)(esp_timer_get_time() / 1000000), rssi);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, n);
}

void start_web() {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "web page not started: %s", esp_err_to_name(err));
        return;
    }
    const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = page_get, .user_ctx = nullptr};
    httpd_register_uri_handler(server, &root);

    err = mdns_init();
    if (err == ESP_OK) err = mdns_hostname_set(kHostname);
    if (err == ESP_OK) err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    if (err != ESP_OK) ESP_LOGW(TAG, "mDNS: %s", esp_err_to_name(err));
    ESP_LOGI(TAG, "listening on http://%s.local/", kHostname);
}

}  // namespace

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "esp-bench %s", esp_app_get_description()->version);
    start_rainbow();
    esp_err_t err = wifi_sta_start(kHostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi not started: %s", esp_err_to_name(err));
        return;  // the rainbow keeps going
    }
    start_web();
}
