// kvm_proto client (components/kvm_proto), like the deck's: binds KVM_PORT, learns the
// core's address from its STATE broadcasts, and turns the keyboard's KVM keys into
// requests to it (with components/kvm_controls, the deck's rules), resending every
// kResendMs until a STATE shows them (for at most kGiveUpMs). The volume keys become a
// KV broadcast to the other host's daemon instead; Sleep and the key after it ask the core
// to put the desk to sleep and wake it (kvm_proto.h, asleep). Each new STATE also goes to the
// keyboard (kb_link::send_kvm_state).
#pragma once

#include <cstdint>

#include "esp_err.h"

namespace kvm_client {

// Call once Wi-Fi has been started.
esp_err_t start();

// A KVM key pressed on the keyboard (enum kb_link_action). Safe from any task.
void action(uint8_t act);

// The core's latest STATE; false if none has been heard yet.
bool state(uint8_t out[4]);

}  // namespace kvm_client
