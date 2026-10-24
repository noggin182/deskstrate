// Minimal DDC/CI (MCCS) master for the Dell P2723DE, on the ESP-IDF i2c_master driver.
//
// One ddc::Bus per monitor (one I2C controller each; the S3 has two).
// Pins are passed in; board pin maps live in board_config.h.
// Only 0x37 (DDC/CI) is written. EDID (0x50) is read-only.
// The MStar ISP (0x49) and debug (0x59) addresses are never touched.
#pragma once

#include <cstdint>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace ddc {

// VCP codes and values (VERIFIED on the P2723DE)
inline constexpr uint8_t kVcpInput = 0x60;
inline constexpr uint8_t kVcpPower = 0xD6;
inline constexpr uint8_t kInputDp = 0x0F;    // PC
inline constexpr uint8_t kInputHdmi = 0x11;  // guest
inline constexpr uint8_t kInputUsbc = 0x1B;  // Mac
inline constexpr uint8_t kPowerOn = 1;
inline constexpr uint8_t kPowerStandby = 4;  // never 5 (soft off); set_vcp refuses it

// Monitors return garbage for ~1 s after an input change; wait this long before reading.
inline constexpr uint32_t kInputSettleMs = 2000;

struct Pins {
    gpio_num_t sda;
    gpio_num_t scl;
};

struct VcpValue {
    uint8_t type;      // 0 = set parameter, 1 = momentary
    uint16_t max;
    uint16_t current;  // raw 16-bit value as sent by the monitor
};

// VCP 0x60 is 16-bit: high byte = the port the query came in on, low byte = active input.
inline uint8_t active_input(uint16_t raw) { return raw & 0xFF; }
inline uint8_t query_port(uint16_t raw) { return raw >> 8; }
const char *input_name(uint8_t input);

// Bring-up only, call BEFORE Bus::init: drives SDA low, releases it, then SCL, each for
// hold_ms, logging each step so you can follow it with a meter on the HDMI pins
// (16 = SDA, 15 = SCL). Open-drain, so "released" means the pull-ups win (~5 V on the
// HDMI side).
void line_toggle_test(Pins pins, uint32_t hold_ms = 3000, int cycles = 2);

class Bus {
public:
    // scl_hz: DDC allows 100 kHz; 50 kHz leaves margin for the BSS138 and cable capacitance.
    esp_err_t init(i2c_port_num_t port, Pins pins, uint32_t scl_hz = 50000);

    // Returns ESP_OK; ESP_ERR_NOT_SUPPORTED (monitor says the code doesn't exist);
    // ESP_ERR_INVALID_CRC / ESP_ERR_INVALID_RESPONSE (bad reply after all retries);
    // or an I2C error (ESP_FAIL = NACK, ESP_ERR_TIMEOUT).
    esp_err_t get_vcp(uint8_t code, VcpValue &out);
    esp_err_t set_vcp(uint8_t code, uint16_t value);

    // 0x60 read with the & 0xFF mask applied. raw is optional.
    esp_err_t get_input(uint8_t &input, uint16_t *raw = nullptr);

    // Is anything ACKing 0x37 / 0x50? Useful as the first wiring check.
    // ESP_OK = ACK; ESP_ERR_NOT_FOUND = clean NACK; ESP_ERR_TIMEOUT = bus stuck (SDA/SCL low).
    esp_err_t probe_ddcci();
    esp_err_t probe_edid();

    // Idle line levels as the GPIO matrix sees them (1 = pulled up). For wiring checks.
    int sda_level() const;
    int scl_level() const;

    // Reads the 128-byte base EDID block and returns the 32-bit serial (bytes 12..15, LE).
    // Identify monitors by this, never by bus order. May NACK with nothing on HDMI pin 18
    // (some EDID EEPROMs are powered from it).
    esp_err_t read_edid_serial(uint32_t &serial);

    int retries = 3;

private:
    esp_err_t transact_get(uint8_t code, VcpValue &out);
    void wait_gap();
    void recover();

    Pins pins_{};
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t ddcci_ = nullptr;
    i2c_master_dev_handle_t edid_ = nullptr;
    SemaphoreHandle_t lock_ = nullptr;
    TickType_t last_cmd_ = 0;
};

}  // namespace ddc
