// Status LED shared by core, deck and kb: one WS2812 whose colour says what the board is
// doing. The first of these that applies wins:
//   green                restarting into a new image
//   purple, flashing     writing an update to the spare slot (pulled or pushed)
//   purple               a doorbell rang: checking GitHub for an update
//   red                  a fault: something failed to start, the last update check
//                        failed, or no Wi-Fi (kJoinTimeoutMs after ready, or lost later)
//   green                booting: not ready yet, or ready and still joining Wi-Fi
//   orange, bright       deck only: the core has gone quiet (set_core_silent)
//   blue, dim            all good
// A single-colour LED (Kind::kSingleActiveLow, e.g. the XIAO ESP32S3's user LED) shows
// the same states as patterns:
//   flashing at 5 Hz     updating
//   blinking at 1 Hz     restarting, doorbell, or booting / joining Wi-Fi (the greens)
//   on                   a fault, no Wi-Fi, or the core gone quiet (the reds, orange)
//   a blip every 2 s     all good
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

enum class Kind {
    kWs2812,           // one WS2812 (RGB)
    kSingleActiveLow,  // one plain LED, lit while the pin is low
};

// Drives the LED on gpio, green (or blinking) until set_ready(). GPIO_NUM_NC: no LED.
esp_err_t start(gpio_num_t gpio, Kind kind = Kind::kWs2812);

// The pin level that leaves this kind of LED dark, e.g. to hold through deep sleep.
inline int off_level(Kind kind) { return kind == Kind::kSingleActiveLow ? 1 : 0; }

// Start-up is done (whether or not it all worked): Wi-Fi is watched from here on.
void set_ready();

void set_fault(uint32_t faults, bool on);
void set_activity(uint32_t activity, bool on);
void set_core_silent(bool silent);

// Turns the LED off and leaves it off until reboot, e.g. before deep sleep: a WS2812
// keeps showing its last colour for as long as it has power. True once it is dark (or
// there is no LED); waits at most timeout_ms.
bool turn_off(uint32_t timeout_ms = 200);

}  // namespace status_led
