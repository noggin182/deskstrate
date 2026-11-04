#include "kb_link.h"

#include <atomic>
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
#include "sdkconfig.h"

namespace kb_link {

namespace {

const char *TAG = "kb_link";
constexpr uart_port_t kUart = UART_NUM_1;
constexpr gpio_num_t kRx = static_cast<gpio_num_t>(CONFIG_DESKSTRATE_KB_RX_GPIO);
constexpr gpio_num_t kTx = static_cast<gpio_num_t>(CONFIG_DESKSTRATE_KB_TX_GPIO);
constexpr size_t kLineMax = 96;
constexpr size_t kRecent = 16;

struct Line {
    int64_t at_us;
    char text[kLineMax];
};

SemaphoreHandle_t lock = nullptr;
Line recent[kRecent];
size_t recent_next = 0, recent_count = 0;
std::atomic<uint32_t> rx_lines{0}, rx_bytes{0}, tx_lines{0};
std::atomic<bool> started{false};

void keep(const char *text) {
    xSemaphoreTake(lock, portMAX_DELAY);
    Line &l = recent[recent_next];
    l.at_us = esp_timer_get_time();
    snprintf(l.text, sizeof l.text, "%s", text);
    recent_next = (recent_next + 1) % kRecent;
    if (recent_count < kRecent) recent_count++;
    xSemaphoreGive(lock);
}

void say_hello() {
    char hello[64];
    snprintf(hello, sizeof hello, "hello deskstrate-kb %s", esp_app_get_description()->version);
    send_line(hello);
}

void rx_task(void *) {
    char line[kLineMax];
    size_t n = 0;
    bool was_on_usb = keyboard_on_usb();
    uint8_t buf[64];
    for (;;) {
        const int got = uart_read_bytes(kUart, buf, sizeof buf, pdMS_TO_TICKS(100));
        for (int i = 0; i < got; i++) {
            const uint8_t c = buf[i];
            rx_bytes++;
            if (c == '\n' || c == '\r') {
                if (n == 0) continue;
                line[n] = 0;
                n = 0;
                rx_lines++;
                ESP_LOGI(TAG, "kb: %s", line);
                keep(line);
            } else if (c >= 0x20 && c < 0x7F && n < sizeof line - 1) {
                line[n++] = static_cast<char>(c);
            }
            // Anything else (0x00 from the line going low, framing noise) is dropped.
        }
        // Greet the keyboard each time it comes (back) onto USB.
        const bool on_usb = keyboard_on_usb();
        if (on_usb && !was_on_usb) say_hello();
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
    cfg.baud_rate = CONFIG_DESKSTRATE_KB_BAUD;
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
    ESP_LOGI(TAG, "UART RX GPIO%d TX GPIO%d at %d baud; keyboard %s", (int)kRx, (int)kTx,
             CONFIG_DESKSTRATE_KB_BAUD, keyboard_on_usb() ? "on USB" : "not on USB");
    if (keyboard_on_usb()) say_hello();
    return ESP_OK;
}

bool keyboard_on_usb() { return gpio_get_level(kRx) == 1; }

bool send_line(const char *text) {
    if (!started) return false;
    const size_t len = strnlen(text, kLineMax);
    if (uart_write_bytes(kUart, text, len) < 0 || uart_write_bytes(kUart, "\n", 1) < 0) return false;
    tx_lines++;
    return true;
}

namespace {
// JSON string body: escapes quotes and backslashes (lines are printable ASCII already).
int put_escaped(char *buf, size_t len, const char *s) {
    size_t o = 0;
    for (; *s && o + 2 < len; s++) {
        if (*s == '"' || *s == '\\') buf[o++] = '\\';
        buf[o++] = *s;
    }
    if (len) buf[o < len ? o : len - 1] = 0;
    return static_cast<int>(o);
}
}  // namespace

int status_json(char *buf, size_t len) {
    int o = snprintf(buf, len, "{\"usb\":%s,\"rx_lines\":%lu,\"rx_bytes\":%lu,\"tx_lines\":%lu,\"recent\":[",
                     keyboard_on_usb() ? "true" : "false", (unsigned long)rx_lines.load(),
                     (unsigned long)rx_bytes.load(), (unsigned long)tx_lines.load());
    if (lock) {
        xSemaphoreTake(lock, portMAX_DELAY);
        const int64_t now = esp_timer_get_time();
        for (size_t i = 0; i < recent_count && o > 0 && (size_t)o + 32 < len; i++) {
            const Line &l = recent[(recent_next + kRecent - recent_count + i) % kRecent];
            o += snprintf(buf + o, len - o, "%s{\"ago_s\":%lld,\"text\":\"", i ? "," : "",
                          (long long)((now - l.at_us) / 1000000));
            if ((size_t)o >= len) break;
            o += put_escaped(buf + o, len - o, l.text);
            if ((size_t)o + 2 < len) o += snprintf(buf + o, len - o, "\"}");
        }
        xSemaphoreGive(lock);
    }
    if (o > 0 && (size_t)o + 3 < len) o += snprintf(buf + o, len - o, "]}");
    return o;
}

}  // namespace kb_link
