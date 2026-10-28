#pragma once

// The four NeoKey lights. Each key animates a host in that host's screen colour
// (PC orange, Mac blue, guests purple/coral):
//   top keys     the host on the monitor above them (left / right); a slow, low
//                cool-white breath in standby
//   bottom keys  the host each USB hub is on: bottom left the HID hub (keyboard,
//                mouse), bottom right the media hub
// A monitor or hub that isn't connected breathes vivid red (logarithmic, high floor).
// The animation cycles through hues close to the host's colour at a constant
// brightness (the PC's orange drifts to yellow). When what a key
// shows changes (a new input, host, the menu...) it fades to the new colour.
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
