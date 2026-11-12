#include "kvm_client.h"

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "kb_link.h"
#include "kb_link_proto.h"
#include "kvm_controls.h"
#include "kvm_proto.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace kvm_client {

namespace {

const char *TAG = "kvm_client";
constexpr uint32_t kResendMs = 300;
constexpr uint32_t kGiveUpMs = 3000;

SemaphoreHandle_t mutex = nullptr;  // guards everything below
int sock = -1;
sockaddr_in core_addr = {};
bool core_known = false;
uint8_t latest[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};
bool have_state = false;
uint8_t wanted[KVM_SLOTS] = {};
bool pending = false;
TickType_t asked_at = 0, sent_at = 0;
uint8_t sends = 0;

// Call with mutex held. Broadcast until the core has been heard.
void send_locked() {
    sockaddr_in to = core_addr;
    if (!core_known) {
        to = {};
        to.sin_family = AF_INET;
        to.sin_port = htons(KVM_PORT);
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }
    const kvm_msg_t msg = kvm_msg(KVM_REQUEST, wanted);
    uint8_t raw[KVM_MAX_LEN];
    const unsigned len = kvm_encode(&msg, raw);
    sendto(sock, raw, len, 0, reinterpret_cast<sockaddr *>(&to), sizeof to);
    sent_at = xTaskGetTickCount();
    sends++;
}

// Call with mutex held. Broadcast, never to the core: only the host daemon named in it
// acts on it, and the Mac (on a VPN) can only hear broadcasts.
void send_volume_locked(uint8_t host, uint8_t vol) {
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(KVM_PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    const kvm_msg_t msg = kvm_volume_msg(host, vol);
    uint8_t raw[KVM_MAX_LEN];
    const unsigned len = kvm_encode(&msg, raw);
    sendto(sock, raw, len, 0, reinterpret_cast<sockaddr *>(&to), sizeof to);
}

// Call with mutex held.
void request_locked(const uint8_t b[KVM_SLOTS]) {
    memcpy(wanted, b, KVM_SLOTS);
    pending = true;
    asked_at = xTaskGetTickCount();
    sends = 0;
    send_locked();
}

// Call with mutex held.
void check_pending_locked() {
    if (!pending) return;
    if (have_state && kvm_shows(latest, wanted)) {
        ESP_LOGI(TAG, "request done after %u send(s)", sends);
        pending = false;
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if (now - asked_at >= pdMS_TO_TICKS(kGiveUpMs)) {
        ESP_LOGW(TAG, "request %.4s not shown after %u sends", reinterpret_cast<const char *>(wanted), sends);
        pending = false;
    } else if (now - sent_at >= pdMS_TO_TICKS(kResendMs)) {
        send_locked();
    }
}

void task(void *) {
    uint8_t buf[64];
    for (;;) {
        sockaddr_in from = {};
        socklen_t from_len = sizeof from;
        const int n = recvfrom(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr *>(&from), &from_len);
        kvm_msg_t msg;
        const bool is_state = n > 0 && kvm_decode(buf, n, KVM_STATE, &msg);
        bool changed = false;
        uint8_t copy[KVM_SLOTS];
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (is_state) {
            changed = !have_state || memcmp(latest, msg.b, KVM_SLOTS) != 0;
            memcpy(latest, msg.b, KVM_SLOTS);
            have_state = true;
            if (!core_known || core_addr.sin_addr.s_addr != from.sin_addr.s_addr) {
                ESP_LOGI(TAG, "core found at %s", inet_ntoa(from.sin_addr));
            }
            core_addr = from;
            core_known = true;
        }
        check_pending_locked();
        memcpy(copy, latest, KVM_SLOTS);
        xSemaphoreGive(mutex);
        if (changed) kb_link::send_kvm_state(copy);
    }
}

}  // namespace

esp_err_t start() {
    mutex = xSemaphoreCreateMutex();
    if (!mutex) return ESP_ERR_NO_MEM;
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket: errno %d", errno);
        return ESP_FAIL;
    }
    const int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
    timeval tv = {.tv_sec = 0, .tv_usec = 100 * 1000};  // wake to resend while waiting
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(KVM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        ESP_LOGE(TAG, "bind %u: errno %d", (unsigned)KVM_PORT, errno);
        close(sock);
        sock = -1;
        return ESP_FAIL;
    }
    if (xTaskCreate(task, "kvm_client", 4096, nullptr, 5, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "kvm_proto client on port %u", (unsigned)KVM_PORT);
    return ESP_OK;
}

void action(uint8_t act) {
    if (!mutex || sock < 0) return;
    using namespace kvm_controls;
    xSemaphoreTake(mutex, portMAX_DELAY);
    // Plan against what the state will be once a request still in flight lands, so two
    // quick presses (Fn+Up twice) build on each other.
    View view;
    if (have_state) memcpy(view.b, latest, KVM_SLOTS);
    if (pending) {
        for (uint8_t i = 0; i < KVM_SLOTS; i++) {
            const bool mon = i == KVM_MON_LEFT || i == KVM_MON_RIGHT;
            if (wanted[i] == KVM_UNKNOWN || (mon && kvm_mon_host(wanted[i]) == KVM_HOST_KEEP)) continue;
            view.b[i] = wanted[i];
        }
    }
    Target t;
    uint8_t vol = 0;
    switch (act) {
        case KB_ACT_LEFT: t = plan_side(view, true); break;
        case KB_ACT_RIGHT: t = plan_side(view, false); break;
        case KB_ACT_SPLIT: t = plan_toggle_split(view); break;
        case KB_ACT_SWAP: t = plan_swap_media(view); break;
        case KB_ACT_OTHER_VOLU: vol = KVM_VOL_UP; break;
        case KB_ACT_OTHER_VOLD: vol = KVM_VOL_DOWN; break;
        case KB_ACT_OTHER_MUTE: vol = KVM_VOL_MUTE; break;
        case KB_ACT_SLEEP: {
            const uint8_t req[KVM_SLOTS] = KVM_SLEEP_REQUEST;
            memcpy(t.b, req, KVM_SLOTS);
            break;
        }
        case KB_ACT_WAKE: {
            const uint8_t req[KVM_SLOTS] = KVM_WAKE_REQUEST;
            memcpy(t.b, req, KVM_SLOTS);
            break;
        }
        default: ESP_LOGW(TAG, "unknown action %u", act); break;
    }
    if (vol) {
        const uint8_t host = other_host(view);
        if (host != kNoHost) {
            ESP_LOGI(TAG, "volume %c to %s", vol, host == kPc ? "PC" : "Mac");
            send_volume_locked(host, vol);
        } else {
            ESP_LOGI(TAG, "volume: keyboard isn't on the PC or Mac");
        }
    } else if (!is_empty(t)) {
        ESP_LOGI(TAG, "action %u: request %.4s", act, reinterpret_cast<const char *>(t.b));
        request_locked(t.b);
    } else {
        ESP_LOGI(TAG, "action %u: nothing to do", act);
    }
    xSemaphoreGive(mutex);
}

bool state(uint8_t out[4]) {
    if (!mutex) return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    memcpy(out, latest, KVM_SLOTS);
    const bool ok = have_state;
    xSemaphoreGive(mutex);
    return ok;
}

}  // namespace kvm_client
