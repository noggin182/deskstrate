#include "status_led.h"

#include <atomic>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

namespace status_led {

namespace {

const char *TAG = "status_led";

constexpr uint32_t kTickMs = 50;
constexpr uint32_t kFlashMs = 100;  // on, then off: 5 Hz

struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};
constexpr Rgb kOff = {0, 0, 0};
constexpr Rgb kGreenDim = {0, 12, 0};
constexpr Rgb kBlue = {0, 0, 80};
constexpr Rgb kPurple = {100, 0, 140};
constexpr Rgb kRed = {120, 0, 0};
constexpr Rgb kOrange = {255, 80, 0};

std::atomic<bool> ready{false};
std::atomic<uint32_t> faults{0};
std::atomic<uint32_t> activity{0};
std::atomic<bool> core_silent{false};

bool has_ip() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    return netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

void task(void *arg) {
    auto strip = static_cast<led_strip_handle_t>(arg);
    TickType_t ready_at = 0;
    bool joined = false;
    bool shown = false;
    Rgb last = kOff;
    for (;;) {
        const TickType_t now = xTaskGetTickCount();
        Rgb c = kGreenDim;
        // Netif is only looked at once ready without faults, i.e. after Wi-Fi started.
        const bool up = ready && !faults && has_ip();
        joined = joined || up;
        if (ready && !ready_at) ready_at = now;

        const uint32_t act = activity;
        if (act & kRestarting) {
            c = kBlue;
        } else if (act & kUpdating) {
            c = (now / pdMS_TO_TICKS(kFlashMs)) % 2 ? kOff : kPurple;
        } else if (act & kDoorbell) {
            c = kPurple;
        } else if (faults) {
            c = kRed;
        } else if (!ready) {
            c = kBlue;
        } else if (!up) {
            c = !joined && now - ready_at < pdMS_TO_TICKS(kJoinTimeoutMs) ? kBlue : kRed;
        } else if (core_silent) {
            c = kOrange;
        }
        if (!shown || !(c == last)) {
            if (led_strip_set_pixel(strip, 0, c.r, c.g, c.b) == ESP_OK && led_strip_refresh(strip) == ESP_OK) {
                last = c;
                shown = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}

}  // namespace

esp_err_t start(gpio_num_t gpio) {
    if (gpio == GPIO_NUM_NC) return ESP_OK;
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = gpio;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    led_strip_handle_t strip = nullptr;
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "no LED on GPIO %d: %s", (int)gpio, esp_err_to_name(err));
        return err;
    }
    if (xTaskCreate(task, "status_led", 3072, strip, 2, nullptr) != pdPASS) {
        led_strip_del(strip);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void set_ready() { ready = true; }

namespace {
void set_bits(std::atomic<uint32_t> &bits, uint32_t b, bool on) {
    if (on) {
        bits |= b;
    } else {
        bits &= ~b;
    }
}
}  // namespace

void set_fault(uint32_t f, bool on) { set_bits(faults, f, on); }

void set_activity(uint32_t a, bool on) { set_bits(activity, a, on); }

void set_core_silent(bool silent) { core_silent = silent; }

}  // namespace status_led
