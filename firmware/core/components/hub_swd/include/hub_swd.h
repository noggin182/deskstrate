// SWD control for the OEM 1:4 USB hubs (PY32F002A, Cortex-M0+), bit-banged on two GPIOs.
//
// One hub_swd::Hub per hub (board_config has HID on one pin pair, MEDIA on another).
// Pins are passed in; board pin maps live in board_config.h.
//
// The hub's firmware keeps its selection state as four bytes in SRAM. Reading them tells
// us the live input; writing the "requested" byte makes the firmware's main loop switch,
// exactly as a front-panel key press would (no key simulation, no UART needed). Access is
// through the MEM-AP with the core left running: no halt, no reset.
//
// Ports are 0..3 throughout, matching enum kvm_usb_port and the low nibble of KVM_NODE
// (0 = PC, 1 = MAC, 2/3 = guests). 0xFF = none/unknown. The 1..4 panel labels are a UI
// concern only (port + 1); nothing here reverses or offsets the index.
//
// IMPORTANT: kStatusAddr is firmware-build-specific. It is the address of the four state
// bytes { held_lockout, requested, current, applied }. It was found at 0x200001FC on the
// VerC bench hub (RDP-locked, so located from SRAM, not a symbol table). A different hub
// build can place it elsewhere: find it by reading SRAM and watching which byte tracks the
// input across a known switch. See firmware/swd_dump for that bring-up probe.
#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

namespace hub_swd {

inline constexpr uint8_t kNoPort = 0xFF;      // == KVM_USB_UNKNOWN, == firmware NO_REQUEST
inline constexpr uint32_t kStatusAddr = 0x200001FC;

struct Pins {
    gpio_num_t swclk;
    gpio_num_t swdio;
};

// The four selection bytes, unpacked. Ports are 0..3, or kNoPort.
struct Status {
    uint8_t held_lockout;  // non-zero while a long-press lockout is active
    uint8_t requested;     // pending port, or kNoPort when idle
    uint8_t current;       // selected port (this is the live input: panel label = current + 1)
    uint8_t applied;       // port the output lines were last written for
};

class Hub {
public:
    // Stores config and parks the lines idle-high. clk_half_us sets the bit-bang speed
    // (half clock period); the default is conservative for level-shifted / capacitive
    // wiring. Pass the status-word address for this hub's build (see kStatusAddr note).
    esp_err_t init(Pins pins, uint32_t status_addr = kStatusAddr, uint32_t clk_half_us = 5);

    // SWD line reset, power up the debug domain, configure the MEM-AP for 32-bit access.
    // Call once before read_status/request; idcode is optional (SW-DP IDCODE on success).
    // ESP_OK, or ESP_ERR_INVALID_RESPONSE / ESP_ERR_TIMEOUT with the failing stage logged.
    esp_err_t connect(uint32_t *idcode = nullptr);

    // Read the four state bytes. No reversal: ports come back 0..3 or kNoPort.
    esp_err_t read_status(Status &out);

    // Ask the hub to switch to port (0..3). Reads the status word, overwrites just the
    // requested byte, writes it back; the hub's main loop does the rest (hub reset + a
    // ~0.6 s device-power cut, same as a key press). Only issued when idle
    // (status.requested == kNoPort): ESP_ERR_NOT_FINISHED while a switch is pending, since
    // a request mid-switch would race the firmware's own write.
    esp_err_t request(uint8_t port);

    // Raw 32-bit memory read/write through the MEM-AP, for probing. Word-aligned.
    esp_err_t read_word(uint32_t addr, uint32_t &out);
    esp_err_t write_word(uint32_t addr, uint32_t value);

    // Name of the last stage that failed (connect() stages, or "read"/"write" for memory
    // access), for diagnostics ("OK" if none).
    const char *last_stage() const { return stage_; }

    // SWD ACK of the transfer that failed: 7 = no reply (nothing driving SWDIO), 2 = WAIT,
    // 4 = FAULT, 8 = parity error, anything else = out of step (often SWCLK/SWDIO swapped).
    uint8_t last_ack() const { return ack_; }

private:
    uint8_t transfer(bool ap, bool read, uint8_t addr, uint32_t &data);
    uint8_t transfer_retry(bool ap, bool read, uint8_t addr, uint32_t &data);
    void clock_cycle();
    void write_bits(uint32_t value, int count);
    uint32_t read_bits(int count);
    void dio_output();
    void dio_input();

    Pins pins_{};
    uint32_t status_addr_ = kStatusAddr;
    uint32_t half_us_ = 5;
    const char *stage_ = "OK";
    uint8_t ack_ = 1;
};

}  // namespace hub_swd
