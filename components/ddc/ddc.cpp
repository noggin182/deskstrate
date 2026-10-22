#include "ddc.h"

#include <cstring>
#include "esp_log.h"
#include "freertos/task.h"

namespace ddc {

static const char *TAG = "ddc";

namespace {

constexpr uint16_t kAddrDdcci = 0x37;     // 0x6E/0x6F on the wire
constexpr uint16_t kAddrEdid = 0x50;
constexpr uint8_t kHostAddr = 0x51;       // source address in host->display messages
constexpr uint8_t kReplyChkSeed = 0x50;   // replies are checksummed against 0x50
constexpr int kXferTimeoutMs = 100;
constexpr uint32_t kReplyDelayMs = 50;    // spec >= 40 ms before reading a reply
constexpr uint32_t kCmdGapMs = 50;        // spec >= 50 ms between commands
constexpr uint32_t kRetryDelayMs = 100;

// Checksum of a host->display message: XOR of 0x6E and every byte from 0x51 up.
uint8_t request_checksum(const uint8_t *msg, size_t len) {
    uint8_t chk = kAddrDdcci << 1;
    for (size_t i = 0; i < len; i++) chk ^= msg[i];
    return chk;
}

class Lock {
public:
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
private:
    SemaphoreHandle_t m_;
};

}  // namespace

const char *input_name(uint8_t input) {
    switch (input) {
        case kInputDp: return "DP (PC)";
        case kInputHdmi: return "HDMI (guest)";
        case kInputUsbc: return "USB-C (Mac)";
        default: return "?";
    }
}

void line_toggle_test(Pins pins, uint32_t hold_ms, int cycles) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pins.sda) | (1ULL << pins.scl);
    cfg.mode = GPIO_MODE_INPUT_OUTPUT_OD;  // the shifter's pull-ups provide high
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    gpio_set_level(pins.sda, 1);
    gpio_set_level(pins.scl, 1);

    struct Line { gpio_num_t pin; const char *name; int hdmi_pin; };
    const Line lines[] = {{pins.sda, "SDA", 16}, {pins.scl, "SCL", 15}};

    for (int c = 0; c < cycles; c++) {
        for (const Line &l : lines) {
            gpio_set_level(l.pin, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            ESP_LOGW(TAG, "toggle: %s (GPIO%d) LOW  -> HDMI pin %d should read ~0 V (reads back %d)",
                     l.name, l.pin, l.hdmi_pin, gpio_get_level(l.pin));
            vTaskDelay(pdMS_TO_TICKS(hold_ms));
            gpio_set_level(l.pin, 1);
            vTaskDelay(pdMS_TO_TICKS(10));
            ESP_LOGW(TAG, "toggle: %s (GPIO%d) HIGH -> HDMI pin %d should read ~5 V (reads back %d)",
                     l.name, l.pin, l.hdmi_pin, gpio_get_level(l.pin));
            vTaskDelay(pdMS_TO_TICKS(hold_ms));
        }
    }
    gpio_reset_pin(pins.sda);
    gpio_reset_pin(pins.scl);
}

esp_err_t Bus::init(i2c_port_num_t port, Pins pins, uint32_t scl_hz) {
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = port;
    bus_cfg.sda_io_num = pins.sda;
    bus_cfg.scl_io_num = pins.scl;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = 0;  // BSS138 board has 10k to 3V3
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_);
    if (err != ESP_OK) return err;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.scl_speed_hz = scl_hz;
    dev_cfg.scl_wait_us = 0;  // default clock-stretch timeout

    dev_cfg.device_address = kAddrDdcci;
    err = i2c_master_bus_add_device(bus_, &dev_cfg, &ddcci_);
    if (err == ESP_OK) {
        dev_cfg.device_address = kAddrEdid;
        err = i2c_master_bus_add_device(bus_, &dev_cfg, &edid_);
    }
    if (err == ESP_OK) {
        lock_ = xSemaphoreCreateMutex();
        if (!lock_) err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        if (edid_) i2c_master_bus_rm_device(edid_);
        if (ddcci_) i2c_master_bus_rm_device(ddcci_);
        i2c_del_master_bus(bus_);
        bus_ = nullptr; ddcci_ = nullptr; edid_ = nullptr;
        return err;
    }
    ESP_LOGI(TAG, "bus %d: SDA GPIO%d, SCL GPIO%d, %lu Hz", port, pins.sda, pins.scl,
             (unsigned long)scl_hz);
    return ESP_OK;
}

void Bus::wait_gap() {
    const TickType_t gap = pdMS_TO_TICKS(kCmdGapMs) + 1;
    const TickType_t since = xTaskGetTickCount() - last_cmd_;
    if (since < gap) vTaskDelay(gap - since);
}

void Bus::recover() {
    // Clocks out a stuck slave and issues a STOP.
    i2c_master_bus_reset(bus_);
}

