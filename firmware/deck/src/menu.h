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
};

struct Result {
    Command command = Command::None;
    uint8_t a = 0;
    uint8_t b = 0;
};

constexpr uint8_t kLightLevels = 3;
uint8_t light_brightness(uint8_t level);

void open(const controls::View &view, uint8_t light_level);
void close();
bool is_open();

// A key pressed while the menu is open. The caller carries out the result.
Result key(controls::Key key, const controls::View &view, uint8_t light_level);

// Redraws the values (after a STATE or a lights change).
void refresh(const controls::View &view, uint8_t light_level);

}  // namespace menu
