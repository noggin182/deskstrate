// Core pin map per board, selected by the PlatformIO env's build_flags.
// Shared modules (ddc, swd) take pins as arguments; only core app code includes this.
#pragma once

#include "driver/gpio.h"

namespace board {

#if defined(DESKSTRATE_BOARD_FEATHER_BENCH)
// Adafruit Feather ESP32-S3 TFT standing in for the S3 Mini on the bench.
// Left alone: TFT 35/36/7/39/40, backlight 45, TFT/I2C power 21, NeoPixel 33/34,
// fuel gauge I2C 41/42, NeoKey keys 5/6/10/11 + pixels 9 (if the wings are fitted).
inline constexpr const char *kName = "Feather ESP32-S3 TFT (bench)";

inline constexpr gpio_num_t kDdc1Sda = GPIO_NUM_1;    // TX pad
inline constexpr gpio_num_t kDdc1Scl = GPIO_NUM_2;    // RX pad
inline constexpr gpio_num_t kDdc2Sda = GPIO_NUM_8;    // A5
inline constexpr gpio_num_t kDdc2Scl = GPIO_NUM_14;   // A4

inline constexpr gpio_num_t kHidSwclk = GPIO_NUM_17;   // A1
inline constexpr gpio_num_t kHidSwdio = GPIO_NUM_18;   // A0
inline constexpr gpio_num_t kMediaSwclk = GPIO_NUM_15; // A3
inline constexpr gpio_num_t kMediaSwdio = GPIO_NUM_16; // A2

#elif defined(DESKSTRATE_BOARD_S3_MINI)
// diymore ESP32-S3 Mini (Waveshare ESP32-S3-Zero layout).
// UNCONFIRMED: check `esptool.py chip_id` and the silkscreen, then toggle-test.
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

#else
#error "No board: build with a firmware/core PlatformIO env (feather_bench or s3_mini)"
#endif

}  // namespace board
