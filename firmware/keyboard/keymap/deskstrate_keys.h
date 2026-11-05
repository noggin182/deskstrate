// Deskstrate KVM keys. They take the custom keycode slots after Keychron's own
// (QK_KB_16 onwards on the Q3 Max), and via/make_via_json.py lists them in the same
// order so VIA shows them by name. Keep the two in step.
#pragma once

#include "keychron_common.h"

enum deskstrate_keycodes {
    KVM_ALL_PC = NEW_SAFE_RANGE,
    KVM_ALL_MAC,
    KVM_SPLIT_PC_MAC,
    KVM_SPLIT_MAC_PC,
    KVM_L_PC,
    KVM_L_MAC,
    KVM_L_GUEST,
    KVM_R_PC,
    KVM_R_MAC,
    KVM_R_GUEST,
    KVM_HID_PC,
    KVM_HID_MAC,
    KVM_MEDIA_PC,
    KVM_MEDIA_MAC,
    DESKSTRATE_KEYS_END,
};

_Static_assert((int)KVM_ALL_PC == (int)QK_KB_16, "via/make_via_json.py assumes our keys start at QK_KB_16");
_Static_assert((int)DESKSTRATE_KEYS_END <= (int)QK_KB_31 + 1, "out of custom keycode slots");
