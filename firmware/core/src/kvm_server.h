// kvm_proto UDP server: the core's side of the wire format in components/kvm_proto.
// Broadcasts STATE (monitors, hubs and the hosts' reported volumes) on every change and
// every KVM_HEARTBEAT_MS, and carries out REQUESTs (the state wanted, or a host's volume
// report) from the deck, keyboard and hosts, answering each with the current STATE. Unauthenticated: trusts the LAN.
#pragma once

namespace kvm_server {

// Starts the UDP task on KVM_PORT. Sends simply fail until Wi-Fi is up.
void start();

}  // namespace kvm_server
