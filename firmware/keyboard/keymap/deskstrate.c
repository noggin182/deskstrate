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

// kvm_proto (components/kvm_proto): monitor byte = host (0 PC, 1 Mac, 3 guest),
// hub byte = port (0 PC, 1 Mac).
static const uint8_t kvm_requests[DESKSTRATE_KEYS_END - KVM_ALL_PC][4] = {
    [KVM_ALL_PC - KVM_ALL_PC]    = {0x00, 0x00, 0x00, 0x00},
    [KVM_ALL_MAC - KVM_ALL_PC]   = {0x01, 0x01, 0x01, 0x01},
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
