// Deep sleep while the keyboard has no USB power.
//
// The keyboard holds the UART line high while it is on USB (kb_link.h). Once the line
// has been low for CONFIG_DESKSTRATE_KB_SLEEP_HOLDOFF_S, this turns the status LED off,
// stops Wi-Fi and deep-sleeps with the RX pin as the wake source (high = wake). Waking
// is a fresh boot. It never sleeps mid-update or while a new image is still pending
// verify (that would roll it back). Off with CONFIG_DESKSTRATE_KB_SLEEP=n.
#pragma once

#include "esp_err.h"

namespace kb_sleep {

esp_err_t start();

// Seconds until it sleeps, or -1 while it isn't counting down.
int seconds_left();

}  // namespace kb_sleep
