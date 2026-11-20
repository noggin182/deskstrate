#include "serial.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace serial {

namespace {

const char *TAG = "serial";

SemaphoreHandle_t lock = nullptr;  // every write to the port
LineHandler handler = nullptr;

// Call with lock held.
void write_locked(const void *data, size_t len) {
    if (!len || !usb_serial_jtag_is_connected()) return;
    usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(kWriteTimeoutMs));
}

// ESP_LOG's output, with "# " at the start of every line. A log line may arrive in
// several calls, so remember whether the last one ended a line.
bool at_line_start = true;
char log_in[256];
char log_out[320];

int log_vprintf(const char *fmt, va_list ap) {
    // Never wait long: a log from inside a write would otherwise deadlock.
    if (!lock || xSemaphoreTake(lock, pdMS_TO_TICKS(50)) != pdTRUE) return 0;
    int n = vsnprintf(log_in, sizeof log_in, fmt, ap);
    if (n < 0) n = 0;
    size_t len = n < static_cast<int>(sizeof log_in) ? n : sizeof log_in - 1;
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
        if (o + 3 > sizeof log_out) {
            write_locked(log_out, o);
            o = 0;
        }
        if (at_line_start) {
            log_out[o++] = '#';
            log_out[o++] = ' ';
        }
        log_out[o++] = log_in[i];
        at_line_start = log_in[i] == '\n';
    }
    // A log cut short by the buffer still ends its line.
    if (len == sizeof log_in - 1 && !at_line_start) {
        if (o + 1 > sizeof log_out) {
            write_locked(log_out, o);
            o = 0;
        }
        log_out[o++] = '\n';
        at_line_start = true;
    }
    write_locked(log_out, o);
    xSemaphoreGive(lock);
    return n;
}

void reader(void *) {
    uint8_t buf[64];
    uint8_t line[kMaxLine];
    size_t len = 0;
    bool too_long = false;
    for (;;) {
        const int n = usb_serial_jtag_read_bytes(buf, sizeof buf, portMAX_DELAY);
        for (int i = 0; i < n; i++) {
            const uint8_t c = buf[i];
            if (c == '\r' || c == '\n') {
                if (too_long) {
                    ESP_LOGW(TAG, "dropped a line over %u bytes from the Mac", (unsigned)kMaxLine);
                } else if (len) {
                    handler(line, len);
                }
                len = 0;
                too_long = false;
            } else if (len < kMaxLine) {
                line[len++] = c;
            } else {
                too_long = true;
            }
        }
    }
}

}  // namespace

esp_err_t start() {
    lock = xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 1024;
    cfg.rx_buffer_size = 512;
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) return err;
    usb_serial_jtag_vfs_use_driver();  // anything still printed to stdout goes through the driver too
    esp_log_set_vprintf(log_vprintf);
    return ESP_OK;
}

esp_err_t start_reader(LineHandler on_line) {
    handler = on_line;
    return xTaskCreate(reader, "serial_rx", 3072, nullptr, 6, nullptr) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void write_line(const uint8_t *data, size_t len) {
    if (!lock || len > kMaxLine) return;
    uint8_t buf[kMaxLine + 1];
    memcpy(buf, data, len);
    buf[len] = '\n';
    xSemaphoreTake(lock, portMAX_DELAY);
    write_locked(buf, len + 1);
    xSemaphoreGive(lock);
}

}  // namespace serial
