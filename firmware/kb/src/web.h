// Web page and JSON for deskstrate-kb (http://deskstrate-kb.local/), plus mDNS.
//   GET  /            hello page: version, Wi-Fi, keyboard link, last lines from it
//   GET  /api/status  the same as JSON
//   POST /api/kb      body is one line of text, sent to the keyboard
// Unauthenticated: trusted LAN only, like the core's bench UI.
#pragma once

#include "esp_err.h"

esp_err_t web_start(const char *hostname);
