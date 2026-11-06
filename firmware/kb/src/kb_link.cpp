#include "kb_link.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "kb_link_proto.h"
#include "kvm_client.h"
#include "sdkconfig.h"

namespace kb_link {

namespace {

const char *TAG = "kb_link";
constexpr uart_port_t kUart = UART_NUM_1;
constexpr gpio_num_t kRx = static_cast<gpio_num_t>(CONFIG_DESKSTRATE_KB_RX_GPIO);
constexpr gpio_num_t kTx = static_cast<gpio_num_t>(CONFIG_DESKSTRATE_KB_TX_GPIO);
constexpr size_t kTextMax = KB_LINK_MAX_PAYLOAD + 1;
constexpr size_t kRecent = 16;

struct Text {
    int64_t at_us;
    char text[kTextMax];
};

SemaphoreHandle_t lock = nullptr;  // guards everything below, and UART writes
Text recent[kRecent];
size_t recent_next = 0, recent_count = 0;
char hello[kTextMax] = "";
kb_link_status status = {};
int64_t status_at_us = 0;  // 0: none yet
kb_link_rx rx = {};
uint8_t tx_seq = 0;
int last_kvm_seq = -1;
uint32_t acks = 0, kvm_requests = 0;
std::atomic<bool> started{false};

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// Call with lock held.
void keep_locked(const char *text) {
    Text &t = recent[recent_next];
    t.at_us = esp_timer_get_time();
    snprintf(t.text, sizeof t.text, "%s", text);
    recent_next = (recent_next + 1) % kRecent;
    if (recent_count < kRecent) recent_count++;
}

// Call with lock held.
void send_locked(uint8_t type, const void *payload, uint8_t len, uint8_t seq) {
    uint8_t frame[KB_LINK_MAX_FRAME];
    const uint8_t n = kb_link_encode(frame, type, seq, payload, len);
    if (n) uart_write_bytes(kUart, frame, n);
}

// Payload as a C string, printable ASCII only.
void payload_text(char *out, const uint8_t *p, uint8_t len) {
    uint8_t i = 0;
    for (; i < len && i < kTextMax - 1; i++) out[i] = p[i] >= 0x20 && p[i] < 0x7F ? static_cast<char>(p[i]) : '?';
    out[i] = 0;
}

// Call with lock held: one good frame from the keyboard.
void handle_locked() {
    const uint8_t type = KB_LINK_TYPE(&rx), seq = KB_LINK_SEQ(&rx), len = KB_LINK_LEN(&rx);
    const uint8_t *p = KB_LINK_PAYLOAD(&rx);
    char text[kTextMax];
    switch (type) {
        case KR_HELLO:
            payload_text(hello, len ? p + 1 : p, len ? len - 1 : 0);
            ESP_LOGI(TAG, "keyboard says hello (link v%u): %s", len ? p[0] : 0, hello);
            keep_locked(hello);
            last_kvm_seq = -1;  // a restarted keyboard counts its seq from 0 again
            break;
        case KR_KVM_REQUEST:
            if (len != 4) break;
            send_locked(EK_ACK, &seq, 1, tx_seq++);
            acks++;
            if (seq == last_kvm_seq) break;  // a resend whose ack was lost: done already
            last_kvm_seq = seq;
            kvm_requests++;
            ESP_LOGI(TAG, "KVM request %02X %02X %02X %02X", p[0], p[1], p[2], p[3]);
            kvm_client::request(p);
            break;
        case KR_STATUS:
            if (len != sizeof status) break;
            memcpy(&status, p, sizeof status);
            status_at_us = esp_timer_get_time();
            break;
        case KR_TEXT:
            payload_text(text, p, len);
            ESP_LOGI(TAG, "kb: %s", text);
            keep_locked(text);
            break;
        default:
            break;
    }
}

void rx_task(void *) {
    uint8_t buf[64];
    bool was_on_usb = false;
    for (;;) {
        // Wait up to 100 ms for the first byte, then take whatever else has arrived.
        // (Asking for a full buffer would wait the whole 100 ms for a short frame, and
        // the keyboard resends a KVM request every 50 ms until it sees the ack.)
        int got = uart_read_bytes(kUart, buf, 1, pdMS_TO_TICKS(100));
        if (got == 1) {
            size_t more = 0;
            uart_get_buffered_data_len(kUart, &more);
            if (more > sizeof buf - 1) more = sizeof buf - 1;
            if (more) {
                const int n = uart_read_bytes(kUart, buf + 1, more, 0);
                if (n > 0) got += n;
            }
        }
        xSemaphoreTake(lock, portMAX_DELAY);
        for (int i = 0; i < got; i++) {
            if (kb_link_feed(&rx, buf[i], now_ms())) handle_locked();
        }
        xSemaphoreGive(lock);
        // Tell a keyboard that has just come onto USB where the KVM is.
        const bool on_usb = keyboard_on_usb();
        if (on_usb && !was_on_usb) {
            uint8_t state[4];
            if (kvm_client::state(state)) send_kvm_state(state);
        }
        was_on_usb = on_usb;
    }
}

}  // namespace

esp_err_t start() {
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;

    // After a deep-sleep wake the RX pin is still routed to the RTC block (wake source).
    if (rtc_gpio_is_valid_gpio(kRx)) rtc_gpio_deinit(kRx);

    uart_config_t cfg = {};  // fields vary between IDF versions; zero the rest
    cfg.baud_rate = KB_LINK_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    esp_err_t err = uart_driver_install(kUart, 1024, 0, 0, nullptr, 0);
    if (err == ESP_OK) err = uart_param_config(kUart, &cfg);
    if (err == ESP_OK) err = uart_set_pin(kUart, kTx, kRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // Pulled down, so an unpowered keyboard (or no keyboard) reads as "not on USB".
    if (err == ESP_OK) err = gpio_set_pull_mode(kRx, GPIO_PULLDOWN_ONLY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UART on RX %d / TX %d: %s", (int)kRx, (int)kTx, esp_err_to_name(err));
        return err;
    }
    started = true;
    if (xTaskCreate(rx_task, "kb_link", 4096, nullptr, 4, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "UART RX GPIO%d TX GPIO%d at %u baud; keyboard %s", (int)kRx, (int)kTx,
             (unsigned)KB_LINK_BAUD, keyboard_on_usb() ? "on USB" : "not on USB");
    return ESP_OK;
}

bool keyboard_on_usb() { return gpio_get_level(kRx) == 1; }

bool send_text(const char *text) {
    if (!started) return false;
    const size_t len = strnlen(text, KB_LINK_MAX_PAYLOAD);
    xSemaphoreTake(lock, portMAX_DELAY);
    send_locked(EK_TEXT, text, static_cast<uint8_t>(len), tx_seq++);
    xSemaphoreGive(lock);
    return true;
}

void send_kvm_state(const uint8_t state[4]) {
    if (!started) return;
    xSemaphoreTake(lock, portMAX_DELAY);
    send_locked(EK_KVM_STATE, state, 4, tx_seq++);
    xSemaphoreGive(lock);
}

namespace {
// JSON string body: escapes quotes and backslashes (text is printable ASCII already).
int put_escaped(char *buf, size_t len, const char *s) {
    size_t o = 0;
    for (; *s && o + 2 < len; s++) {
        if (*s == '"' || *s == '\\') buf[o++] = '\\';
        buf[o++] = *s;
    }
    if (len) buf[o < len ? o : len - 1] = 0;
    return static_cast<int>(o);
}

// snprintf that never runs past the end: returns how much it added, 0 if it didn't fit.
int add(char *buf, size_t len, int o, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
int add(char *buf, size_t len, int o, const char *fmt, ...) {
    if (o < 0 || (size_t)o >= len) return 0;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(buf + o, len - o, fmt, ap);
    va_end(ap);
    return n > 0 && (size_t)(o + n) < len ? n : 0;
}
}  // namespace

int status_json(char *buf, size_t len) {
    if (!lock) return snprintf(buf, len, "null");
    xSemaphoreTake(lock, portMAX_DELAY);
    const int64_t now = esp_timer_get_time();
    int o = 0;
    o += add(buf, len, o, "{\"usb\":%s,\"hello\":\"", keyboard_on_usb() ? "true" : "false");
    o += put_escaped(buf + o, len - o, hello);
    o += add(buf, len, o, "\",\"status\":");
    if (status_at_us) {
        const kb_link_status &s = status;
        o += add(buf, len, o,
                 "{\"transport\":%u,\"usb_power\":%u,\"host_os\":%u,\"base_layer\":%u,\"battery_pct\":%u,"
                 "\"resends\":%u,\"rx_bad\":%u,\"rx_good\":%u,\"uptime_s\":%lu}",
                 s.transport, s.usb_power, s.host_os, s.base_layer, s.battery_pct, s.resends, s.rx_bad,
                 s.rx_good, (unsigned long)s.uptime_s);
    } else {
        o += add(buf, len, o, "null");
    }
    o += add(buf, len, o, ",\"status_ago_s\":%lld,\"rx_good\":%lu,\"rx_bad\":%lu,\"acks\":%lu,\"kvm_requests\":%lu,\"recent\":[",
             status_at_us ? (long long)((now - status_at_us) / 1000000) : -1LL, (unsigned long)rx.good,
             (unsigned long)rx.bad, (unsigned long)acks, (unsigned long)kvm_requests);
    for (size_t i = 0; i < recent_count && (size_t)o + 64 < len; i++) {
        const Text &t = recent[(recent_next + kRecent - recent_count + i) % kRecent];
        o += add(buf, len, o, "%s{\"ago_s\":%lld,\"text\":\"", i ? "," : "", (long long)((now - t.at_us) / 1000000));
        o += put_escaped(buf + o, len - o, t.text);
        o += add(buf, len, o, "\"}");
    }
    o += add(buf, len, o, "]}");
    xSemaphoreGive(lock);
    return o;
}

}  // namespace kb_link
