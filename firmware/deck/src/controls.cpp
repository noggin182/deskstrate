#include "controls.h"

namespace controls {

namespace {

bool is_top(Key key) { return key == kTopLeft || key == kTopRight; }

bool is_pc_or_mac(uint8_t host) { return host == kPc || host == kMac; }

Target everything_to(uint8_t host) {
    Target t;
    t.b[KVM_MON_LEFT] = host_input(host, KVM_MON_LEFT);
    t.b[KVM_MON_RIGHT] = host_input(host, KVM_MON_RIGHT);
    t.b[KVM_HUB_HID] = host;
    t.b[KVM_HUB_MEDIA] = host;
    return t;
}

Target split(uint8_t left, uint8_t right, uint8_t hubs) {
    Target t;
    t.b[KVM_MON_LEFT] = host_input(left, KVM_MON_LEFT);
    t.b[KVM_MON_RIGHT] = host_input(right, KVM_MON_RIGHT);
    t.b[KVM_HUB_HID] = hubs;
    t.b[KVM_HUB_MEDIA] = hubs;
    return t;
}

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

uint8_t monitor_host(const View &view, uint8_t mon_id) {
    const uint8_t b = view.b[mon_id];
    if (kvm_mon_standby(b)) return kNoHost;
    switch (kvm_mon_host(b)) {
        case KVM_HOST_PC: return kPc;
        case KVM_HOST_MAC: return kMac;
        case KVM_HOST_GUEST: return mon_id == KVM_MON_LEFT ? kGuest1 : kGuest2;
    }
    return kNoHost;
}

uint8_t host_input(uint8_t host, uint8_t mon_id) {
    switch (host) {
        case kPc: return KVM_HOST_PC;
        case kMac: return KVM_HOST_MAC;
        case kGuest1: return mon_id == KVM_MON_LEFT ? static_cast<uint8_t>(KVM_HOST_GUEST) : KVM_UNKNOWN;
        case kGuest2: return mon_id == KVM_MON_RIGHT ? static_cast<uint8_t>(KVM_HOST_GUEST) : KVM_UNKNOWN;
    }
    return KVM_UNKNOWN;
}

bool is_split(const View &view) {
    const uint8_t left = monitor_host(view, KVM_MON_LEFT);
    const uint8_t right = monitor_host(view, KVM_MON_RIGHT);
    return left != kNoHost && right != kNoHost && left != right;
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
        case kBottomRight: {
            const bool left = event.key == kBottomLeft;
            if (is_split(view)) {
                out = Target{};
                out.b[KVM_HUB_HID] = monitor_host(view, left ? KVM_MON_LEFT : KVM_MON_RIGHT);
            } else {
                out = everything_to(left ? kPc : kMac);
            }
            return true;
        }
        case kTopRight: {
            if (!is_split(view)) return false;
            // Everything follows the keyboard; a guest can't fill both screens.
            uint8_t host = view.b[KVM_HUB_HID];
            if (!is_pc_or_mac(host)) host = view.b[KVM_HUB_MEDIA];
            if (!is_pc_or_mac(host)) host = kPc;
            out = everything_to(host);
            return true;
        }
        default:
            return false;  // TopLeft opens the menu (handled by the caller)
    }
}

Target plan_swap(const View &view) {
    // Each monitor takes the other's input. HDMI stays HDMI, so a guest swaps for the
    // other monitor's guest (each guest is cabled to one monitor).
    Target t;
    if (monitor_host(view, KVM_MON_LEFT) == kNoHost || monitor_host(view, KVM_MON_RIGHT) == kNoHost) {
        return t;
    }
    t.b[KVM_MON_LEFT] = kvm_mon_host(view.b[KVM_MON_RIGHT]);
    t.b[KVM_MON_RIGHT] = kvm_mon_host(view.b[KVM_MON_LEFT]);
    return t;
}

}  // namespace controls
