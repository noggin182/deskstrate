// Over-the-air updates into the spare OTA slot, with bootloader rollback.
//   GET  /api/ota   {"running","version","built","state","next","auth","pull"}
//   POST /api/ota   raw app image (.pio/build/s3_mini/firmware.bin) as the body, with
//                   "Authorization: Bearer <token>"; on success the core replies, then
//                   reboots into it
// The token comes from menuconfig (DESKSTRATE_OTA_TOKEN) and is kept in NVS, so builds
// without it (CI) keep the stored one. No token stored = uploads refused. The rest of
// the web API is still unauthenticated and LAN only.
// This is the push path, for the bench. The core also pulls releases from GitHub by
// itself (components/ota_pull), which also owns the rollback check for both paths.
#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

namespace ota {

// Logs the running slot and image version.
void log_running();

// Loads the upload token (NVS must be initialised) and registers /api/ota.
esp_err_t register_handlers(httpd_handle_t server);

}  // namespace ota
