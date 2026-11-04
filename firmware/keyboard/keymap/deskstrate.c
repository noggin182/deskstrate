// Deskstrate additions to Keychron's stock Q3 Max keymap (build.sh copies the stock
// keymap.c next to this file).
//
// KVM keys (deskstrate_keys.h): each builds the kvm_proto request (KR + the wanted
// [left, right, HID hub, media hub], 0xFF = leave alone). Sending it needs the ESP32
// link, which isn't built yet, so for now they only exist to be bound in VIA.
//
// Pin test: PB12 starts high and PA3 low, and each Esc press swaps them, so the two
// spare MCU pins the keyboard's ESP32 will use can be checked with a meter. Esc still
// types as normal. See README.md for where the pins are.
#include QMK_KEYBOARD_H
#include "deskstrate_keys.h"
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

static void send_kvm_request(const uint8_t state[4]) {
    uint8_t frame[6] = {'K', 'R', state[0], state[1], state[2], state[3]};
    (void)frame;  // TODO: send to the ESP32 once the PB12 link exists.
}

static bool pins_flipped = false;

static void write_test_pins(void) {
    gpio_write_pin(B12, !pins_flipped);
    gpio_write_pin(A3, pins_flipped);
}

void keyboard_post_init_user(void) {
    gpio_set_pin_output_push_pull(B12);
    gpio_set_pin_output_push_pull(A3);
    write_test_pins();
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (keycode >= KVM_ALL_PC && keycode < DESKSTRATE_KEYS_END) {
        if (record->event.pressed) send_kvm_request(kvm_requests[keycode - KVM_ALL_PC]);
        return false;
    }
    if (keycode == KC_ESC && record->event.pressed) {
        pins_flipped = !pins_flipped;
        write_test_pins();
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
