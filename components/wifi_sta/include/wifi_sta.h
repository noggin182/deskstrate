// Wi-Fi station join shared by core and deck: NVS init, netif with a DHCP hostname,
// and automatic reconnect.
//
// Credentials: a build with menuconfig -> "Deskstrate Wi-Fi" set uses them, and the Wi-Fi
// driver stores them in NVS. A build without them (CI, OTA images) joins with whatever
// is stored, so images carry no secrets. Provision a board once over USB with a local
// build that has them.
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// After esp_wifi_init() and esp_wifi_set_mode(WIFI_MODE_STA): applies this build's
// menuconfig credentials (stored in NVS by the driver), or checks that stored ones exist.
// ESP_ERR_NOT_FOUND if neither, ESP_ERR_INVALID_ARG if the menuconfig ones are too long.
esp_err_t wifi_sta_apply_credentials();

// Starts Wi-Fi and returns without waiting for a connection.
// hostname: DHCP hostname, e.g. "deskstrate".
esp_err_t wifi_sta_start(const char *hostname);

// True while associated with an IP address.
bool wifi_sta_connected();

// After the station has started: turns modem power save off, for a board on USB power
// that must hear kvm_proto broadcasts promptly. With the default power save the radio
// sleeps between beacons, so the router holds broadcasts until a DTIM beacon and unicast
// until the next beacon: a few hundred ms of lag on every STATE and REQUEST.
esp_err_t wifi_sta_no_power_save();
