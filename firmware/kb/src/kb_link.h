// Link to the keyboard's STM32 (Keychron Q3 Max running firmware/keyboard): framed UART
// with CRC and acks, see components/kb_link_proto/include/kb_link_proto.h.
//   RX (CONFIG_DESKSTRATE_KB_RX_GPIO) <- STM32 PB12, bit-banged TX
//   TX (CONFIG_DESKSTRATE_KB_TX_GPIO) -> STM32 PA3, USART2 RX
// The keyboard holds PB12 high while it is on USB, so the RX level doubles as "keyboard
// is on USB" (see sleep.h). KVM requests from the keyboard go to kvm_client; the core's
// STATE goes back to the keyboard.
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace kb_link {

esp_err_t start();

// RX line high: the keyboard is in cable mode with USB power (or nothing pulls it low).
bool keyboard_on_usb();

// Sends text to the keyboard (EK_TEXT, at most 32 bytes); it echoes it back. False if
// the link isn't started.
bool send_text(const char *text);

// Sends the core's STATE to the keyboard (EK_KVM_STATE).
void send_kvm_state(const uint8_t state[4]);

// The link as JSON: {"usb":true,"hello":"...","status":{...}|null,"status_ago_s":N,
//   "rx_good":N,"rx_bad":N,"acks":N,"kvm_requests":N,"recent":[{"ago_s":N,"text":"..."},...]}
// (recent: oldest first). Returns the length written.
int status_json(char *buf, size_t len);

}  // namespace kb_link
