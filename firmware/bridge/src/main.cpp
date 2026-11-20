// deskstrate-bridge: an ESP32-S3 Super Mini on the Mac's dock. The Mac is on a work VPN
// that keeps it from sending to the LAN, so its daemon talks kvm_proto over this board's
// USB serial port instead, and the board passes it to and from UDP 18200 on Wi-Fi
// (serial.h, udp.h). A plain passthrough: it never reads kvm_proto. The port being there
// is how the Mac's daemon knows it is docked.
// Updates itself from GitHub releases like the other boards (ota_pull).
#include "esp_app_desc.h"
#include "esp_log.h"
#include "ota_pull.h"
#include "sdkconfig.h"
#include "serial.h"
#include "status_led.h"
#include "udp.h"
#include "wifi_sta.h"

static const char *TAG = "bridge";
static const char *kHostname = "deskstrate-bridge";

static void fault(const char *what, esp_err_t err) {
    ESP_LOGE(TAG, "%s not started: %s", what, esp_err_to_name(err));
    status_led::set_fault(status_led::kFaultStartup, true);
}

extern "C" void app_main(void) {
    // First, so every log line from here on reaches the Mac as a "# " comment.
    const esp_err_t serial_err = serial::start();
    status_led::start(static_cast<gpio_num_t>(CONFIG_DESKSTRATE_BRIDGE_LED_GPIO));
    if (serial_err != ESP_OK) fault("serial port", serial_err);
    ESP_LOGI(TAG, "Deskstrate bridge %s", esp_app_get_description()->version);

    esp_err_t err = wifi_sta_start(kHostname);
    if (err != ESP_OK) fault("Wi-Fi", err);
    if (err == ESP_OK) {
        // On USB power: hear the core's broadcasts promptly (wifi_sta.h).
        wifi_sta_no_power_save();
        err = udp::start();
        if (err != ESP_OK) fault("UDP", err);
    }
    if (err == ESP_OK && serial_err == ESP_OK) {
        err = serial::start_reader(udp::send_line);
        if (err != ESP_OK) fault("serial reader", err);
    }
    if (err == ESP_OK) {
        err = ota_pull::start(ota_pull::kBridgePort);
        if (err != ESP_OK) fault("updater", err);
    }
    // A new OTA image that can't get back on the network, or can't update again, rolls back.
    ota_pull::confirm_boot(err == ESP_OK);
    status_led::set_ready();
}
