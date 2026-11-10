#pragma once

#include <cstdint>

#include "esp_err.h"
#include "kvm_proto.h"

esp_err_t network_start(void);

// kvm_proto client: the core is the server and the source of truth.

// True if a STATE from the core arrived since the last call; copies its 4 bytes into out.
bool network_take_kvm_state(uint8_t out[KVM_SLOTS]);

// Asks the core for a state (REQUEST bytes, '-' = leave alone): to the sender of the last
// STATE, or broadcast until one has been seen. Resent until a STATE shows it (up to 3 s);
// a new request adds to one still in flight. All '-' just asks for a STATE.
// ESP_ERR_INVALID_STATE before Wi-Fi is up.
esp_err_t network_send_kvm_request(const uint8_t want[KVM_SLOTS]);
