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
#include "nvs.h"

namespace kvm_server {

namespace {

const char *TAG = "kvm";

int sock = -1;
uint32_t boot_epoch = 0;
uint32_t packet_n = 0;
uint32_t seq = 0;
kvm_state_t last = {};        // last STATE sent, for change detection
TickType_t last_sent = 0;

// KVM_COUNTER: (boot_epoch << 32) | n, boot_epoch bumped in NVS once per boot.
void load_boot_epoch() {
    nvs_handle_t nvs;
    if (nvs_open("kvm", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_get_u32(nvs, "epoch", &boot_epoch);
    boot_epoch++;
    nvs_set_u32(nvs, "epoch", boot_epoch);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void fill_hdr(kvm_hdr_t &hdr, uint8_t type, uint8_t flags) {
    hdr.magic = KVM_MAGIC;
    hdr.version = KVM_VERSION;
    hdr.type = type;
    hdr.sender = KVM_NODE_CORE;
    hdr.flags = flags;
    hdr.counter = ((uint64_t)boot_epoch << 32) | ++packet_n;
}

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

uint8_t result_code(esp_err_t err) {
    switch (err) {
        case ESP_OK: return KVM_RES_OK;
        case ESP_ERR_INVALID_ARG: return KVM_RES_UNKNOWN;
        case ESP_ERR_NOT_FINISHED: return KVM_RES_BUSY;
        default: return KVM_RES_REJECTED;  // not connected, or the write failed
    }
}

// Current monitors + hubs as STATE payload fields (header and seq not set).
kvm_state_t snapshot() {
    kvm_state_t st = {};
    st.writer = KVM_NODE_CORE;  // the core drives DDC itself
    st.preset = 0xFF;           // ad hoc: presets don't exist yet
    st.mon_count = 2;
    for (uint8_t id = 0; id < 2; id++) {
        const int i = monitor_index(id);
        const monitors::State m = i >= 0 ? monitors::get(i) : monitors::State{};
        if (!m.online) {
            st.mon[id] = KVM_IN_KEEP;
        } else {
            st.mon[id] = m.power == ddc::kPowerStandby ? KVM_IN_OFF : m.input;
        }
    }
    for (int h = 0; h < hubs::kCount; h++) {
        const hubs::State s = hubs::get(h);
        st.usb[h] = s.online ? s.port : KVM_USB_UNKNOWN;
    }
    return st;
}

bool same_payload(const kvm_state_t &a, const kvm_state_t &b) {
    return a.writer == b.writer && a.preset == b.preset && a.mon_count == b.mon_count &&
           memcmp(a.usb, b.usb, sizeof a.usb) == 0 && memcmp(a.mon, b.mon, sizeof a.mon) == 0;
}

void send_to(const void *buf, size_t len, const sockaddr_in &to) {
    sendto(sock, buf, len, 0, reinterpret_cast<const sockaddr *>(&to), sizeof to);
}

sockaddr_in broadcast_addr() {
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(KVM_PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    return to;
}

// Broadcasts STATE if anything changed (bumping seq), or as a heartbeat when due.
void publish_state(bool force) {
    kvm_state_t st = snapshot();
    const bool changed = !same_payload(st, last);
    const bool heartbeat = (xTaskGetTickCount() - last_sent) >= pdMS_TO_TICKS(KVM_HEARTBEAT_MS);
    if (!changed && !heartbeat && !force) return;
    if (changed) {
        seq++;
        ESP_LOGI(TAG, "STATE seq %lu: mon %02X %02X, usb %u %u", (unsigned long)seq, st.mon[0],
                 st.mon[1], st.usb[0], st.usb[1]);
    }
    fill_hdr(st.hdr, KVM_STATE, changed ? 0 : KVM_F_HEARTBEAT);
    st.seq = seq;
    send_to(&st, sizeof st, broadcast_addr());  // fails quietly until Wi-Fi is up
    last = st;
    last_sent = xTaskGetTickCount();
}

uint8_t apply_mon(uint8_t mon_id, uint8_t input) {
    if (mon_id >= monitors::kCount) return KVM_RES_UNKNOWN;
    const int i = monitor_index(mon_id);
    if (i < 0 || !monitors::get(i).online) return KVM_RES_REJECTED;  // not connected
    esp_err_t err;
    if (input == KVM_IN_OFF) {
        err = monitors::set_power(i, ddc::kPowerStandby);
    } else if (input == KVM_IN_DP || input == KVM_IN_HDMI || input == KVM_IN_USBC) {
        err = ESP_OK;
        if (monitors::get(i).power == ddc::kPowerStandby) err = monitors::set_power(i, ddc::kPowerOn);
        if (err == ESP_OK) err = monitors::set_input(i, input);
    } else {
        return KVM_RES_UNKNOWN;
    }
    return result_code(err);
}

uint8_t apply_usb(uint8_t hub, uint8_t port) {
    if (hub >= hubs::kCount || port > KVM_USB_GUEST2) return KVM_RES_UNKNOWN;
    return result_code(hubs::set_port(hub, port));
}

void handle_request(const kvm_request_t &req, const sockaddr_in &from) {
    uint8_t code;
    switch (req.action) {
        case KVM_ACT_MON: code = apply_mon(req.a, req.b); break;
        case KVM_ACT_USB: code = apply_usb(req.a, req.b); break;
        case KVM_ACT_GET_STATE: code = KVM_RES_OK; break;
        default: code = KVM_RES_UNKNOWN; break;
    }
    ESP_LOGI(TAG, "REQUEST %u from node 0x%02X (action %u a=%u b=%u): result %u",
             (unsigned)req.req_id, req.hdr.sender, req.action, req.a, req.b, code);

    if (code == KVM_RES_OK) publish_state(false);  // carry the change before the RESULT

    kvm_result_t res = {};
    fill_hdr(res.hdr, KVM_RESULT, 0);
    res.req_id = req.req_id;
    res.new_seq = code == KVM_RES_OK ? seq : 0;
    res.code = code;
    send_to(&res, sizeof res, from);

    // Every requester gets the current STATE straight back, so a client that has just
    // started (KVM_ACT_GET_STATE) needn't wait for the next heartbeat.
    // publish_state() first, so a change it hasn't broadcast yet gets its seq.
    publish_state(false);
    kvm_state_t st = last;
    fill_hdr(st.hdr, KVM_STATE, KVM_F_HEARTBEAT);
    send_to(&st, sizeof st, from);
}

void server_task(void *) {
    uint8_t buf[128];
    for (;;) {
        sockaddr_in from = {};
        socklen_t from_len = sizeof from;
        const int n = recvfrom(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr *>(&from), &from_len);
        if (n == (int)sizeof(kvm_request_t)) {
            kvm_request_t req;
            memcpy(&req, buf, sizeof req);  // decode by copy, never by casting the buffer
            if (req.hdr.magic == KVM_MAGIC && req.hdr.version == KVM_VERSION &&
                req.hdr.type == KVM_REQUEST && req.hdr.sender != KVM_NODE_CORE) {
                handle_request(req, from);
            }
        }
        // Other sizes are our own STATE broadcasts echoed back, or not for us.
        publish_state(false);
    }
}

}  // namespace

void start() {
    load_boot_epoch();

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
    ESP_LOGI(TAG, "UDP on port %u (boot epoch %lu, unauthenticated)", (unsigned)KVM_PORT,
             (unsigned long)boot_epoch);
}

}  // namespace kvm_server
