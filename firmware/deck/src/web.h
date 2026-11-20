// Web page for deskstrate-deck (https://deck.deskstrate.nogsnet.com/):
//   GET /            the screen and key lights, mirrored live (mirror.h)
//   GET /ws          the mirror's WebSocket
//   GET /api/status  version, uptime, Wi-Fi, free RAM and the mirror's viewers, as JSON
//   GET /api/tls, /api/tls/csr  HTTPS status / this board's CSR (web_tls.h)
// The same handlers are served over HTTPS on 443 once a certificate is published.
// View only and unauthenticated: trusted LAN only, like the core's and kb's pages.
#pragma once

#include "esp_err.h"

// Call once Wi-Fi has started and NVS is initialised.
esp_err_t web_start();
