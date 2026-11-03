// kvm_proto UDP server: the core's side of the wire format in components/kvm_proto.
// Broadcasts the 4-byte STATE (monitors + hubs) on every change and every
// KVM_HEARTBEAT_MS, and carries out REQUESTs (the state wanted) from the deck and hosts,
// answering each with the current STATE. Unauthenticated: trusts the LAN.
#pragma once

namespace kvm_server {

// Starts the UDP task on KVM_PORT. Sends simply fail until Wi-Fi is up.
void start();

}  // namespace kvm_server