esp_err_t Bus::transact_get(uint8_t code, VcpValue &out) {
    uint8_t req[5] = {kHostAddr, 0x82, 0x01, code, 0};
    req[4] = request_checksum(req, 4);

    wait_gap();
    esp_err_t err = i2c_master_transmit(ddcci_, req, sizeof req, kXferTimeoutMs);
    if (err != ESP_OK) {
        last_cmd_ = xTaskGetTickCount();
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(kReplyDelayMs));

    uint8_t b[11] = {};
    err = i2c_master_receive(ddcci_, b, sizeof b, kXferTimeoutMs);
    last_cmd_ = xTaskGetTickCount();
    if (err != ESP_OK) return err;

    if ((b[1] & 0x7F) == 0) {
        ESP_LOGD(TAG, "VCP 0x%02X: null reply (busy)", code);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (b[1] != 0x88 || b[2] != 0x02 || b[4] != code) {
        ESP_LOGD(TAG, "VCP 0x%02X: malformed %02X %02X %02X %02X %02X", code, b[0], b[1],
                 b[2], b[3], b[4]);
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t chk = kReplyChkSeed;
    for (int i = 0; i < 10; i++) chk ^= b[i];
    if (chk != b[10]) {
        ESP_LOGD(TAG, "VCP 0x%02X: checksum %02X != %02X", code, chk, b[10]);
        return ESP_ERR_INVALID_CRC;
    }
    if (b[3] != 0) return ESP_ERR_NOT_SUPPORTED;

    out.type = b[5];
    out.max = (uint16_t)(b[6] << 8 | b[7]);
    out.current = (uint16_t)(b[8] << 8 | b[9]);
    return ESP_OK;
}

esp_err_t Bus::get_vcp(uint8_t code, VcpValue &out) {
    if (!bus_) return ESP_ERR_INVALID_STATE;
    Lock lock(lock_);
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < retries; attempt++) {
        if (attempt) vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
        err = transact_get(code, out);
        if (err == ESP_OK || err == ESP_ERR_NOT_SUPPORTED) return err;
        if (err != ESP_ERR_INVALID_RESPONSE && err != ESP_ERR_INVALID_CRC) recover();
    }
    ESP_LOGW(TAG, "get VCP 0x%02X failed: %s", code, esp_err_to_name(err));
    return err;
}

esp_err_t Bus::set_vcp(uint8_t code, uint16_t value) {
    if (!bus_) return ESP_ERR_INVALID_STATE;
    if (code == kVcpPower && value == 5) {
        ESP_LOGE(TAG, "refusing D6=5 (soft off); use 4 for standby");
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t req[7] = {kHostAddr, 0x84, 0x03, code, (uint8_t)(value >> 8), (uint8_t)value, 0};
    req[6] = request_checksum(req, 6);

    Lock lock(lock_);
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < retries; attempt++) {
        if (attempt) vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
        wait_gap();
        err = i2c_master_transmit(ddcci_, req, sizeof req, kXferTimeoutMs);
        last_cmd_ = xTaskGetTickCount();
        if (err == ESP_OK) return ESP_OK;
        recover();
    }
    ESP_LOGW(TAG, "set VCP 0x%02X=%u failed: %s", code, value, esp_err_to_name(err));
    return err;
}

esp_err_t Bus::get_input(uint8_t &input, uint16_t *raw) {
    VcpValue v{};
    esp_err_t err = get_vcp(kVcpInput, v);
    if (err != ESP_OK) return err;
    input = active_input(v.current);
    if (raw) *raw = v.current;
    return ESP_OK;
}

bool Bus::probe_ddcci() {
    if (!bus_) return false;
    Lock lock(lock_);
    wait_gap();
    const bool ok = i2c_master_probe(bus_, kAddrDdcci, kXferTimeoutMs) == ESP_OK;
    last_cmd_ = xTaskGetTickCount();
    return ok;
}

bool Bus::probe_edid() {
    if (!bus_) return false;
    Lock lock(lock_);
    return i2c_master_probe(bus_, kAddrEdid, kXferTimeoutMs) == ESP_OK;
}

esp_err_t Bus::read_edid_serial(uint32_t &serial) {
    if (!bus_) return ESP_ERR_INVALID_STATE;
    static constexpr uint8_t kHeader[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};
    uint8_t edid[128];
    const uint8_t offset = 0;

    Lock lock(lock_);
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < retries; attempt++) {
        if (attempt) vTaskDelay(pdMS_TO_TICKS(kRetryDelayMs));
        err = i2c_master_transmit_receive(edid_, &offset, 1, edid, sizeof edid, kXferTimeoutMs);
        if (err != ESP_OK) { recover(); continue; }
        uint8_t sum = 0;
        for (uint8_t byte : edid) sum += byte;
        if (memcmp(edid, kHeader, sizeof kHeader) != 0 || sum != 0) {
            err = ESP_ERR_INVALID_CRC;
            continue;
        }
        serial = (uint32_t)edid[12] | (uint32_t)edid[13] << 8 | (uint32_t)edid[14] << 16 |
                 (uint32_t)edid[15] << 24;
        return ESP_OK;
    }
    return err;
}

}  // namespace ddc
