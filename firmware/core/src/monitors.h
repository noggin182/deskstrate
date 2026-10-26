// Both monitors over DDC/CI: owns one ddc::Bus each, polls input and power in the
// background and caches the result so readers (web UI, later UDP) never touch I2C.
#pragma once

#include <cstdint>
#include "esp_err.h"

namespace monitors {

inline constexpr int kCount = 2;  // index = DDC bus (0 = DDC1, 1 = DDC2)

struct State {
    bool online = false;   // ACKing DDC/CI (0x37); false with no monitor on the bus
    uint8_t input = 0;     // VCP 0x60 & 0xFF (ddc::kInput*), 0 = unknown
    uint8_t power = 0;     // VCP 0xD6 (ddc::kPowerOn / kPowerStandby), 0 = unknown
    uint32_t serial = 0;   // EDID serial, 0 = not read yet; kept while offline
    // Why a monitor is offline: lines_up false = SDA or SCL idles low (shifter not wired
    // or unpowered); edid true while offline = a monitor is cabled but DDC/CI is off in
    // its OSD. Lines up and no EDID = nothing cabled, or the monitor is unplugged.
    bool lines_up = false;
    bool edid = false;     // 0x50 ACKs
};

// Inits both buses and starts the poll task. Runs the line toggle test on DDC1 first
// when CONFIG_DESKSTRATE_DDC_TOGGLE_TEST is set.
void start();

// Last polled (or just written) state. Cheap, never blocks on I2C.
State get(int mon);

// "left" / "right" from the EDID serial, nullptr if unknown.
const char *side(uint32_t serial);

// Writes go straight to the monitor; the cache updates on success.
// ESP_ERR_INVALID_ARG for a bad monitor index or value, ESP_ERR_NOT_FOUND if no monitor
// is answering on that bus.
esp_err_t set_input(int mon, uint8_t input);  // ddc::kInputDp / kInputHdmi / kInputUsbc
esp_err_t set_power(int mon, uint8_t power);  // ddc::kPowerOn / kPowerStandby

}  // namespace monitors
