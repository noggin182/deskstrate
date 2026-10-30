#include "hub_swd.h"

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hub_swd {
namespace {

constexpr char TAG[] = "hub_swd";

// SWD ACK (3-bit), plus a local parity-error code.
enum Ack : uint8_t { kAckOk = 1, kAckWait = 2, kAckFault = 4, kAckParity = 8 };

// DP / MEM-AP register addresses.
constexpr uint8_t kDpAbort = 0x0, kDpIdcode = 0x0, kDpCtrlStat = 0x4, kDpSelect = 0x8, kDpRdbuff = 0xC;
constexpr uint8_t kApCsw = 0x0, kApTar = 0x4, kApDrw = 0xC;

int parity32(uint32_t v) { return __builtin_popcount(v) & 1; }

}  // namespace

void Hub::dio_output() { gpio_set_direction(pins_.swdio, GPIO_MODE_OUTPUT); }
void Hub::dio_input() { gpio_set_direction(pins_.swdio, GPIO_MODE_INPUT); }

void Hub::clock_cycle() {
    gpio_set_level(pins_.swclk, 0);
    esp_rom_delay_us(half_us_);
    gpio_set_level(pins_.swclk, 1);
    esp_rom_delay_us(half_us_);
}

// Host drives SWDIO while SWCLK is low; target samples on the rising edge.
void Hub::write_bits(uint32_t value, int count) {
    for (int i = 0; i < count; i++) {
        gpio_set_level(pins_.swdio, (value >> i) & 1);
        clock_cycle();
    }
}

// Target drives SWDIO on the rising edge; sample it while SWCLK is low.
uint32_t Hub::read_bits(int count) {
    uint32_t value = 0;
    for (int i = 0; i < count; i++) {
        gpio_set_level(pins_.swclk, 0);
        esp_rom_delay_us(half_us_);
        value |= (uint32_t)gpio_get_level(pins_.swdio) << i;
        gpio_set_level(pins_.swclk, 1);
        esp_rom_delay_us(half_us_);
    }
    return value;
}

uint8_t Hub::transfer(bool ap, bool read, uint8_t addr, uint32_t &data) {
    uint8_t a23 = (addr >> 2) & 3;
    uint8_t req = 1 | (ap << 1) | (read << 2) | (a23 << 3);
    req |= parity32((req >> 1) & 0xF) << 5;
    req |= 1 << 7;  // park

    dio_output();
    write_bits(req, 8);
    dio_input();
    clock_cycle();  // turnaround
    uint8_t ack = read_bits(3);

    if (ack == kAckOk && read) {
        uint32_t v = read_bits(32);
        int p = read_bits(1);
        clock_cycle();  // turnaround
        dio_output();
        if (p != parity32(v)) ack = kAckParity;
        data = v;
    } else if (ack == kAckOk) {
        clock_cycle();  // turnaround
        dio_output();
        write_bits(data, 32);
        write_bits(parity32(data), 1);
    } else {
        clock_cycle();  // turnaround after WAIT / FAULT / no response
        dio_output();
    }
    write_bits(0, 8);  // idle
    return ack;
}

uint8_t Hub::transfer_retry(bool ap, bool read, uint8_t addr, uint32_t &data) {
    uint8_t ack = kAckWait;
    for (int tries = 0; tries < 100 && ack == kAckWait; tries++) ack = transfer(ap, read, addr, data);
    if (ack != kAckOk) ack_ = ack;
    if (ack == kAckFault) {
        uint32_t abort = 0x1E;  // clear sticky error bits
        transfer(false, false, kDpAbort, abort);
    }
    return ack;
}

esp_err_t Hub::init(Pins pins, uint32_t status_addr, uint32_t clk_half_us) {
    pins_ = pins;
    status_addr_ = status_addr;
    half_us_ = clk_half_us ? clk_half_us : 1;
    stage_ = "OK";
    ack_ = kAckOk;

    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << pins_.swclk) | (1ULL << pins_.swdio);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) return err;
    gpio_set_level(pins_.swclk, 1);
    gpio_set_level(pins_.swdio, 1);
    return ESP_OK;
}

void Hub::release() {
    const gpio_num_t pins[] = {pins_.swclk, pins_.swdio};
    for (gpio_num_t pin : pins) {
        gpio_set_direction(pin, GPIO_MODE_INPUT);
        gpio_set_pull_mode(pin, GPIO_FLOATING);
    }
}

