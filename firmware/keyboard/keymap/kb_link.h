// Keyboard side of the link to the ESP32 inside the keyboard (firmware/kb). Wire format
// and rules: kb_link_proto.h (components/kb_link_proto, copied here by build.sh).
//   PB12 -> ESP: bit-banged 8N1, interrupts masked for each byte (~175 us at 57600)
//   PA3 <- ESP:  USART2 RX into a circular DMA buffer, read from the main loop
// The link runs only in cable mode with USB power: PB12 idles high then, and is held
// low otherwise, which is what tells the ESP to stay awake or go to sleep.
#pragma once

#include <stdint.h>

// Call every matrix scan (Keychron's housekeeping_task_kb doesn't call the user hook).
void kb_link_task(void);

// Sends a kvm_proto REQUEST [left, right, HID hub, media hub] to the ESP, resent until
// acked. Dropped if the link isn't up (wireless or no USB power).
void kb_link_send_kvm_request(const uint8_t state[4]);
