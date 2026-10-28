#pragma once

// The four NeoKey lights. Each key animates the host it stands for, in that host's
// screen colour (PC orange, Mac blue, guests purple/coral):
//   top keys     the host on the monitor above them (left / right); nearly dark in
//                standby, a dim red glow when the monitor isn't connected
//   bottom keys  the host they send the keyboard to (PC / Mac, or in split mode the
//                left / right screen's host): full strength while the HID hub is on
//                that host, dim otherwise (and while the HID hub is offline)
// The animation cycles through hues close to the host's colour at a constant
// brightness (the PC's orange drifts to yellow and a hint of red).
// While a top key is held, the bottom key it chords with pulses white. The menu
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
