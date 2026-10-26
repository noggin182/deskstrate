// Wi-Fi station join shared by core and deck: NVS init, netif with a DHCP hostname,
// and automatic reconnect. Credentials come from menuconfig -> "Deskstrate Wi-Fi".
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

// Starts Wi-Fi and returns without waiting for a connection.
// hostname: DHCP hostname, e.g. "deskstrate".
esp_err_t wifi_sta_start(const char *hostname);

// True while associated with an IP address.
bool wifi_sta_connected();

// Blocks until connected or timeout. Returns true if connected.
bool wifi_sta_wait_connected(TickType_t timeout);
