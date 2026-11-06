#include "kvm_controls.h"

namespace kvm_controls {

namespace {

bool is_pc_or_mac(uint8_t host) { return host == kPc || host == kMac; }

// The view once the core has carried out t (monitors: the host nibble only).
View after(const View &view, const Target &t) {
    View v = view;
    for (uint8_t i = 0; i < KVM_SLOTS; ++i) {
        if (t.b[i] != KVM_UNKNOWN) v.b[i] = t.b[i];
    }
    return v;
}

}  // namespace

bool is_empty(const Target &t) {
    for (uint8_t i = 0; i < KVM_SLOTS; ++i) {
        if (t.b[i] != KVM_UNKNOWN) return false;
    }
    return true;
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

Target plan_side(const View &view, bool left) {
    if (!is_split(view)) return everything_to(left ? kPc : kMac);
    Target t;
    t.b[KVM_HUB_HID] = monitor_host(view, left ? KVM_MON_LEFT : KVM_MON_RIGHT);
    return t;
}

Target plan_leave_split(const View &view) {
    if (!is_split(view)) return Target{};
    // Everything follows the keyboard; a guest can't fill both screens.
    uint8_t host = view.b[KVM_HUB_HID];
    if (!is_pc_or_mac(host)) host = view.b[KVM_HUB_MEDIA];
    if (!is_pc_or_mac(host)) host = kPc;
    return everything_to(host);
}

Target plan_toggle_split(const View &view) {
    if (is_split(view)) return plan_leave_split(view);
    const uint8_t left = view.b[KVM_MON_LEFT];
    if (left == KVM_UNKNOWN) return Target{};
    // The left monitor and both hubs stay; the right monitor gets the other host.
    Target t;
    t.b[KVM_MON_RIGHT] = host_input(kvm_mon_host(left) == KVM_HOST_PC ? kMac : kPc, KVM_MON_RIGHT);
    return t;
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

Target plan_swap_media(const View &view) {
    Target t = plan_swap(view);
    if (is_empty(t)) return t;
    // The media hub stays with its screen: it moves to whatever that screen shows next.
    const View next = after(view, t);
    const uint8_t media = view.b[KVM_HUB_MEDIA];
    for (uint8_t mon = KVM_MON_LEFT; mon <= KVM_MON_RIGHT; ++mon) {
        if (media == monitor_host(view, mon)) {
            const uint8_t host = monitor_host(next, mon);
            if (host != media && host != kNoHost) t.b[KVM_HUB_MEDIA] = host;
            break;
        }
    }
    return t;
}

uint8_t other_host(const View &view) {
    switch (view.b[KVM_HUB_HID]) {
        case kPc: return kMac;
        case kMac: return kPc;
    }
    return kNoHost;
}

}  // namespace kvm_controls
