#include "controls.h"

namespace controls {

namespace {

bool is_top(Key key) { return key == kTopLeft || key == kTopRight; }

bool is_pc_or_mac(uint8_t host) { return host == KVM_USB_PC || host == KVM_USB_MAC; }

Target everything_to(uint8_t host) {
    Target t;
    t.mon[KVM_MON_LEFT] = host_input(host, KVM_MON_LEFT);
    t.mon[KVM_MON_RIGHT] = host_input(host, KVM_MON_RIGHT);
    t.usb[KVM_HUB_HID] = host;
    t.usb[KVM_HUB_MEDIA] = host;
    return t;
}

Target split(uint8_t left, uint8_t right, uint8_t hubs) {
    Target t;
    t.mon[KVM_MON_LEFT] = host_input(left, KVM_MON_LEFT);
    t.mon[KVM_MON_RIGHT] = host_input(right, KVM_MON_RIGHT);
    t.usb[KVM_HUB_HID] = hubs;
    t.usb[KVM_HUB_MEDIA] = hubs;
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
    switch (view.mon[mon_id]) {
        case KVM_IN_DP: return KVM_USB_PC;
        case KVM_IN_USBC: return KVM_USB_MAC;
        case KVM_IN_HDMI: return mon_id == KVM_MON_LEFT ? KVM_USB_GUEST1 : KVM_USB_GUEST2;
    }
    return KVM_USB_UNKNOWN;
}

uint8_t host_input(uint8_t host, uint8_t mon_id) {
    switch (host) {
        case KVM_USB_PC: return KVM_IN_DP;
        case KVM_USB_MAC: return KVM_IN_USBC;
        case KVM_USB_GUEST1: return mon_id == KVM_MON_LEFT ? KVM_IN_HDMI : KVM_IN_KEEP;
        case KVM_USB_GUEST2: return mon_id == KVM_MON_RIGHT ? KVM_IN_HDMI : KVM_IN_KEEP;
    }
    return KVM_IN_KEEP;
}

bool is_split(const View &view) {
    const uint8_t left = monitor_host(view, KVM_MON_LEFT);
    const uint8_t right = monitor_host(view, KVM_MON_RIGHT);
    return left != KVM_USB_UNKNOWN && right != KVM_USB_UNKNOWN && left != right;
}

bool plan(const Event &event, const View &view, Target &out) {
    if (event.type == EventType::Chord) {
        if (event.modifier == kTopRight && event.key == kBottomLeft) {
            out = split(KVM_USB_PC, KVM_USB_MAC, KVM_USB_PC);
            return true;
        }
        if (event.modifier == kTopLeft && event.key == kBottomRight) {
            out = split(KVM_USB_MAC, KVM_USB_PC, KVM_USB_MAC);
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
                out.usb[KVM_HUB_HID] = monitor_host(view, left ? KVM_MON_LEFT : KVM_MON_RIGHT);
            } else {
                out = everything_to(left ? KVM_USB_PC : KVM_USB_MAC);
            }
            return true;
        }
        case kTopRight: {
            if (!is_split(view)) return false;
            // Everything follows the keyboard; a guest can't fill both screens.
            uint8_t host = view.usb[KVM_HUB_HID];
            if (!is_pc_or_mac(host)) host = view.usb[KVM_HUB_MEDIA];
            if (!is_pc_or_mac(host)) host = KVM_USB_PC;
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
    if (monitor_host(view, KVM_MON_LEFT) == KVM_USB_UNKNOWN || monitor_host(view, KVM_MON_RIGHT) == KVM_USB_UNKNOWN) {
        return t;
    }
    t.mon[KVM_MON_LEFT] = view.mon[KVM_MON_RIGHT];
    t.mon[KVM_MON_RIGHT] = view.mon[KVM_MON_LEFT];
    return t;
}

}  // namespace controls
