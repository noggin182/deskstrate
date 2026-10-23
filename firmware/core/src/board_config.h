// Core pin map for the diymore ESP32-S3 Mini.
// Shared modules (ddc, swd) take pins as arguments; only core app code includes this.
#pragma once

#include "driver/gpio.h"

namespace board {

// diymore ESP32-S3 Mini (Waveshare ESP32-S3-Zero layout).
// Chip confirmed: ESP32-S3FH4R2 (4MB flash, 2MB PSRAM).
// Pins below still to be checked against the silkscreen and toggle-tested.
// Avoid 0 (BOOT), 3/45/46 (strapping), 19/20 (USB), 43/44 (console), 21 (RGB LED).
inline constexpr const char *kName = "ESP32-S3 Mini";

inline constexpr gpio_num_t kDdc1Sda = GPIO_NUM_1;
inline constexpr gpio_num_t kDdc1Scl = GPIO_NUM_2;
inline constexpr gpio_num_t kDdc2Sda = GPIO_NUM_4;
inline constexpr gpio_num_t kDdc2Scl = GPIO_NUM_5;

inline constexpr gpio_num_t kHidSwclk = GPIO_NUM_6;
inline constexpr gpio_num_t kHidSwdio = GPIO_NUM_7;
inline constexpr gpio_num_t kMediaSwclk = GPIO_NUM_8;
inline constexpr gpio_num_t kMediaSwdio = GPIO_NUM_9;


}  // namespace board
