#pragma once

// Deck key gestures and what they ask the core for. No ESP-IDF here, so the logic
// can be built and tested on a PC.
//
// Keys (looking at the deck):   TopLeft (Menu)      TopRight (Function)
//                               BottomLeft (A)      BottomRight (B)
//
// Normal mode:
//   BottomLeft / BottomRight   everything to the PC / Mac; in split mode only the HID
//                              hub moves, to the left / right screen's host
//   hold TopRight + BottomLeft split: PC left, Mac right, both hubs on the PC
//   hold TopLeft + BottomRight split: Mac left, PC right, both hubs on the Mac
//   tap TopRight               leave split mode: everything to the HID hub's host
//   tap TopLeft                open the menu
// Top keys act on release, and only if no bottom key was pressed while they were held.
// Bottom keys act on press.
//
// Menu mode: every key acts on press. TopLeft = back, TopRight = select,
// BottomLeft = up, BottomRight = down.

#include <cstdint>

#include "kvm_proto.h"

namespace controls {

enum Key : uint8_t { kTopLeft, kTopRight, kBottomLeft, kBottomRight, kKeyCount };

enum class EventType : uint8_t { None, Tap, Chord };

struct Event {
    EventType type = EventType::None;
    Key key = kTopLeft;       // Tap: the key; Chord: the bottom key that was tapped
    Key modifier = kTopLeft;  // Chord only: the top key being held
};

// Turns debounced key levels into taps and chords. Call update() with the current
// levels; it reports at most one event per key edge, in key order.
class Gestures {
public:
    // A top key held longer than this without a chord does nothing on release.
    static constexpr uint32_t kTapMaxMs = 1500;

    // pressed: bit n = key n is down. menu: the deck is showing the menu.
    // Returns the number of events written to out (at most kKeyCount).
    uint8_t update(uint8_t pressed, uint32_t now_ms, bool menu, Event out[kKeyCount]);

    bool held(Key key) const { return (pressed_ & (1u << key)) != 0; }

private:
    uint8_t pressed_ = 0;
    uint8_t used_ = 0;  // top keys that acted as a modifier (or were pressed in the menu)
    uint32_t down_at_[kKeyCount] = {};
};

// Hosts as the deck names them: the hub port numbers (a kvm_proto hub byte). Each guest
// is cabled to one monitor's HDMI: guest 1 (port 2) to the left, guest 2 (port 3) to
// the right.
enum Host : uint8_t { kPc = 0, kMac = 1, kGuest1 = 2, kGuest2 = 3, kNoHost = KVM_UNKNOWN };

// The core's 4 STATE bytes, by enum kvm_slot (0xFF = not connected).
struct View {
    uint8_t b[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};
};

// What to ask the core for: REQUEST bytes, 0xFF = leave alone.
using Target = View;

// Host (enum Host) shown by monitor mon_id, or kNoHost (standby, unknown).
uint8_t monitor_host(const View &view, uint8_t mon_id);
// The monitor byte that shows host on monitor mon_id, or KVM_UNKNOWN if it can't
// (a guest only exists on one monitor's HDMI).
uint8_t host_input(uint8_t host, uint8_t mon_id);
// Split mode = the two monitors show different hosts.
bool is_split(const View &view);

// Normal-mode action for a tap or chord, or false if it does nothing.
bool plan(const Event &event, const View &view, Target &out);

// Swap the two monitors' inputs (both hubs stay).
Target plan_swap(const View &view);

}  // namespace controls
