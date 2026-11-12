// Wake-on-LAN for the PC: a magic packet when the PC is wanted (a monitor or hub switched to
// it, or the desk woken with a monitor on it). Mac: none, it isn't on the LAN's side of its VPN.
// The PC's MAC is set in menuconfig (Deskstrate core) and kept in NVS; with none stored,
// nothing is sent. An awake PC ignores the packet, so it is sent whether or not it's needed.
#pragma once

#include <cstdint>

#include "kvm_proto.h"

namespace wol {

// Loads the PC's MAC: this build's menuconfig value (stored to NVS), or else the stored one.
void start();

// Sends the PC a magic packet if any slot of want is KVM_HOST_PC (upper case: a lower-case
// monitor byte is a sleep request). want is REQUEST bytes, so the deck, keyboard, daemons and
// the web UI all come through here.
void wake_pc_if_wanted(const uint8_t want[KVM_SLOTS]);

}  // namespace wol
