// Deskstrate additions to Keychron's stock Q3 Max keymap (build.sh copies the stock
// keymap.c next to this file).
//
// KVM keys (deskstrate_keys.h): each sends a kvm_proto request ([left, right, HID hub,
// media hub], 0xFF = leave alone) to the ESP32 inside the keyboard over the PB12/PA3
// link (kb_link.h), which passes it to the core. Only in cable mode with USB power: on
// Bluetooth or 2.4 GHz the ESP32 is asleep and the keys do nothing.
#include QMK_KEYBOARD_H
#include "deskstrate_keys.h"
#include "kb_link.h"
#include "transport.h"

// Keychron's base layers (keymap.c's enum layers): the back switch picks one.
#define LAYER_MAC_BASE 0
#define LAYER_WIN_BASE 2

// kvm_proto (components/kvm_proto): monitor byte = host (0 PC, 1 Mac, 3 guest),
// hub byte = port (0 PC, 1 Mac).
static const uint8_t kvm_requests[DESKSTRATE_KEYS_END - KVM_ALL_PC][4] = {
    [KVM_ALL_PC - KVM_ALL_PC]    = {0x00, 0x00, 0x00, 0x00},
    [KVM_ALL_MAC - KVM_ALL_PC]   = {0x01, 0x01, 0x01, 0x01},
    // Split, as on the deck: the right monitor goes to the second host, the rest to the first.
    [KVM_SPLIT_PC_MAC - KVM_ALL_PC] = {0x00, 0x01, 0x00, 0x00},
    [KVM_SPLIT_MAC_PC - KVM_ALL_PC] = {0x01, 0x00, 0x01, 0x01},
    [KVM_L_PC - KVM_ALL_PC]      = {0x00, 0xFF, 0xFF, 0xFF},
    [KVM_L_MAC - KVM_ALL_PC]     = {0x01, 0xFF, 0xFF, 0xFF},
    [KVM_L_GUEST - KVM_ALL_PC]   = {0x03, 0xFF, 0xFF, 0xFF},
    [KVM_R_PC - KVM_ALL_PC]      = {0xFF, 0x00, 0xFF, 0xFF},
    [KVM_R_MAC - KVM_ALL_PC]     = {0xFF, 0x01, 0xFF, 0xFF},
    [KVM_R_GUEST - KVM_ALL_PC]   = {0xFF, 0x03, 0xFF, 0xFF},
    [KVM_HID_PC - KVM_ALL_PC]    = {0xFF, 0xFF, 0x00, 0xFF},
    [KVM_HID_MAC - KVM_ALL_PC]   = {0xFF, 0xFF, 0x01, 0xFF},
    [KVM_MEDIA_PC - KVM_ALL_PC]  = {0xFF, 0xFF, 0xFF, 0x00},
    [KVM_MEDIA_MAC - KVM_ALL_PC] = {0xFF, 0xFF, 0xFF, 0x01},
};

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode >= KVM_ALL_PC && keycode < DESKSTRATE_KEYS_END) {
        if (record->event.pressed) kb_link_send_kvm_request(kvm_requests[keycode - KVM_ALL_PC]);
        return false;
    }
    return true;
}

// Mac/Windows layout follows the host. On the cable, QMK's OS detection picks the base
// layer each time the keyboard enumerates (so after every hub switch); the back switch
// still decides on Bluetooth and 2.4 GHz, where there's no enumeration to go on.
static void apply_host_layout(void) {
    switch (detected_host_os()) {
        case OS_MACOS:
        case OS_IOS:
            default_layer_set(1UL << LAYER_MAC_BASE);
            break;
        case OS_WINDOWS:
        case OS_LINUX:
            default_layer_set(1UL << LAYER_WIN_BASE);
            break;
        default:  // not sure: leave the switch's choice
            break;
    }
}

// QMK only clears its detection data when the keyboard boots. With 5V kept up across a
// hub switch the keyboard never reboots, so the second host's enumeration was mixed with
// the first's and the result never changed. Clear it when a host that had configured us
// resets the bus (a new host), but not on the resets inside one enumeration, which the
// detection needs to see.
void notify_usb_device_state_change_user(struct usb_device_state state) {
    static bool was_configured = false;
    if (state.configure_state == USB_DEVICE_STATE_CONFIGURED) {
        was_configured = true;
    } else if (state.configure_state == USB_DEVICE_STATE_INIT && was_configured) {
        was_configured = false;
        erase_wlength_data();
    }
}

bool process_detected_host_os_user(os_variant_t os) {
    if (get_transport() == TRANSPORT_USB) apply_host_layout();
    return true;
}

void matrix_scan_user(void) {
    kb_link_task();

    static transport_t last = TRANSPORT_NONE;
    transport_t        now  = get_transport();
    if (now == last) return;
    last = now;
    if (now == TRANSPORT_USB) {
        apply_host_layout();
    } else {
        dip_switch_read(true);  // back to the switch's layout
    }
}
