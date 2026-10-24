// swd_dump: read-only SWD dumper for a USB hub's PY32F002A (Cortex-M0+).
// Bit-bangs SWD, reads memory through the MEM-AP while the target keeps running:
// no halt, no reset, no writes to target memory. Repeats every 20 s so a host can
// catch one complete block between the BEGIN and END lines.
//
// Output, on the USB Serial/JTAG console:
//   BEGIN
//   I <name> <hex word>              target ID registers
//   D <addr> <32 bytes as hex>       memory, bytes in address order
//   END ok <bytes>  |  END fault <addr> <bytes>
#include <cstdint>
#include <cstdio>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr gpio_num_t kSwclk = GPIO_NUM_1;
constexpr gpio_num_t kSwdio = GPIO_NUM_2;

constexpr uint32_t kFlashBase = 0x08000000;
constexpr uint32_t kFlashSize = 0x5000;  // PY32F002A: 20 KB

constexpr uint32_t kSramBase = 0x20000000;
constexpr uint32_t kSramSize = 0x0C00;  // PY32F002A: 3 KB

enum Ack : uint8_t { kAckOk = 1, kAckWait = 2, kAckFault = 4, kAckParity = 8 };

// DP registers
constexpr uint8_t kDpIdcode = 0x0, kDpAbort = 0x0, kDpCtrlStat = 0x4, kDpSelect = 0x8, kDpRdbuff = 0xC;
// MEM-AP registers (bank 0) and IDR (bank 0xF)
constexpr uint8_t kApCsw = 0x0, kApTar = 0x4, kApDrw = 0xC, kApIdr = 0xC;

// ---------------------------------------------------------------- bit level

inline void half_period() { esp_rom_delay_us(1); }

void clock_cycle() {
    gpio_set_level(kSwclk, 0);
    half_period();
    gpio_set_level(kSwclk, 1);
    half_period();
}

// Host drives SWDIO while SWCLK is low; target samples on the rising edge.
void write_bits(uint32_t value, int count) {
    for (int i = 0; i < count; i++) {
        gpio_set_level(kSwdio, (value >> i) & 1);
        clock_cycle();
    }
}

// Target drives SWDIO on the rising edge; sample it while SWCLK is low.
uint32_t read_bits(int count) {
    uint32_t value = 0;
    for (int i = 0; i < count; i++) {
        gpio_set_level(kSwclk, 0);
        half_period();
        value |= (uint32_t)gpio_get_level(kSwdio) << i;
        gpio_set_level(kSwclk, 1);
        half_period();
    }
    return value;
}

