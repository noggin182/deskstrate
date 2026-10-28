// The display actions, done on whichever desktop is showing: the user's, or the secure
// desktop (sign-in and lock screen) when this runs as SYSTEM in the console session.
// Each blocks until done. Must run in the console session (not the service's session 0).
#pragma once

#include <cstdint>

namespace displays {

void sleep();                    // SC_MONITORPOWER 2
void wake();                     // a 1 px mouse nudge (SC_MONITORPOWER -1 is unreliable)
bool set_topology(uint8_t topo); // KVM_TOPO_EXTEND / KVM_TOPO_MIRROR (Duplicate); true if applied

}  // namespace displays
