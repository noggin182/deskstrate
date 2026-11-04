#include "sleep.h"

#include <atomic>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kb_link.h"
#include "ota_pull.h"
#include "sdkconfig.h"
#include "status_led.h"

namespace kb_sleep {

#if CONFIG_DESKSTRATE_KB_SLEEP

namespace {

const char *TAG = "kb_sleep";
constexpr gpio_num_t kRx = static_cast<gpio_num_t>(CONFIG_DESKSTRATE_KB_RX_GPIO);
constexpr gpio_num_t kLed = static_cast<gpio_num_t>(CONFIG_DESKSTRATE_KB_LED_GPIO);
constexpr uint32_t kTickMs = 250;
constexpr uint32_t kHoldoffMs = CONFIG_DESKSTRATE_KB_SLEEP_HOLDOFF_S * 1000;

std::atomic<int> left_s{-1};

bool busy() {
    if (ota_pull::progress().phase != ota_pull::Phase::Idle) return true;
    esp_ota_img_states_t s;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &s) == ESP_OK &&
           s == ESP_OTA_IMG_PENDING_VERIFY;
}

[[noreturn]] void go_to_sleep() {
    ESP_LOGI(TAG, "keyboard off USB for %lu s: deep sleep until it's back",
             (unsigned long)(kHoldoffMs / 1000));
    status_led::turn_off();
    esp_wifi_stop();
    // Keep the LED's data line low while asleep, so noise can't light it.
    if (kLed >= 0) {
        gpio_reset_pin(kLed);
        gpio_set_direction(kLed, GPIO_MODE_OUTPUT);
        gpio_set_level(kLed, 0);
        gpio_hold_en(kLed);
    }
    // Wake when the keyboard raises the line. The pull-down keeps a disconnected line low.
    rtc_gpio_init(kRx);
    rtc_gpio_set_direction(kRx, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_dis(kRx);
    rtc_gpio_pulldown_en(kRx);
    esp_sleep_enable_ext0_wakeup(kRx, 1);
    esp_deep_sleep_start();
}

void task(void *) {
    uint32_t low_ms = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
        if (kb_link::keyboard_on_usb()) {
            low_ms = 0;
            left_s = -1;
            continue;
        }
        low_ms += kTickMs;
        left_s = low_ms >= kHoldoffMs ? 0 : (int)((kHoldoffMs - low_ms + 999) / 1000);
        if (low_ms >= kHoldoffMs && !busy()) go_to_sleep();
    }
}

}  // namespace

esp_err_t start() {
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) ESP_LOGI(TAG, "woke from deep sleep: keyboard is on USB");
    return xTaskCreate(task, "kb_sleep", 3072, nullptr, 3, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

int seconds_left() { return left_s; }

#else

esp_err_t start() { return ESP_OK; }
int seconds_left() { return -1; }

#endif

}  // namespace kb_sleep
