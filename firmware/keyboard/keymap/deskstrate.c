// Deskstrate additions to Keychron's stock Q3 Max keymap (build.sh copies the stock
// keymap.c next to this file).
//
// KVM keys (deskstrate_keys.h): each sends its action to the ESP32 inside the keyboard
// over the PB12/PA3 link (kb_link.h), which turns it into a request to the core (or a
// volume message to the other host). Only in cable mode with USB power: on
// Bluetooth or 2.4 GHz the ESP32 is asleep and the keys do nothing.
#include QMK_KEYBOARD_H
#include "deskstrate_keys.h"
#include "kb_link.h"
#include "kb_link_proto.h"
#include "transport.h"

// Keychron's base layers (keymap.c's enum layers): the back switch picks one.
#define LAYER_MAC_BASE 0
#define LAYER_WIN_BASE 2

// The link's action for each KVM key, in deskstrate_keys.h order.
static const uint8_t kvm_actions[DESKSTRATE_KEYS_END - KVM_LEFT] = {
    [KVM_LEFT - KVM_LEFT]       = KB_ACT_LEFT,
    [KVM_RIGHT - KVM_LEFT]      = KB_ACT_RIGHT,
    [KVM_SPLIT - KVM_LEFT]      = KB_ACT_SPLIT,
    [KVM_SWAP - KVM_LEFT]       = KB_ACT_SWAP,
    [KVM_OTHER_VOLU - KVM_LEFT] = KB_ACT_OTHER_VOLU,
    [KVM_OTHER_VOLD - KVM_LEFT] = KB_ACT_OTHER_VOLD,
    [KVM_OTHER_MUTE - KVM_LEFT] = KB_ACT_OTHER_MUTE,
    [KVM_SLEEP - KVM_LEFT]      = KB_ACT_SLEEP,
};

// The desk's sleep (kvm_proto.h, asleep) is the core's: KVM_SLEEP asks it to sleep, and
// the lighting follows its STATE, off (not saved) while asleep, whoever put it to sleep.
// While asleep, any key press asks it to wake, then goes to the host as usual. Each ask
// sets the lighting straight away, and the STATE is only followed again once it agrees
// (or after EXPECT_MS), so the lighting doesn't flicker back while the core catches up.
#define EXPECT_MS 2000
static bool     rgb_off_for_sleep = false;
static int8_t   expect            = -1;  // asleep (1) or awake (0) just asked for; -1 none
static uint32_t expect_at;

static void sleep_lighting(bool off) {
    if (off == rgb_off_for_sleep) return;
    rgb_off_for_sleep = off;
    if (off) {
        rgb_matrix_disable_noeeprom();
    } else {
        rgb_matrix_enable_noeeprom();
    }
}

static void ask_desk(bool asleep) {
    expect    = asleep;
    expect_at = timer_read32();
    kb_link_send_kvm_action(asleep ? KB_ACT_SLEEP : KB_ACT_WAKE);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed && keycode != KVM_SLEEP && (expect == 1 || (expect < 0 && kb_link_desk_asleep()))) {
        // A lighting toggle as the waking press is left to turn the lighting on itself.
        if (keycode == QK_UNDERGLOW_TOGGLE || keycode == QK_RGB_MATRIX_TOGGLE) {
            rgb_off_for_sleep = false;
        } else {
            sleep_lighting(false);
        }
        ask_desk(false);
    }
    if (keycode >= KVM_LEFT && keycode < DESKSTRATE_KEYS_END) {
        if (record->event.pressed) {
            if (keycode == KVM_SLEEP) {
                sleep_lighting(true);
                ask_desk(true);
            } else {
                kb_link_send_kvm_action(kvm_actions[keycode - KVM_LEFT]);
            }
        }
        return false;
    }
    return true;
}

static void follow_desk(void) {
    const bool asleep = kb_link_desk_asleep();
    if (expect >= 0) {
        if (asleep != (expect == 1) && timer_elapsed32(expect_at) < EXPECT_MS) return;
        expect = -1;
    }
    sleep_lighting(asleep);
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
    follow_desk();

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
