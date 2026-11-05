// deskstrate-kb: the ESP32-S3 inside the Keychron Q3 Max.
// Powered from the keyboard's battery. While the keyboard is on USB (at the desk) it
// stays on Wi-Fi as deskstrate-kb, serves a hello page and talks to the keyboard's
// STM32 over a UART (kb_link.h). Off USB it deep-sleeps with the status LED off
// (sleep.h). Updates itself from GitHub releases like the core and deck (ota_pull).
// Next: decode the keyboard's lines (stats, KVM keys) and send kvm_proto requests.
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "kb_link.h"
#include "ota_pull.h"
#include "sdkconfig.h"
#include "sleep.h"
#include "status_led.h"
#include "web.h"
#include "wifi_sta.h"

static const char *TAG = "kb";
static const char *kHostname = "deskstrate-kb";

static void fault(const char *what, esp_err_t err) {
    ESP_LOGE(TAG, "%s not started: %s", what, esp_err_to_name(err));
    status_led::set_fault(status_led::kFaultStartup, true);
}

extern "C" void app_main(void) {
    const auto led = static_cast<gpio_num_t>(CONFIG_DESKSTRATE_KB_LED_GPIO);
    if (led >= 0) gpio_hold_dis(led);  // held dark through deep sleep
    status_led::start(led, kb_sleep::kLedKind);  // green (blinking) until start-up is done
    ESP_LOGI(TAG, "Deskstrate keyboard %s", esp_app_get_description()->version);

    esp_err_t link = kb_link::start();
    if (link != ESP_OK) fault("keyboard link", link);
    if (link == ESP_OK) {
        // Without the link the RX level means nothing, so never sleep on it.
        esp_err_t err = kb_sleep::start();
        if (err != ESP_OK) fault("sleep", err);
    }

    esp_err_t err = wifi_sta_start(kHostname);
    if (err != ESP_OK) fault("Wi-Fi", err);
    if (err == ESP_OK) {
        err = web_start(kHostname);
        if (err != ESP_OK) fault("web page", err);
    }
    if (err == ESP_OK) {
        err = ota_pull::start(ota_pull::kKbPort);
        if (err != ESP_OK) fault("updater", err);
    }
    // A new OTA image that can't get back on the network, or can't update again, rolls back.
    ota_pull::confirm_boot(err == ESP_OK);
    status_led::set_ready();
}
