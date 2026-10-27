// Status LED shared by core and deck: one WS2812 whose colour says what the board is
// doing. The first of these that applies wins:
//   blue                 restarting into a new image
//   purple, flashing     writing an update to the spare slot (pulled or pushed)
//   purple               a doorbell rang: checking GitHub for an update
//   red                  a fault: something failed to start, the last update check
//                        failed, or no Wi-Fi (kJoinTimeoutMs after ready, or lost later)
//   blue                 booting: not ready yet, or ready and still joining Wi-Fi
//   orange, bright       deck only: the core has gone quiet (set_core_silent)
//   green, dim           all good
// Every setter is safe from any task, and before start() (or with no LED at all).
#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

namespace status_led {

inline constexpr uint32_t kJoinTimeoutMs = 30 * 1000;

enum Fault : uint32_t {
    kFaultStartup = 1u << 0,  // a service failed to start; stays until reboot
    kFaultUpdate = 1u << 1,   // the last update check or install failed
};

// Update activity, one bit each so the updater and the push path don't clear each other's.
enum Activity : uint32_t {
    kDoorbell = 1u << 0,
    kUpdating = 1u << 1,
    kRestarting = 1u << 2,
};

// Drives a single WS2812 on gpio, blue until set_ready(). GPIO_NUM_NC: no LED.
esp_err_t start(gpio_num_t gpio);

// Start-up is done (whether or not it all worked): Wi-Fi is watched from here on.
void set_ready();

void set_fault(uint32_t faults, bool on);
void set_activity(uint32_t activity, bool on);
void set_core_silent(bool silent);

}  // namespace status_led
