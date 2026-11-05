// kvm_proto client (components/kvm_proto), like the deck's: binds KVM_PORT, learns the
// core's address from its STATE broadcasts, and sends the keyboard's KVM requests to it,
// resending every kResendMs until a STATE shows them (for at most kGiveUpMs). Each new
// STATE also goes to the keyboard (kb_link::send_kvm_state).
#pragma once

#include <cstdint>

#include "esp_err.h"

namespace kvm_client {

// Call once Wi-Fi has been started.
esp_err_t start();

// Asks the core for this state (kvm_proto REQUEST bytes; 0xFF = leave alone). Replaces
// any request still pending. Safe from any task.
void request(const uint8_t b[4]);

// The core's latest STATE; false if none has been heard yet.
bool state(uint8_t out[4]);

}  // namespace kvm_client
