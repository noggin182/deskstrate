// Both USB hubs over SWD: owns one hub_swd::Hub each, polls the selected port in the
// background and caches it so readers (web UI, UDP) never touch SWD.
// Index = enum kvm_usb_hub (0 = HID, 1 = MEDIA); ports are enum kvm_usb_port (0..3).
#pragma once

#include <cstdint>
#include "esp_err.h"

namespace hubs {

inline constexpr int kCount = 2;
inline constexpr uint8_t kUnknown = 0xFF;  // == KVM_USB_UNKNOWN

struct State {
    bool online = false;       // SWD connected and the status word read back
    uint8_t port = kUnknown;   // selected port 0..3 (panel label = port + 1)
    uint32_t raw = 0;          // status word as read (online only), for diagnostics
    const char *error = "not polled yet";  // why it's offline (static string), or nullptr
    const char *stage = "";    // SWD stage that failed (static string), offline only
    uint8_t ack = 0;           // SWD ACK of the failing transfer, offline only
};

// Starts the poll task. A hub that isn't wired or powered just stays offline and is
// retried every 15 s.
void start();

// Last polled (or just requested) state. Cheap, never blocks on SWD.
State get(int hub);

// Asks the hub to switch (ESP_OK at once if it's already on that port).
// ESP_ERR_INVALID_ARG for a bad hub or port, ESP_ERR_NOT_FOUND if the hub isn't online,
// ESP_ERR_NOT_FINISHED if it is still carrying out a previous switch.
esp_err_t set_port(int hub, uint8_t port);

}  // namespace hubs
