#pragma once

// Live mirror of the TFT and the four key lights on the deck's web page (web.cpp).
//
// The page opens a WebSocket at /ws. Every region LVGL flushes to the panel is also
// copied into a shadow of the whole screen (135 x 240 RGB565, ~64 KB of internal RAM,
// allocated only while someone is watching) and marked dirty. A sender task sends the
// dirty rectangle at most every kFrameMs, in bands of at most kBandBytes:
//   binary frame   x, y, w, h (uint16 little-endian), then w*h RGB565 pixels (LE)
// A slow viewer gets in-between redraws merged, never queued, and the panel never waits
// for the network. A new viewer makes LVGL redraw the whole screen, so it gets every
// pixel. Key lights and the backlight go as a text frame when they change (at most
// every kStateMs):
//   {"keys":["rrggbb",...4 in controls::Key order],"bl":duty,"fade_ms":ms}
// The key colours are the bytes sent to the NeoPixels; bl is the backlight's PWM duty
// (of 255) it is fading to. A viewer the deck can't take gets {"error":"..."}.

#include <cstdint>

#include "esp_err.h"
#include "esp_http_server.h"
#include "lvgl.h"

namespace mirror {

inline constexpr int kMaxViewers = 2;
inline constexpr uint32_t kFrameMs = 50;
inline constexpr uint32_t kStateMs = 100;
inline constexpr size_t kBandBytes = 8192;

// From the LVGL task: the flush callback's area and pixels.
void flush(const lv_area_t *area, const lv_color_t *pixels);
// From the LVGL task, every loop: redraws the whole screen when a new viewer joined.
void poll();
// The key lights as sent to the strip, in controls::Key order.
void keys(const uint8_t rgb[4][3]);
// The backlight's target duty and how long it takes to get there.
void backlight(uint8_t duty, uint32_t fade_ms);

// Starts the sender task.
esp_err_t start();
// Registers GET /ws on a server (the plain one, and HTTPS each time it starts).
esp_err_t register_handler(httpd_handle_t server);
// Before a server stops: drops its viewers, after any send in progress has finished.
void server_stopping(httpd_handle_t server);

int viewers();
bool buffered();  // whether the shadow screen is allocated

}  // namespace mirror