esp_err_t Hub::connect(uint32_t *idcode) {
    // Take the lines back (after release()), idle high.
    gpio_set_pull_mode(pins_.swclk, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(pins_.swdio, GPIO_PULLUP_ONLY);
    gpio_set_level(pins_.swclk, 1);
    gpio_set_level(pins_.swdio, 1);
    gpio_set_direction(pins_.swclk, GPIO_MODE_OUTPUT);

    // Line reset + JTAG-to-SWD select sequence.
    dio_output();
    write_bits(0xFFFFFFFF, 32);
    write_bits(0xFFFFFFFF, 24);
    write_bits(0xE79E, 16);  // JTAG-to-SWD magic
    write_bits(0xFFFFFFFF, 32);
    write_bits(0xFFFFFFFF, 24);
    write_bits(0, 8);

    uint32_t id = 0;
    if (transfer_retry(false, true, kDpIdcode, id) != kAckOk) {
        stage_ = "idcode";
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (idcode) *idcode = id;

    uint32_t abort = 0x1E;
    transfer_retry(false, false, kDpAbort, abort);  // clear sticky errors

    uint32_t power = 0x50000000;  // CSYSPWRUPREQ | CDBGPWRUPREQ
    transfer_retry(false, false, kDpCtrlStat, power);
    uint32_t stat = 0;
    bool up = false;
    for (int i = 0; i < 100; i++) {
        if (transfer_retry(false, true, kDpCtrlStat, stat) == kAckOk &&
            (stat & 0xA0000000) == 0xA0000000) {
            up = true;
            break;
        }
        vTaskDelay(1);
    }
    if (!up) {
        stage_ = "power";
        return ESP_ERR_TIMEOUT;
    }

    uint32_t sel = 0;
    transfer_retry(false, false, kDpSelect, sel);         // AP bank 0
    uint32_t csw = 0x23000002;                            // 32-bit access, no auto-increment
    if (transfer_retry(true, false, kApCsw, csw) != kAckOk) {
        stage_ = "csw";
        return ESP_ERR_INVALID_RESPONSE;
    }
    stage_ = "OK";
    return ESP_OK;
}

esp_err_t Hub::read_word(uint32_t addr, uint32_t &out) {
    uint32_t scratch = 0;
    stage_ = "read";
    if (transfer_retry(true, false, kApTar, addr) != kAckOk) return ESP_ERR_INVALID_RESPONSE;
    // AP reads are posted: the value arrives on the following DP RDBUFF read.
    if (transfer_retry(true, true, kApDrw, scratch) != kAckOk) return ESP_ERR_INVALID_RESPONSE;
    if (transfer_retry(false, true, kDpRdbuff, out) != kAckOk) return ESP_ERR_INVALID_RESPONSE;
    stage_ = "OK";
    return ESP_OK;
}

esp_err_t Hub::write_word(uint32_t addr, uint32_t value) {
    stage_ = "write";
    if (transfer_retry(true, false, kApTar, addr) != kAckOk) return ESP_ERR_INVALID_RESPONSE;
    if (transfer_retry(true, false, kApDrw, value) != kAckOk) return ESP_ERR_INVALID_RESPONSE;
    stage_ = "OK";
    return ESP_OK;
}

esp_err_t Hub::read_status(Status &out) {
    uint32_t raw = 0;
    esp_err_t err = read_word(status_addr_, raw);
    if (err != ESP_OK) return err;
    out.held_lockout = raw & 0xFF;
    out.requested = (raw >> 8) & 0xFF;
    out.current = (raw >> 16) & 0xFF;
    out.applied = (raw >> 24) & 0xFF;
    return ESP_OK;
}

esp_err_t Hub::request(uint8_t port) {
    if (port > 3) return ESP_ERR_INVALID_ARG;
    uint32_t raw = 0;
    esp_err_t err = read_word(status_addr_, raw);
    if (err != ESP_OK) return err;
    if (((raw >> 8) & 0xFF) != kNoPort) return ESP_ERR_NOT_FINISHED;  // mid-switch
    raw = (raw & ~(0xFFu << 8)) | ((uint32_t)port << 8);  // overwrite only the requested byte
    return write_word(status_addr_, raw);
}

}  // namespace hub_swd
