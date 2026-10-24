// Deskstrate core: headless source of truth.
// Skeleton: joins Wi-Fi as deskstrate-core, polls both monitors over DDC/CI and serves
// a bench web UI to switch them.
// Next: UDP server (kvm_proto), state/switching logic, SWD hubs, OTA.
#include <cstdio>

#include "board_config.h"
#include "esp_log.h"
#include "kvm_proto.h"
#include "monitors.h"
#include "web.h"
#include "wifi_sta.h"

static const char *TAG = "core";
static const char *kHostname = "deskstrate-core";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Deskstrate core on %s (kvm_proto v%u, port %u)", board::kName,
             (unsigned)KVM_VERSION, (unsigned)KVM_PORT);

    esp_err_t err = wifi_sta_start(kHostname);
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi not started: %s", esp_err_to_name(err));

    monitors::start();

    if (err == ESP_OK) {
        err = web_start(kHostname);
        if (err != ESP_OK) ESP_LOGE(TAG, "web UI not started: %s", esp_err_to_name(err));
    }
}