void drive_swdio(bool out) {
    gpio_set_direction(kSwdio, out ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
}

int parity32(uint32_t v) { return __builtin_popcount(v) & 1; }

// ---------------------------------------------------------------- transfers

uint8_t transfer_once(bool ap, bool read, uint8_t addr, uint32_t *data) {
    uint8_t a23 = (addr >> 2) & 3;
    uint8_t req = 1 | (ap << 1) | (read << 2) | (a23 << 3);
    req |= parity32((req >> 1) & 0xF) << 5;
    req |= 1 << 7;  // park

    drive_swdio(true);
    write_bits(req, 8);
    drive_swdio(false);
    clock_cycle();  // turnaround
    uint8_t ack = read_bits(3);

    if (ack == kAckOk && read) {
        uint32_t v = read_bits(32);
        int p = read_bits(1);
        clock_cycle();  // turnaround
        drive_swdio(true);
        if (p != parity32(v)) ack = kAckParity;
        *data = v;
    } else if (ack == kAckOk) {
        clock_cycle();  // turnaround
        drive_swdio(true);
        write_bits(*data, 32);
        write_bits(parity32(*data), 1);
    } else {
        clock_cycle();  // turnaround after WAIT / FAULT / no response
        drive_swdio(true);
    }
    write_bits(0, 8);  // idle
    return ack;
}

uint8_t transfer(bool ap, bool read, uint8_t addr, uint32_t *data) {
    uint8_t ack = kAckWait;
    for (int tries = 0; tries < 100 && ack == kAckWait; tries++) ack = transfer_once(ap, read, addr, data);
    return ack;
}

uint8_t dp_read(uint8_t addr, uint32_t *v) { return transfer(false, true, addr, v); }
uint8_t dp_write(uint8_t addr, uint32_t v) { return transfer(false, false, addr, &v); }
uint8_t ap_write(uint8_t addr, uint32_t v) { return transfer(true, false, addr, &v); }

// AP reads are posted: the value arrives on the next AP read or on DP RDBUFF.
uint8_t ap_read(uint8_t addr, uint32_t *v) {
    uint32_t discard;
    uint8_t ack = transfer(true, true, addr, &discard);
    if (ack != kAckOk) return ack;
    return dp_read(kDpRdbuff, v);
}

void clear_errors() { dp_write(kDpAbort, 0x1E); }

uint8_t mem_read(uint32_t addr, uint32_t *v) {
    uint8_t ack = ap_write(kApTar, addr);
    if (ack == kAckOk) ack = ap_read(kApDrw, v);
    if (ack != kAckOk) clear_errors();
    return ack;
}

// ---------------------------------------------------------------- connect

void line_reset() {
    drive_swdio(true);
    write_bits(0xFFFFFFFF, 32);
    write_bits(0xFFFFFFFF, 24);
    write_bits(0xE79E, 16);  // JTAG-to-SWD
    write_bits(0xFFFFFFFF, 32);
    write_bits(0xFFFFFFFF, 24);
    write_bits(0, 8);
}

bool connect(uint32_t *idcode) {
    line_reset();
    uint8_t ack = dp_read(kDpIdcode, idcode);
    if (ack != kAckOk) {
        printf("E idcode ack %u\n", ack);
        return false;
    }
    clear_errors();
    dp_write(kDpCtrlStat, 0x50000000);  // CSYSPWRUPREQ | CDBGPWRUPREQ
    uint32_t stat = 0;
    for (int i = 0; i < 100; i++) {
        if (dp_read(kDpCtrlStat, &stat) == kAckOk && (stat & 0xA0000000) == 0xA0000000) break;
        vTaskDelay(1);
    }
    if ((stat & 0xA0000000) != 0xA0000000) {
        printf("E power-up ack, ctrl/stat %08lx\n", (unsigned long)stat);
        return false;
    }
    uint32_t idr = 0;
    dp_write(kDpSelect, 0x000000F0);
    ap_read(kApIdr, &idr);
    dp_write(kDpSelect, 0);
    printf("I ap_idr %08lx\n", (unsigned long)idr);
    ack = ap_write(kApCsw, 0x23000002);  // 32-bit access, no auto-increment
    if (ack != kAckOk) {
        printf("E csw ack %u\n", ack);
        return false;
    }
    return true;
}

void info(const char *name, uint32_t addr) {
    uint32_t v = 0;
    uint8_t ack = mem_read(addr, &v);
    if (ack == kAckOk)
        printf("I %s %08lx\n", name, (unsigned long)v);
    else
        printf("I %s ack %u\n", name, ack);
}

void dump() {
    printf("BEGIN\n");
    uint32_t idcode = 0;
    if (!connect(&idcode)) {
        printf("END fault connect 0\n");
        return;
    }
    printf("I dp_idcode %08lx\n", (unsigned long)idcode);
    info("cpuid", 0xE000ED00);
    info("dbgmcu_idcode", 0x40015800);

    // Flash-controller register window (0x40022000..0x400220A0). These peripheral
    // registers are not read-protected, so they read back whatever RDP level is set.
    // Labelled by address so we can identify OPTR/SDKR/WRPR regardless of exact offset.
    for (uint32_t addr = 0x40022000; addr < 0x400220A4; addr += 4) {
        uint32_t v = 0;
        uint8_t ack = mem_read(addr, &v);
        if (ack == kAckOk)
            printf("R %08lx %08lx\n", (unsigned long)addr, (unsigned long)v);
        else
            printf("R %08lx ack %u\n", (unsigned long)addr, ack);
    }

    // Option-byte shadow region in system memory (reads fine under RDP).
    for (uint32_t addr = 0x1FFF0E00; addr < 0x1FFF0E20; addr += 4) {
        uint32_t v = 0;
        uint8_t ack = mem_read(addr, &v);
        if (ack == kAckOk) printf("O %08lx %08lx\n", (unsigned long)addr, (unsigned long)v);
    }

    // Is main flash actually blocked? Read the first word a few times.
    for (int i = 0; i < 3; i++) {
        uint32_t v = 0;
        uint8_t ack = mem_read(0x08000000, &v);
        if (ack == kAckOk)
            printf("F 08000000 %08lx\n", (unsigned long)v);
        else
            printf("F 08000000 ack %u\n", ack);
    }

    info("sysmem_0", 0x1FFF0000);

    // Main flash is read-protected (RDP), so dump SRAM instead. The firmware copies
    // its 48-word vector table to 0x20000000, so the start of SRAM mirrors the first
    // 0xC0 bytes of the flash image; the rest holds settings[] and the state vars.
    uint32_t base = kFlashBase;
    uint32_t size = kFlashSize;
    const char *what = "flash";
    uint32_t probe = 0;
    if (mem_read(0x08000000, &probe) != kAckOk) {
        base = kSramBase;
        size = kSramSize;
        what = "sram";
    }
    printf("MEM %s\n", what);
    uint32_t done = 0;
    for (uint32_t addr = base; addr < base + size; addr += 32) {
        uint8_t line[32];
        for (int w = 0; w < 8; w++) {
            uint32_t v = 0;
            uint8_t ack = mem_read(addr + w * 4, &v);
            if (ack != kAckOk) {
                printf("END fault %08lx %lu\n", (unsigned long)(addr + w * 4), (unsigned long)done);
                return;
            }
            for (int b = 0; b < 4; b++) line[w * 4 + b] = v >> (8 * b);
        }
        printf("D %08lx ", (unsigned long)addr);
        for (uint8_t b : line) printf("%02x", b);
        printf("\n");
        done += 32;
    }
    printf("END ok %lu\n", (unsigned long)done);
}

}  // namespace

extern "C" void app_main(void) {
    gpio_config_t io = {};
    io.pin_bit_mask = (1ULL << kSwclk) | (1ULL << kSwdio);
    io.mode = GPIO_MODE_OUTPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
    gpio_set_level(kSwclk, 1);
    gpio_set_level(kSwdio, 1);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        dump();
        vTaskDelay(pdMS_TO_TICKS(17000));
    }
}
