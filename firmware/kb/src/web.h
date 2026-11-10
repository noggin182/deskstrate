// Web page and JSON for deskstrate-kb (https://kb.deskstrate.nogsnet.com/), plus mDNS.
//   GET  /            hello page: version, Wi-Fi, keyboard link, last lines from it
//   GET  /api/status  the same as JSON
//   POST /api/kb      body (up to 32 characters) goes to the keyboard, which echoes it
//   GET  /api/tls, /api/tls/csr  HTTPS status / this board's CSR (web_tls.h)
// The same handlers are served over HTTPS on 443 once a certificate is published.
// Unauthenticated: trusted LAN only, like the core's bench UI.
#pragma once

#include "esp_err.h"

esp_err_t web_start(const char *hostname);
