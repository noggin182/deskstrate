#include "udp.h"

#include <cerrno>
#include <cstring>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kvm_proto.h"
#include "lwip/sockets.h"
#include "serial.h"

namespace udp {

namespace {

const char *TAG = "udp";
int sock = -1;

uint32_t own_ip() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info = {};
    if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK) return 0;
    return info.ip.addr;
}

void task(void *) {
    uint8_t buf[serial::kMaxLine + 1];  // one spare byte: a longer datagram is truncated, so dropped
    for (;;) {
        sockaddr_in from = {};
        socklen_t from_len = sizeof from;
        const int n = recvfrom(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr *>(&from), &from_len);
        if (n <= 0) {
            if (n < 0) vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (from.sin_addr.s_addr == own_ip()) continue;  // our own broadcast
        if (static_cast<size_t>(n) > serial::kMaxLine || memchr(buf, '\r', n) || memchr(buf, '\n', n)) {
            ESP_LOGW(TAG, "dropped a %d-byte datagram that can't be one line", n);
            continue;
        }
        serial::write_line(buf, n);
    }
}

}  // namespace

esp_err_t start() {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket: errno %d", errno);
        return ESP_FAIL;
    }
    const int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof on);
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
    if (xTaskCreate(task, "udp_rx", 3072, nullptr, 6, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "passing UDP %u to and from the Mac", (unsigned)KVM_PORT);
    return ESP_OK;
}

void send_line(const uint8_t *line, size_t len) {
    if (sock < 0) return;
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(KVM_PORT);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(sock, line, len, 0, reinterpret_cast<sockaddr *>(&to), sizeof to);
}

}  // namespace udp
