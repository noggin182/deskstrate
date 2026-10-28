#pragma once

// The deck's menu: a full-screen list drawn over the status screen. Opened by tapping
// the top-left key; while it is open the keys navigate (see controls.h).

#include <cstdint>

#include "controls.h"

namespace menu {

enum class Command : uint8_t {
    None,
    Close,
    Monitor,     // a = kvm_mon_id, b = kvm_input
    Usb,         // a = kvm_usb_hub, b = kvm_usb_port
    Swap,        // swap the two monitors' inputs
    ScreensOff,  // both monitors to standby
    Lights,      // a = new key light level (0 high, 1 low, 2 off)
    DebugCode,   // a = 1 to show the state code over the volume panel
};

struct Result {
    Command command = Command::None;
    uint8_t a = 0;
    uint8_t b = 0;
};

constexpr uint8_t kLightLevels = 3;

// Deck-local settings the menu shows and changes (kept in NVS by main.cpp).
struct Settings {
    uint8_t light_level = 0;
    bool debug_code = true;
};

uint8_t light_brightness(uint8_t level);

void open(const controls::View &view, const Settings &settings);
void close();
bool is_open();

// A key pressed while the menu is open. The caller carries out the result.
Result key(controls::Key key, const controls::View &view, const Settings &settings);

// Redraws the values (after a STATE or a settings change).
void refresh(const controls::View &view, const Settings &settings);

}  // namespace menu
