// Over-the-air updates into the spare OTA slot, with bootloader rollback.
//   GET  /api/ota   {"running","version","built","state","next"}
//   POST /api/ota   raw app image (.pio/build/s3_mini/firmware.bin) as the body;
//                   on success the core replies, then reboots into it
// LAN only, no auth, like the rest of the web API.
//
// A new image boots in pending-verify. confirm_boot() marks it valid once Wi-Fi has an IP
// and the web server is up (so the next update can still reach it); if that doesn't
// happen within kConfirmTimeoutMs it rolls back to the previous image and reboots.
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

namespace ota {

inline constexpr uint32_t kConfirmTimeoutMs = 60000;

// Logs the running slot and image version.
void log_running();

// Registers /api/ota on the web server.
esp_err_t register_handlers(httpd_handle_t server);

// Call once after start-up. web_ok: the web server started. No-op unless the running
// image is pending verify.
void confirm_boot(bool web_ok);

}  // namespace ota
