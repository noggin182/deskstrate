#pragma once

#include <cstdint>

#include "esp_err.h"
#include "kvm_proto.h"

esp_err_t network_start(void);
esp_err_t network_broadcast_update(const char *json_message);

// kvm_proto client: the core is the server and the source of truth.
// Unauthenticated for now (tags sent as zeros, not checked).

// True if a STATE from the core arrived since the last call; copies the latest into out.
bool network_take_kvm_state(kvm_state_t &out);

// Sends a REQUEST (enum kvm_action, args a/b) to the core: to the sender of the last
// STATE, or broadcast until one has been seen. ESP_ERR_INVALID_STATE before Wi-Fi is up.
esp_err_t network_send_kvm_request(uint8_t action, uint8_t a, uint8_t b);
