// Deskstrate additions to Keychron's stock Q3 Max keymap (build.sh copies the stock
// keymap.c next to this file).
//
// Pin test: PB12 starts high and PA3 low, and each Esc press swaps them, so the two
// spare MCU pins the keyboard's ESP32 will use can be checked with a meter. Esc still
// types as normal. See README.md for where the pins are.
#include QMK_KEYBOARD_H

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
    if (keycode == KC_ESC && record->event.pressed) {
        pins_flipped = !pins_flipped;
        write_test_pins();
    }
    return true;
}
