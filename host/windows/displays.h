// The display actions, done on whichever desktop is showing: the user's, or the secure
// desktop (sign-in and lock screen) when this runs as SYSTEM in the console session.
// Each blocks until done. Must run in the console session (not the service's session 0).
#pragma once

#include <cstdint>

namespace displays {

void sleep();                    // DDC standby (VCP D6 = 4) on the monitors showing this PC
void wake();                     // a 1 px mouse nudge (SC_MONITORPOWER -1 is unreliable), then DDC on
bool set_topology(uint8_t topo); // HOST_TOPO_EXTEND / HOST_TOPO_MIRROR (Duplicate); true if applied

// Presses the media volume key for enum kvm_volume (up / down / mute), as the keyboard's
// knob would: Windows' own step and on-screen indicator.
void volume(uint8_t kvm_volume);

}  // namespace displays
