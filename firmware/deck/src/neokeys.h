#pragma once

// The four NeoKey lights. Each key animates the host it stands for, in that host's
// screen colour (PC orange, Mac blue, guests purple/coral):
//   top keys     the host on the monitor above them (left / right); dim when off
//   bottom keys  the host they send the keyboard to (PC / Mac, or in split mode the
//                left / right screen's host): full animation while the HID hub is on
//                that host, a dim glow otherwise
// Patterns: PC breathes, Mac ripples between blue and cyan, guests beat twice.
// While a top key is held, the bottom key it chords with blinks white. The menu
// shows back (red), select (green) and up/down (white). Before the first STATE from
// the core, all four breathe amber. Every press flashes its key white.

#include <cstdint>

#include "controls.h"
#include "driver/gpio.h"
#include "esp_err.h"

namespace neokeys {

struct Scene {
    bool core_seen = false;
    bool menu = false;
    uint8_t held = 0;         // bit n = controls::Key n is down
    controls::View view;
    uint8_t brightness = 255; // overall scale, 0 = off
};

esp_err_t start(gpio_num_t pin);
void flash(controls::Key key, uint32_t now_ms);
// Call every frame (~30 ms).
void render(const Scene &scene, uint32_t now_ms);

}  // namespace neokeys
