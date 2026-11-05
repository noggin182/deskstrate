// Deep sleep while the keyboard has no USB power.
//
// The keyboard holds the UART line high while it is in cable mode on USB (kb_link.h). Once the line
// has been low for CONFIG_DESKSTRATE_KB_SLEEP_HOLDOFF_S, this turns the status LED off,
// stops Wi-Fi and deep-sleeps with the RX pin as the wake source (high = wake). Waking
// is a fresh boot. It never sleeps mid-update or while a new image is still pending
// verify (that would roll it back). Off with CONFIG_DESKSTRATE_KB_SLEEP=n.
#pragma once

#include "esp_err.h"
#include "sdkconfig.h"
#include "status_led.h"

namespace kb_sleep {

// The status LED's kind (menuconfig, Deskstrate keyboard): main starts it, and sleep
// holds its pin at the level that keeps it dark.
inline constexpr status_led::Kind kLedKind =
#if CONFIG_DESKSTRATE_KB_LED_WS2812
    status_led::Kind::kWs2812;
#else
    status_led::Kind::kSingleActiveLow;
#endif

esp_err_t start();

// Seconds until it sleeps, or -1 while it isn't counting down.
int seconds_left();

}  // namespace kb_sleep
