// kvm_proto UDP server: the core's side of the wire format in components/kvm_proto.
// Broadcasts STATE (monitors + hubs) on every change and every KVM_HEARTBEAT_MS, and
// answers REQUESTs from the deck and hosts with a RESULT.
//
// Not implemented yet: HMAC tags (sent as zeros, not checked) and per-sender counter
// checks. Fine on a trusted LAN for bench testing only.
#pragma once

namespace kvm_server {

// Starts the UDP task on KVM_PORT. Call after wifi_sta_start (needs NVS for the
// boot epoch); sends simply fail until Wi-Fi is up.
void start();

}  // namespace kvm_server
