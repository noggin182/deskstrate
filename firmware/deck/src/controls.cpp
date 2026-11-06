#include "controls.h"

namespace controls {

namespace {

bool is_top(Key key) { return key == kTopLeft || key == kTopRight; }

}  // namespace

uint8_t Gestures::update(uint8_t pressed, uint32_t now_ms, bool menu, Event out[kKeyCount]) {
    uint8_t n = 0;
    for (uint8_t k = 0; k < kKeyCount; ++k) {
        const Key key = static_cast<Key>(k);
        const uint8_t bit = 1u << k;
        const bool was = (pressed_ & bit) != 0;
        const bool now = (pressed & bit) != 0;
        if (was == now) continue;

        if (now) {
            pressed_ |= bit;
            down_at_[k] = now_ms;
            if (menu) {
                // Menu keys act on press; a top key's release must not act again.
                used_ |= bit;
                out[n++] = {EventType::Tap, key, kTopLeft};
            } else if (is_top(key)) {
                used_ &= ~bit;  // decided on release
            } else if (held(kTopRight)) {
                used_ |= 1u << kTopRight;
                out[n++] = {EventType::Chord, key, kTopRight};
            } else if (held(kTopLeft)) {
                used_ |= 1u << kTopLeft;
                out[n++] = {EventType::Chord, key, kTopLeft};
            } else {
                out[n++] = {EventType::Tap, key, kTopLeft};
            }
        } else {
            pressed_ &= ~bit;
            if (is_top(key) && !(used_ & bit) && !menu && now_ms - down_at_[k] <= kTapMaxMs) {
                out[n++] = {EventType::Tap, key, kTopLeft};
            }
            used_ &= ~bit;
        }
    }
    return n;
}

bool plan(const Event &event, const View &view, Target &out) {
    if (event.type == EventType::Chord) {
        if (event.modifier == kTopRight && event.key == kBottomLeft) {
            out = split(kPc, kMac, kPc);
            return true;
        }
        if (event.modifier == kTopLeft && event.key == kBottomRight) {
            out = split(kMac, kPc, kMac);
            return true;
        }
        return false;
    }
    if (event.type != EventType::Tap) return false;

    switch (event.key) {
        case kBottomLeft:
        case kBottomRight:
            out = plan_side(view, event.key == kBottomLeft);
            return true;
        case kTopRight:
            out = plan_leave_split(view);
            return !is_empty(out);
        default:
            return false;  // TopLeft opens the menu (handled by the caller)
    }
}

}  // namespace controls
