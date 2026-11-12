#include "wol.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace wol {

namespace {

const char *TAG = "wol";
constexpr uint16_t kPort = 9;  // discard: the usual WoL port; the NIC matches the payload, not the port
constexpr int kRepeats = 2;    // Wi-Fi broadcasts get lost now and then

uint8_t mac[6] = {};
bool have_mac = false;

bool parse_mac(const char *s, uint8_t out[6]) {
    unsigned v[6];
    char end;
    if (sscanf(s, "%2x%*[:-]%2x%*[:-]%2x%*[:-]%2x%*[:-]%2x%*[:-]%2x%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &end) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) out[i] = static_cast<uint8_t>(v[i]);
    return true;
}

void send_magic() {
    uint8_t pkt[6 + 16 * 6];
    memset(pkt, 0xFF, 6);
    for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, mac, 6);

    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);  // per call: the kvm and httpd tasks both send
    if (s < 0) {
        ESP_LOGW(TAG, "socket: errno %d", errno);
        return;
    }
    const int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(kPort);
    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    int sent = 0;
    for (int i = 0; i < kRepeats; i++) {
        if (sendto(s, pkt, sizeof pkt, 0, reinterpret_cast<const sockaddr *>(&to), sizeof to) == sizeof pkt) sent++;
    }
    close(s);
    ESP_LOGI(TAG, "magic packet to PC %02x:%02x:%02x:%02x:%02x:%02x (%d/%d sent)", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5], sent, kRepeats);
}

}  // namespace

void start() {
    nvs_handle_t nvs;
    if (nvs_open("wol", NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGE(TAG, "NVS unavailable: Wake-on-LAN off");
        return;
    }
    const char *built_in = CONFIG_DESKSTRATE_PC_MAC;
    if (strcmp(built_in, "off") == 0) {
        nvs_erase_key(nvs, "pc_mac");
        nvs_commit(nvs);
        ESP_LOGI(TAG, "PC MAC cleared by this build");
    } else if (built_in[0]) {
        if (parse_mac(built_in, mac)) {
            have_mac = true;
            if (nvs_set_blob(nvs, "pc_mac", mac, sizeof mac) == ESP_OK) nvs_commit(nvs);
        } else {
            ESP_LOGW(TAG, "menuconfig PC MAC \"%s\" isn't a MAC: ignored", built_in);
        }
    }
    if (!have_mac) {
        size_t len = sizeof mac;
        have_mac = nvs_get_blob(nvs, "pc_mac", mac, &len) == ESP_OK && len == sizeof mac;
    }
    nvs_close(nvs);
    if (have_mac) {
        ESP_LOGI(TAG, "PC %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        ESP_LOGI(TAG, "no PC MAC stored: Wake-on-LAN off");
    }
}

void wake_pc_if_wanted(const uint8_t want[KVM_SLOTS]) {
    if (!have_mac) return;
    for (int i = 0; i < KVM_SLOTS; i++) {
        if (want[i] == KVM_HOST_PC) {
            send_magic();
            return;
        }
    }
}

}  // namespace wol
