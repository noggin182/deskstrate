#include "kvm_server.h"

#include <cerrno>
#include <cstring>

#include "ddc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hubs.h"
#include "kvm_proto.h"
#include "lwip/sockets.h"
#include "monitors.h"
#include "wol.h"

namespace kvm_server {

namespace {

const char *TAG = "kvm";

int sock = -1;
uint8_t last[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};  // last STATE sent
uint8_t last_levels[KVM_LEVELS][KVM_LEVEL_LEN];  // their volume fields
bool sent_once = false;
TickType_t last_sent = 0;

// Each host's volume as it last reported it ("---" if never, or not for
// KVM_LEVEL_EXPIRE_MS). The host owns the real value; the core only passes it on.
uint8_t levels[KVM_LEVELS][KVM_LEVEL_LEN] = {{KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN},
                                              {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN}};
TickType_t level_at[KVM_LEVELS] = {};

void expire_levels() {
    const TickType_t now = xTaskGetTickCount();
    for (int h = 0; h < KVM_LEVELS; h++) {
        if (levels[h][0] == KVM_UNKNOWN || now - level_at[h] < pdMS_TO_TICKS(KVM_LEVEL_EXPIRE_MS)) continue;
        ESP_LOGI(TAG, "%s volume expired", h == KVM_LEVEL_PC ? "PC" : "Mac");
        memset(levels[h], KVM_UNKNOWN, KVM_LEVEL_LEN);
    }
}

// The desk's sleep, reported as lower-case monitor bytes in STATE (kvm_proto.h). Kept in RTC memory so an OTA or a crash doesn't
// wake the desk; a power cycle starts awake.
constexpr uint32_t kAsleepMagic = 0x5A5EE9A5;
RTC_NOINIT_ATTR uint32_t asleep_magic;
RTC_NOINIT_ATTR bool asleep_flag;
bool asleep() { return asleep_magic == kAsleepMagic && asleep_flag; }
void set_asleep(bool on) {
    if (on != asleep()) ESP_LOGI(TAG, "desk %s", on ? "asleep" : "awake");
    asleep_flag = on;
    asleep_magic = kAsleepMagic;
}
bool woke = false;                      // the desk just woke: wake a guest's monitor once
TickType_t guest_power_at[monitors::kCount] = {};
constexpr uint32_t kGuestPowerRetryMs = 5000;

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
        const uint8_t standby = asleep() ? static_cast<uint8_t>(KVM_STANDBY) : 0;
        b[id] = static_cast<uint8_t>((m.online ? host_of_input(m.input) : KVM_MON_NC) | standby);
    }
    for (int h = 0; h < hubs::kCount; h++) {
        const hubs::State s = hubs::get(h);
        b[KVM_HUB_HID + h] = s.online ? kvm_hub_byte(s.port) : static_cast<uint8_t>(KVM_UNKNOWN);
    }
}

void send_state(const uint8_t b[KVM_SLOTS], const uint8_t lv[KVM_LEVELS][KVM_LEVEL_LEN], const sockaddr_in &to) {
    kvm_msg_t msg = kvm_msg(KVM_STATE, b);
    memcpy(msg.level, lv, sizeof msg.level);
    uint8_t raw[KVM_MAX_LEN];
    const unsigned n = kvm_encode(&msg, raw);
    sendto(sock, raw, n, 0, reinterpret_cast<const sockaddr *>(&to), sizeof to);
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
    expire_levels();
    const bool changed = !sent_once || memcmp(b, last, sizeof b) != 0 || memcmp(levels, last_levels, sizeof levels) != 0;
    const bool heartbeat = (xTaskGetTickCount() - last_sent) >= pdMS_TO_TICKS(KVM_HEARTBEAT_MS);
    if (!changed && !heartbeat) return;
    if (changed) {
        ESP_LOGI(TAG, "STATE %.4s %.3s %.3s", reinterpret_cast<const char *>(b),
                 reinterpret_cast<const char *>(levels[KVM_LEVEL_PC]), reinterpret_cast<const char *>(levels[KVM_LEVEL_MAC]));
    }
    send_state(b, levels, broadcast_addr());  // fails quietly until Wi-Fi is up
    memcpy(last, b, sizeof b);
    memcpy(last_levels, levels, sizeof levels);
    sent_once = true;
    last_sent = xTaskGetTickCount();
}

