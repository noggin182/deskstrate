// kvm_controls - what the deck's keys and the keyboard's Fn keys ask the core for,
// worked out from the core's latest STATE. Shared by the deck (firmware/deck) and the
// ESP inside the keyboard (firmware/kb) so the two always behave the same. No ESP-IDF
// here: test/test_kvm_controls.cpp builds and runs it on a PC.
//
//   plan_side          deck bottom-left/right, keyboard Fn+Left/Right: everything to the
//                      PC / Mac; in split mode only the HID hub moves, to the left /
//                      right screen's host
//   plan_leave_split   deck top-right tap: everything follows the HID hub's host
//   plan_toggle_split  keyboard Fn+Up: leave split as above, or enter it keeping the left
//                      monitor, with the right monitor and the HID hub on the other host
//                      and the media hub on the left monitor's host (as split())
//   plan_swap          deck menu Swap: the monitors swap inputs, hubs stay
//   plan_swap_media    keyboard Fn+Down: as plan_swap, and the media hub follows its screen
//   other_host         keyboard Fn+knob: the PC or Mac that doesn't have the HID hub
#pragma once

#include <cstdint>

#include "kvm_proto.h"

namespace kvm_controls {

// Hosts as kvm_proto hub bytes. Each guest is cabled to one monitor's HDMI: guest 1
// (port 2) to the left, guest 2 (port 3) to the right.
enum Host : uint8_t {
    kPc = KVM_HOST_PC,
    kMac = KVM_HOST_MAC,
    kGuest1 = KVM_HUB_GUEST1,
    kGuest2 = KVM_HUB_GUEST2,
    kNoHost = KVM_UNKNOWN,
};

// The core's 4 STATE bytes, by enum kvm_slot ('-' = not connected).
struct View {
    uint8_t b[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};
};

// What to ask the core for: REQUEST bytes, '-' = leave alone.
using Target = View;

// True if the target changes nothing (every byte '-').
bool is_empty(const Target &t);

// Host (enum Host) shown by monitor mon_id, or kNoHost (unknown). Standby doesn't hide
// it: the desk is asleep, and any request wakes it.
uint8_t monitor_host(const View &view, uint8_t mon_id);
// The monitor byte that shows host on monitor mon_id, or KVM_UNKNOWN if it can't
// (a guest only exists on one monitor's HDMI).
uint8_t host_input(uint8_t host, uint8_t mon_id);
// Split mode = the two monitors show different hosts.
bool is_split(const View &view);

Target everything_to(uint8_t host);
// Monitors to left / right; the media hub to left's host, the HID hub to right's.
Target split(uint8_t left, uint8_t right);

// left: the left-hand key (PC / left screen), else the right-hand one (Mac / right screen).
Target plan_side(const View &view, bool left);
// Empty (changes nothing) when not in split mode.
Target plan_leave_split(const View &view);
// Empty when the core's state isn't known yet.
Target plan_toggle_split(const View &view);
// Empty unless both monitors show something.
Target plan_swap(const View &view);
Target plan_swap_media(const View &view);

// kPc or kMac: whichever of the two doesn't have the HID hub; kNoHost if the HID hub is
// on a guest or unknown.
uint8_t other_host(const View &view);

}  // namespace kvm_controls
