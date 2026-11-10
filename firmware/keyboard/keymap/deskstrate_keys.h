// Deskstrate KVM keys. They take the custom keycode slots after Keychron's own
// (QK_KB_16 onwards on the Q3 Max), and via/make_via_json.py lists them in the same
// order so VIA shows them by name. Keep the two in step.
#pragma once

#include "keychron_common.h"

// Each one sends a KR_KVM_ACTION (kb_link_proto.h) to the ESP, which works out the
// request from the core's state, as the deck does for its keys.
enum deskstrate_keycodes {
    KVM_LEFT = NEW_SAFE_RANGE,  // all to PC; in split, keyboard/mouse to the left screen
    KVM_RIGHT,                  // all to Mac; in split, keyboard/mouse to the right screen
    KVM_SPLIT,                  // enter / leave split
    KVM_SWAP,                   // swap the monitors (media hub stays with its screen)
    KVM_OTHER_VOLU,             // volume up on the host that doesn't have the keyboard
    KVM_OTHER_VOLD,             // volume down, same host
    KVM_OTHER_MUTE,             // mute toggle, same host
    KVM_SLEEP,                  // put the desk to sleep (monitors to standby, lighting off)
    DESKSTRATE_KEYS_END,
};

_Static_assert((int)KVM_LEFT == (int)QK_KB_16, "via/make_via_json.py assumes our keys start at QK_KB_16");
_Static_assert((int)DESKSTRATE_KEYS_END <= (int)QK_KB_31 + 1, "out of custom keycode slots");
