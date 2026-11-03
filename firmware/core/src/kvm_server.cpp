#include "kvm_server.h"

#include <cerrno>
#include <cstring>

#include "ddc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hubs.h"
#include "kvm_proto.h"
#include "lwip/sockets.h"
#include "monitors.h"

namespace kvm_server {

namespace {

const char *TAG = "kvm";

int sock = -1;
uint8_t last[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};  // last STATE sent
bool sent_once = false;
TickType_t last_sent = 0;

// Which monitors:: index (DDC bus) is the left/right monitor, by EDID serial.
// Until a serial has been read, falls back to bus order, but never to a bus already
// identified as the other monitor: with one monitor cabled, the other side is absent
// rather than an alias for it. -1 if there is no candidate.
int monitor_index(uint8_t mon_id) {
    if (mon_id >= monitors::kCount) return -1;
    const char *want = mon_id == KVM_MON_LEFT ? "left" : "right";
    int unidentified = -1;
    for (int i = 0; i < monitors::kCount; i++) {
        const char *s = monitors::side(monitors::get(i).serial);
        if (s && strcmp(s, want) == 0) return i;
        if (!s && (unidentified < 0 || i == mon_id)) unidentified = i;
    }
    return unidentified;
}

// The Dell's input (VCP 0x60) for a host and back. The wire never carries VCP values.
uint8_t host_of_input(uint8_t input) {
    switch (input) {
        case ddc::kInputDp: return KVM_HOST_PC;
        case ddc::kInputUsbc: return KVM_HOST_MAC;
        case ddc::kInputHdmi: return KVM_HOST_GUEST;
    }
    return KVM_HOST_KEEP;  // unknown
}

uint8_t input_of_host(uint8_t host) {
    switch (host) {
        case KVM_HOST_PC: return ddc::kInputDp;
        case KVM_HOST_MAC: return ddc::kInputUsbc;
        case KVM_HOST_GUEST: return ddc::kInputHdmi;
    }
    return 0;
}

// Current monitors + hubs as STATE bytes.
void snapshot(uint8_t b[KVM_SLOTS]) {
    for (uint8_t id = KVM_MON_LEFT; id <= KVM_MON_RIGHT; id++) {
        const int i = monitor_index(id);
        const monitors::State m = i >= 0 ? monitors::get(i) : monitors::State{};
        b[id] = m.online ? host_of_input(m.input) | (m.power == ddc::kPowerStandby ? KVM_STANDBY : 0) : KVM_UNKNOWN;
    }
    for (int h = 0; h < hubs::kCount; h++) {
        const hubs::State s = hubs::get(h);
        b[KVM_HUB_HID + h] = s.online ? s.port : KVM_UNKNOWN;
    }
}

void send_state(const uint8_t b[KVM_SLOTS], const sockaddr_in &to) {
    const kvm_msg_t msg = kvm_msg(KVM_STATE, b);
    sendto(sock, &msg, sizeof msg, 0, reinterpret_cast<const sockaddr *>(&to), sizeof to);
}

sockaddr_in broadcast_addr() {
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(KVM_PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    return to;
}

// Broadcasts STATE if anything changed, or as a heartbeat when due.
void publish_state() {
    uint8_t b[KVM_SLOTS];
    snapshot(b);
    const bool changed = !sent_once || memcmp(b, last, sizeof b) != 0;
    const bool heartbeat = (xTaskGetTickCount() - last_sent) >= pdMS_TO_TICKS(KVM_HEARTBEAT_MS);
    if (!changed && !heartbeat) return;
    if (changed) ESP_LOGI(TAG, "STATE %02X %02X %02X %02X", b[0], b[1], b[2], b[3]);
    send_state(b, broadcast_addr());  // fails quietly until Wi-Fi is up
    memcpy(last, b, sizeof b);
    sent_once = true;
    last_sent = xTaskGetTickCount();
}

void apply_mon(uint8_t mon_id, uint8_t want) {
    const uint8_t host = want & KVM_HOST_MASK;  // bit 7 (standby): power isn't changed yet
    if (want == KVM_UNKNOWN || host == KVM_HOST_KEEP) return;
    const uint8_t input = input_of_host(host);
    if (!input) {
        ESP_LOGW(TAG, "monitor %u: no input for host %u", mon_id, host);
        return;
    }
    const int i = monitor_index(mon_id);
    if (i < 0 || !monitors::get(i).online) return;  // not connected
    const monitors::State m = monitors::get(i);
    if (m.input == input && m.power != ddc::kPowerStandby) return;  // already there
    // A monitor in standby is woken to switch it; the switch is what was asked for.
    esp_err_t err = m.power == ddc::kPowerStandby ? monitors::set_power(i, ddc::kPowerOn) : ESP_OK;
    if (err == ESP_OK) err = monitors::set_input(i, input);
    if (err != ESP_OK) ESP_LOGW(TAG, "monitor %u: switch failed: %s", mon_id, esp_err_to_name(err));
}

void apply_hub(int hub, uint8_t port) {
    if (port == KVM_UNKNOWN) return;
    if (port > 3) {
        ESP_LOGW(TAG, "hub %d: no port %u", hub, port);
        return;
    }
    const hubs::State h = hubs::get(hub);
    if (h.online && h.port == port) return;  // already there
    const esp_err_t err = hubs::set_port(hub, port);
    if (err != ESP_OK) ESP_LOGW(TAG, "hub %d: switch failed: %s", hub, esp_err_to_name(err));
}

void handle_request(const kvm_msg_t &req, const sockaddr_in &from) {
    char ip[16];
    inet_ntoa_r(from.sin_addr, ip, sizeof ip);
    ESP_LOGI(TAG, "REQUEST %02X %02X %02X %02X from %s", req.b[0], req.b[1], req.b[2], req.b[3], ip);
    apply_mon(KVM_MON_LEFT, req.b[KVM_MON_LEFT]);
    apply_mon(KVM_MON_RIGHT, req.b[KVM_MON_RIGHT]);
    for (int h = 0; h < hubs::kCount; h++) apply_hub(h, req.b[KVM_HUB_HID + h]);

    // Broadcast any change, then send the current STATE straight back so the requester
    // sees its answer (or that nothing changed) without waiting for a heartbeat.
    publish_state();
    send_state(last, from);
}

void server_task(void *) {
    uint8_t buf[64];
    for (;;) {
        sockaddr_in from = {};
        socklen_t from_len = sizeof from;
        const int n = recvfrom(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr *>(&from), &from_len);
        kvm_msg_t req;
        if (n > 0 && kvm_decode(buf, n, KVM_REQUEST, &req)) handle_request(req, from);
        // Anything else is our own STATE broadcast echoed back, or not for us.
        publish_state();
    }
}

}  // namespace

void start() {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket: errno %d", errno);
        return;
    }
    const int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    timeval tv = {.tv_sec = 0, .tv_usec = 100 * 1000};  // wake up for change checks
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(KVM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        ESP_LOGE(TAG, "bind %u: errno %d", (unsigned)KVM_PORT, errno);
        close(sock);
        sock = -1;
        return;
    }

    xTaskCreate(server_task, "kvm", 4096, nullptr, 4, nullptr);
    ESP_LOGI(TAG, "UDP on port %u (unauthenticated)", (unsigned)KVM_PORT);
}

}  // namespace kvm_server
