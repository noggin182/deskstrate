// Bench web UI on port 80: one page with a row per hub and per monitor, plus a tiny
// JSON API. LAN only, no auth: this is a test interface, not the real control path.
//   GET  /                         the page
//   GET  /api/state                {"mon":[{"online","input","power","side"}, ...]}
//   POST /api/mon?i=N&input=0x0F   switch monitor N's input (decimal or 0x hex)
//   POST /api/mon?i=N&power=1|4    monitor N on / standby
//   POST /api/hub?i=N&port=0..3    switch hub N
//   GET/POST /api/ota              update status / upload (see ota.h)
// Also advertises <hostname>.local and _http._tcp over mDNS.
#pragma once

#include "esp_err.h"

esp_err_t web_start(const char *hostname);
