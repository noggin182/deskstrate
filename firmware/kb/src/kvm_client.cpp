#include "kvm_client.h"

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "kb_link.h"
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
    sendto(sock, &msg, sizeof msg, 0, reinterpret_cast<sockaddr *>(&to), sizeof to);
    sent_at = xTaskGetTickCount();
    sends++;
}

// True once every byte asked for shows in the state (monitors: the host only).
bool shows(const uint8_t s[KVM_SLOTS], const uint8_t want[KVM_SLOTS]) {
    for (uint8_t i = 0; i < KVM_SLOTS; i++) {
        if (want[i] == KVM_UNKNOWN) continue;
        const bool mon = i == KVM_MON_LEFT || i == KVM_MON_RIGHT;
        if (mon ? kvm_mon_host(s[i]) != kvm_mon_host(want[i]) : s[i] != want[i]) return false;
    }
    return true;
}

// Call with mutex held.
void check_pending_locked() {
    if (!pending) return;
    if (have_state && shows(latest, wanted)) {
        ESP_LOGI(TAG, "request done after %u send(s)", sends);
        pending = false;
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if (now - asked_at >= pdMS_TO_TICKS(kGiveUpMs)) {
        ESP_LOGW(TAG, "request %02X %02X %02X %02X not shown after %u sends", wanted[0], wanted[1], wanted[2],
                 wanted[3], sends);
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

void request(const uint8_t b[4]) {
    if (!mutex || sock < 0) return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    memcpy(wanted, b, KVM_SLOTS);
    pending = true;
    asked_at = xTaskGetTickCount();
    sends = 0;
    send_locked();
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
