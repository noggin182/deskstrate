// Deskstrate core: headless source of truth.
// Joins Wi-Fi as deskstrate-core, polls both monitors over DDC/CI and both hubs over SWD
// (or simulates all four with CONFIG_DESKSTRATE_SIMULATE), serves a bench web UI and the
// kvm_proto UDP server for the deck.
// Next: presets/switching logic, kvm_proto authentication, OTA.
#include <cstdio>

#include "board_config.h"
#include "esp_log.h"
#include "hubs.h"
#include "kvm_proto.h"
#include "kvm_server.h"
#include "monitors.h"
#include "web.h"
#include "wifi_sta.h"

#if CONFIG_DESKSTRATE_SWD_BRINGUP
#include "hub_swd.h"
#endif

static const char *TAG = "core";
static const char *kHostname = "deskstrate-core";

#if CONFIG_DESKSTRATE_SWD_BRINGUP
// Read-only: connect to one hub, log its IDCODE and selection state. Never writes.
static void swd_bringup_one(const char *role, hub_swd::Pins pins) {
    hub_swd::Hub hub;
    hub.init(pins);
    uint32_t idcode = 0;
    esp_err_t err = hub.connect(&idcode);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SWD %s: connect failed at %s (%s)", role, hub.last_stage(),
                 esp_err_to_name(err));
        return;
    }
    hub_swd::Status s = {};
    err = hub.read_status(s);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SWD %s: idcode %08x, status read failed (%s)", role, (unsigned)idcode,
                 esp_err_to_name(err));
        return;
    }
    // current is the live port (0..3); panel label is current + 1.
    ESP_LOGI(TAG, "SWD %s: idcode %08x, current=%u applied=%u requested=%u held=%u", role,
             (unsigned)idcode, s.current, s.applied, s.requested, s.held_lockout);
}
#endif

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Deskstrate core on %s (kvm_proto v%u, port %u)", board::kName,
             (unsigned)KVM_VERSION, (unsigned)KVM_PORT);

#if CONFIG_DESKSTRATE_SWD_BRINGUP
    swd_bringup_one("HID", {board::kHidSwclk, board::kHidSwdio});
    swd_bringup_one("MEDIA", {board::kMediaSwclk, board::kMediaSwdio});
#endif

    esp_err_t err = wifi_sta_start(kHostname);
    if (err != ESP_OK) ESP_LOGE(TAG, "Wi-Fi not started: %s", esp_err_to_name(err));

    monitors::start();
    hubs::start();
    if (err == ESP_OK) kvm_server::start();

    if (err == ESP_OK) {
        err = web_start(kHostname);
        if (err != ESP_OK) ESP_LOGE(TAG, "web UI not started: %s", esp_err_to_name(err));
    }
}
