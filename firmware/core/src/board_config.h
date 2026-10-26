// Core pin map for the diymore ESP32-S3 Mini.
// Shared modules (ddc, swd) take pins as arguments; only core app code includes this.
#pragma once

#include "driver/gpio.h"

namespace board {

// diymore ESP32-S3 Mini (Waveshare ESP32-S3-Zero layout).
// Chip confirmed: ESP32-S3FH4R2 (4MB flash, 2MB PSRAM).
// Pins below VERIFIED on the wired core (confirmed 2026-10-26).
// Avoid 0 (BOOT), 3/45/46 (strapping), 19/20 (USB), 43/44 (console), 21 (RGB LED).
inline constexpr const char *kName = "ESP32-S3 Mini";

// DDC1 is SDA 11 / SCL 12 (the reverse of DDC2's order): an I2C scan found the monitor
// only with these roles (2026-10-26).
inline constexpr gpio_num_t kDdc1Sda = GPIO_NUM_11;
inline constexpr gpio_num_t kDdc1Scl = GPIO_NUM_12;
inline constexpr gpio_num_t kDdc2Sda = GPIO_NUM_10;
inline constexpr gpio_num_t kDdc2Scl = GPIO_NUM_9;

// Hub 1 = HID, hub 2 = media.
inline constexpr gpio_num_t kHidSwclk = GPIO_NUM_1;
inline constexpr gpio_num_t kHidSwdio = GPIO_NUM_5;
inline constexpr gpio_num_t kMediaSwclk = GPIO_NUM_6;
inline constexpr gpio_num_t kMediaSwdio = GPIO_NUM_4;


}  // namespace board