void apply_mon(uint8_t mon_id, uint8_t want) {
    const uint8_t host = kvm_mon_host(want);  // lower case (standby) is handle_request's
    if (host == KVM_HOST_KEEP) return;
    const uint8_t input = input_of_host(host);
    if (!input) {
        ESP_LOGW(TAG, "monitor %u: no input for host %c", mon_id, host);
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

// A monitor showing HDMI (a guest) has no daemon to follow the flag, and HDMI is the
// input the core's DDC reaches, so the core does it: standby while asleep (retried every
// kGuestPowerRetryMs), and on once when the desk wakes.
void guest_power() {
    const bool wake = woke;
    woke = false;
    for (int i = 0; i < monitors::kCount; i++) {
        const monitors::State m = monitors::get(i);
        if (!m.online || m.input != ddc::kInputHdmi) continue;
        uint8_t want = 0;
        if (asleep() && m.power != ddc::kPowerStandby) want = ddc::kPowerStandby;
        if (wake && m.power == ddc::kPowerStandby) want = ddc::kPowerOn;
        if (!want) continue;
        const TickType_t now = xTaskGetTickCount();
        if (!wake && guest_power_at[i] && now - guest_power_at[i] < pdMS_TO_TICKS(kGuestPowerRetryMs)) continue;
        guest_power_at[i] = now;
        ESP_LOGI(TAG, "DDC%d (HDMI): %s", i + 1, want == ddc::kPowerOn ? "on" : "standby");
        const esp_err_t err = monitors::set_power(i, want);
        if (err != ESP_OK) ESP_LOGW(TAG, "DDC%d: power failed: %s", i + 1, esp_err_to_name(err));
    }
}

void apply_hub(int hub, uint8_t want) {
    const uint8_t port = kvm_hub_port(want);
    if (port > 3) return;  // '-': leave alone (kvm_decode has dropped anything else)
    const hubs::State h = hubs::get(hub);
    if (h.online && h.port == port) return;  // already there
    const esp_err_t err = hubs::set_port(hub, port);
    if (err != ESP_OK) ESP_LOGW(TAG, "hub %d: switch failed: %s", hub, esp_err_to_name(err));
}

void handle_request(const kvm_msg_t &req, const sockaddr_in &from) {
    // A host reporting its volume: keep it, and restart its expiry.
    for (int h = 0; h < KVM_LEVELS; h++) {
        if (req.level[h][0] == KVM_UNKNOWN) continue;
        memcpy(levels[h], req.level[h], KVM_LEVEL_LEN);
        level_at[h] = xTaskGetTickCount();
    }
    const int sleep = kvm_request_sleep(req.b);  // -1: no slots (a volume report or a STATE query)
    uint8_t pc[KVM_SLOTS];  // what asks for the PC: the request, plus monitors already on it if this wakes the desk
    memcpy(pc, req.b, sizeof pc);
    if (sleep >= 0) {
        char ip[16];
        inet_ntoa_r(from.sin_addr, ip, sizeof ip);
        ESP_LOGI(TAG, "REQUEST %.4s from %s", reinterpret_cast<const char *>(req.b), ip);
        if (!sleep && asleep()) {
            woke = true;
            uint8_t now[KVM_SLOTS];
            snapshot(now);
            for (uint8_t id = KVM_MON_LEFT; id <= KVM_MON_RIGHT; id++) {
                if (kvm_mon_host(now[id]) == KVM_HOST_PC) pc[id] = KVM_HOST_PC;
            }
        }
        set_asleep(sleep);
        for (auto &t : guest_power_at) t = 0;  // act on the change straight away
    }
    if (sleep != 1) wol::wake_pc_if_wanted(pc);  // first, so the PC boots while the switches happen
    apply_mon(KVM_MON_LEFT, req.b[KVM_MON_LEFT]);
    apply_mon(KVM_MON_RIGHT, req.b[KVM_MON_RIGHT]);
    for (int h = 0; h < hubs::kCount; h++) apply_hub(h, req.b[KVM_HUB_HID + h]);

    guest_power();

    // Broadcast any change, then send the current STATE straight back so the requester
    // sees its answer (or that nothing changed) without waiting for a heartbeat.
    publish_state();
    send_state(last, last_levels, from);
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
        guest_power();
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
