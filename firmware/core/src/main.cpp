// Deskstrate core: headless source of truth.
// Skeleton: joins Wi-Fi as deskstrate-core and runs the read-only DDC1 bring-up logger.
// Next: UDP server (kvm_proto), state/switching logic, SWD hubs, OTA.
#include <cstdio>

#include "board_config.h"
#include "ddc_bringup.h"
#include "esp_log.h"
#include "kvm_proto.h"
#include "wifi_sta.h"

static const char *TAG = "core";

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Deskstrate core on %s (kvm_proto v%u, port %u)", board::kName,
             (unsigned)KVM_VERSION, (unsigned)KVM_PORT);

    const esp_err_t err = wifi_sta_start("deskstrate-core");
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi not started: %s", esp_err_to_name(err));

    ddc_bringup_start();
}
