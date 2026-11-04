// radio_test: ESP-NOW range test for the ESP32 that goes inside the keyboard.
//
// sender:   broadcasts a numbered packet every 100 ms at full TX power. No Wi-Fi
//           association, like the keyboard will run.
// receiver: once a second prints how many packets arrived, how many were missed
//           (from gaps in the numbering) and their RSSI, plus a running total:
//
//   rx 10/10  lost 0  rssi -58 (min -61 max -55)  total 1200/1203 (0.2% lost)
//
// Rough guide: RSSI above about -80 dBm with under 5% lost means the keyboard's
// occasional request will get through. A request is resent until the core's STATE
// shows it, so the odd loss only costs a resend.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#ifndef RADIO_TEST_CHANNEL
#define RADIO_TEST_CHANNEL 1
#endif
#ifndef RADIO_TEST_LR
#define RADIO_TEST_LR 0
#endif

namespace {

constexpr uint8_t kBroadcast[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint32_t kIntervalMs = 100;

struct __attribute__((packed)) Packet {
    char magic[2];  // "RT"
    uint32_t seq;
};

void radio_init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
#if RADIO_TEST_LR
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_LR));
#endif
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_channel(RADIO_TEST_CHANNEL, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(80));  // 20 dBm, the S3's maximum
    ESP_ERROR_CHECK(esp_now_init());

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    printf("radio_test %s  channel %d%s  mac %02x:%02x:%02x:%02x:%02x:%02x\n",
           RADIO_TEST_SENDER ? "sender" : "receiver", RADIO_TEST_CHANNEL,
           RADIO_TEST_LR ? "  long-range" : "", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---------------------------------------------------------------- sender

[[noreturn]] void run_sender() {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcast, sizeof kBroadcast);
    peer.channel = RADIO_TEST_CHANNEL;
    peer.ifidx = WIFI_IF_STA;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    Packet pkt = {{'R', 'T'}, 0};
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        pkt.seq++;
        esp_err_t err = esp_now_send(kBroadcast, reinterpret_cast<const uint8_t *>(&pkt), sizeof pkt);
        if (err != ESP_OK) printf("send %lu failed: %s\n", (unsigned long)pkt.seq, esp_err_to_name(err));
        if (pkt.seq % 50 == 0) printf("sent %lu\n", (unsigned long)pkt.seq);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kIntervalMs));
    }
}

// ---------------------------------------------------------------- receiver

struct Stats {
    uint32_t rx, lost;
    int rssi_sum, rssi_min, rssi_max;
};

portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
Stats g_window = {};
uint32_t g_last_seq = 0;
bool g_have_seq = false;

void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len != sizeof(Packet) || data[0] != 'R' || data[1] != 'T') return;
    Packet pkt;
    memcpy(&pkt, data, sizeof pkt);
    int rssi = info->rx_ctrl->rssi;

    taskENTER_CRITICAL(&g_lock);
    // A lower number means the sender rebooted: just carry on counting from it.
    if (g_have_seq && pkt.seq > g_last_seq) g_window.lost += pkt.seq - g_last_seq - 1;
    g_last_seq = pkt.seq;
    g_have_seq = true;
    g_window.rx++;
    g_window.rssi_sum += rssi;
    if (g_window.rx == 1 || rssi < g_window.rssi_min) g_window.rssi_min = rssi;
    if (g_window.rx == 1 || rssi > g_window.rssi_max) g_window.rssi_max = rssi;
    taskEXIT_CRITICAL(&g_lock);
}

[[noreturn]] void run_receiver() {
    ESP_ERROR_CHECK(esp_now_register_recv_cb(on_recv));

    uint64_t total_rx = 0, total_lost = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000));
        taskENTER_CRITICAL(&g_lock);
        Stats s = g_window;
        g_window = {};
        taskEXIT_CRITICAL(&g_lock);

        total_rx += s.rx;
        total_lost += s.lost;
        uint64_t total = total_rx + total_lost;
        double pct = total ? 100.0 * total_lost / total : 0;
        if (s.rx == 0) {
            printf("rx 0  (nothing heard)  total %llu/%llu (%.1f%% lost)\n",
                   total_rx, total, pct);
        } else {
            printf("rx %2lu/%2lu  lost %lu  rssi %d (min %d max %d)  total %llu/%llu (%.1f%% lost)\n",
                   (unsigned long)s.rx, (unsigned long)(s.rx + s.lost), (unsigned long)s.lost,
                   s.rssi_sum / (int)s.rx, s.rssi_min, s.rssi_max, total_rx, total, pct);
        }
    }
}

}  // namespace

extern "C" void app_main() {
    radio_init();
#if RADIO_TEST_SENDER
    run_sender();
#else
    run_receiver();
#endif
}
