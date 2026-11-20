// The LAN side of the bridge: UDP KVM_PORT (18200), kvm_proto's port. The bridge never
// parses kvm_proto, so a protocol change never needs a bridge update.
//   - every datagram heard on the port goes to the Mac as one line (serial.h), except
//     the bridge's own broadcasts coming back and anything containing \r or \n, which
//     would break the framing;
//   - every line from the Mac goes out as one broadcast to 255.255.255.255:18200.
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace udp {

// Call once Wi-Fi has been started.
esp_err_t start();

// Broadcasts one line from the Mac (without its line ending).
void send_line(const uint8_t *line, size_t len);

}  // namespace udp
