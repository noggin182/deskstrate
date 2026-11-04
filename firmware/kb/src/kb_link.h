// UART link to the keyboard's STM32 (Keychron Q3 Max running firmware/keyboard).
//   RX (CONFIG_DESKSTRATE_KB_RX_GPIO) <- STM32 PB12, bit-banged TX
//   TX (CONFIG_DESKSTRATE_KB_TX_GPIO) -> STM32 PA3, USART2 RX
// 8N1 at CONFIG_DESKSTRATE_KB_BAUD, newline-terminated text lines both ways. The
// keyboard holds PB12 high (UART idle) while it has USB power and low without it, so
// the RX level doubles as "keyboard is on USB" (see sleep.h).
// Nothing is decoded yet: lines from the keyboard are logged and kept for the web page.
#pragma once

#include <cstddef>

#include "esp_err.h"

namespace kb_link {

esp_err_t start();

// RX line high: the keyboard is powered from USB (or nothing pulls it low).
bool keyboard_on_usb();

// Sends text plus a newline to the keyboard. False if the link isn't started.
bool send_line(const char *text);

// The link as JSON: {"usb":true,"rx_lines":N,"rx_bytes":N,"tx_lines":N,
//   "recent":[{"ago_s":N,"text":"..."},...]}
// (oldest first). Returns the length written.
int status_json(char *buf, size_t len);

}  // namespace kb_link
