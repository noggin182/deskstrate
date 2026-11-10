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
//   hold TopRight + BottomLeft split: PC left (media hub), Mac right (HID hub)
//   hold TopLeft + BottomRight split: Mac left (media hub), PC right (HID hub)
//   tap TopRight               leave split mode: everything to the media hub's host
//   tap TopLeft                open the menu
// Top keys act on release, and only if no bottom key was pressed while they were held.
// Bottom keys act on press.
//
// Menu mode: every key acts on press. TopLeft = back, TopRight = select,
// BottomLeft = up, BottomRight = down.

#include <cstdint>

#include "kvm_controls.h"

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

// Hosts, View/Target and what each key asks for live in kvm_controls, shared with the
// keyboard's Fn keys.
using namespace kvm_controls;

// Normal-mode action for a tap or chord, or false if it does nothing.
bool plan(const Event &event, const View &view, Target &out);

}  // namespace controls
