// The Mac's side of the bridge: the S3's built-in USB Serial/JTAG port, which the Mac
// sees as /dev/cu.usbmodem... (USB ID 303A:1001, serial number = the board's MAC).
//
// Framing is one line per datagram. A line ends at \r, \n or \r\n; empty lines are
// skipped and lines over kMaxLine bytes are dropped. Everything the bridge writes ends
// in \n.
//
// ESP_LOG output goes to the same port with every line prefixed "# ", so the Mac daemon
// can tell the bridge's logs from kvm_proto (and copies them into its host.log). One
// lock guards every write, so a log line never lands inside a data line.
//
// Writes give up after kWriteTimeoutMs, and are skipped while no USB host is attached,
// so a Mac that is asleep, undocked or not reading never stalls the bridge.
#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"

namespace serial {

inline constexpr size_t kMaxLine = 256;
inline constexpr uint32_t kWriteTimeoutMs = 20;

// Installs the driver and the "# " log hook. Call first thing in app_main.
esp_err_t start();

// Starts reading lines from the Mac; each one is handed to `on_line` (from the reader task).
using LineHandler = void (*)(const uint8_t *line, size_t len);
esp_err_t start_reader(LineHandler on_line);

// Writes data plus "\n" to the Mac. len at most kMaxLine.
void write_line(const uint8_t *data, size_t len);

}  // namespace serial
