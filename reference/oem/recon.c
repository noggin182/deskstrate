/*
 * SW0404CCAA VerC 4-PC USB switch  -  PY32F002A application firmware, reconstructed
 * from the Ghidra export of fw.hex (0x08000000 .. 0x0800278F).
 *
 * Only the vendor's application logic is here. Everything else in the image is the
 * Puya HAL (GPIO, RCC, FLASH, UART, SysTick) and the ARM C library, renamed inline
 * by what it does. Ghidra names are given in [brackets] so you can cross-reference.
 *
 * Pin "handles" are RAM variables filled in by the scatter loader (compressed RW
 * data, so the export cannot show their values). The physical pins were matched
 * from the live GPIO monitoring; where two handles have identical behaviour the
 * assignment between them is arbitrary and noted as such.
 *
 * Findings that matter for the controller are collected at the bottom.
 */

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------------
 * Firmware input numbering is the REVERSE of the front-panel labels.
 * Keys are scanned in the order PA2, PA1, PA0, PF1 = panel keys 4, 3, 2, 1.
 *
 *   index 0 = input 4      index 1 = input 3      index 2 = input 2      index 3 = input 1
 *   input = 4 - index
 * --------------------------------------------------------------------------------- */

/* ---- pin handles: RAM 0x200001dc .. 0x20000234 --------------------------------- */
/* keys, active low, internal pull-ups                                  [DAT_200001dc..f2] */
static const struct { GPIO_TypeDef *port; uint16_t pin; } KEY[4] = {
    { GPIOA, PIN(2) },      /* index 0: panel key 4 */
    { GPIOA, PIN(1) },      /* index 1: panel key 3 */
    { GPIOA, PIN(0) },      /* index 2: panel key 2 */
    { GPIOF, PIN(1) },      /* index 3: panel key 1 */
};

/* six output lines written together on every selection                 [DAT_200001f8..21c] */
enum { L1, L2, L3, L4, L5, L6 };
static const struct { GPIO_TypeDef *port; uint16_t pin; } LINE[6] = {
    [L1] = { GPIOA, PIN(3) },   /* charlieplex common line for the key LEDs           */
    [L2] = { GPIOA, PIN(4) },   /* L2, L5, L6 are electrically identical (odd/even    */
    [L3] = { GPIOA, PIN(7) },   /*   select copies + LED line X); which handle is    */
    [L4] = { GPIOB, PIN(3) },   /*   which of PA4/PA6/PB6 cannot be told apart.      */
    [L5] = { GPIOA, PIN(6) },   /* L3 = LED line Z. L4 = pair select (1-2 vs 3-4).  */
    [L6] = { GPIOB, PIN(6) },
};

/* three switching-sequence outputs                                     [DAT_20000220..234] */
#define HUB_RESET_N   GPIOF, PIN(0)   /* [220/224] low for the whole switch window        */
#define VBUS_CUT      GPIOA, PIN(5)   /* [228/22c] high for the whole switch window       */
#define MODE_OUT      GPIOB, PIN(0)   /* [230/234] = !mode_flag, function not yet traced  */

/* ---- state: RAM 0x200000dc .. 0x20000238 --------------------------------------- */
#define NO_REQUEST      0xFF
#define REQ_TOGGLE_MODE 4              /* the hidden long-press action                  */

static uint32_t settings[64];          /* [DAT_200000dc] 256 bytes = 2 flash pages.
                                          settings[0] = 0xFFFF0000 | mode_flag<<8 | index
                                          settings[1..63] = the Puya flash-example test
                                          pattern, never touched: that is the walking
                                          0123456789ABCDEF / AAAAAAAA / 55555555 block
                                          you saw at 0x08003050 in the dump.           */
static uint8_t  current;               /* [DAT_200001f6] selected index               */
static uint8_t  applied;               /* [DAT_200001f7] index the lines were last written for */
static uint8_t  request;               /* [DAT_200001f5] pending index, 4, or NO_REQUEST */
static bool     held_lockout;          /* [DAT_200001f4] set after a long press until all keys are up */
static bool     mode_flag;             /* [DAT_20000236] persisted in settings bit 8   */
static uint16_t switch_timer;          /* [DAT_20000238] main-loop iterations remaining */

/* ---- entry ------------------------------------------------------------------------ */
int main(void)                                                     /* [FUN_080001e0] */
{
    for (uint8_t i = 0; i < 48; i++)                 /* vector table to RAM, VTOR = 0x20000000 */
        ((uint32_t *)0x20000000)[i] = ((const uint32_t *)0x08000000)[i];
    SCB->VTOR = 0x20000000;
    app_main();                                                    /* [FUN_080025f8] */
    return 0;
}

void app_main(void)                                                /* [FUN_080025f8] */
{
    HAL_Init();                    /* [FUN_08000ed4] SysTick 1 ms, HAL_MspInit           */
    clock_config();                /* [FUN_080002b0] HSI 8 MHz with factory trim, AHB/APB /1 */
    uart_init();                   /* [FUN_0800030c] USART1 115200 8N1 on PA2/PA3, see below */
    gpio_init();                   /* [FUN_080002a0]                                    */
    state_init();                  /* [FUN_08001e10]                                    */
    HAL_UART_Receive_IT(&huart1, uart_rx, 1);        /* [FUN_08000408] never fires, see below */

    for (;;) {
        scan_keys();               /* [FUN_080005b4] */
        apply_selection();         /* [FUN_08001e70] */
        switch_sequence();         /* [FUN_08001a74] */
    }
}

void gpio_init(void)                                               /* [FUN_080002a0] */
{
    /* [FUN_08001c78] nine outputs: LINE[0..5], HUB_RESET_N, VBUS_CUT, MODE_OUT, push-pull */
    /* [FUN_08001bf8] four key inputs, pull-up, plus held_lockout = 0                       */
    /* [FUN_08002034] GPIOB BSRR = PIN(1): sets ODR on PB1, which is left in analog mode,
                      so it does nothing on this board                                      */
    switch_timer = 0xFFF;          /* hold the hub in reset ~120 ms after power-up       */
    mode_flag    = true;           /* immediately overwritten by state_init()            */
}

void state_init(void)                                              /* [FUN_08001e10] */
{
    settings[0] = 0xFFFF0002;
    current     = 2;               /* index 2 = panel input 2: the power-on default      */
    mode_flag   = false;
    request     = NO_REQUEST;
    applied     = NO_REQUEST;      /* forces apply_selection() to write the lines once   */
}

/* ---- key scan ------------------------------------------------------------------ */
/* Runs once per loop. Blocking: it sits in HAL_Delay() while a key is down.
 *   20 ms of continuous press = accepted (20 x 1 ms polls).
 *   Key of a NON-selected input: wait for release, then request that index.
 *   Key of the SELECTED input:   released within 1 s = ignored;
 *                                held 1 s (100 x 10 ms) = request 4 (toggle mode),
 *                                and lock out further scanning until all keys are up. */
void scan_keys(void)                                               /* [FUN_080005b4] */
{
    if (held_lockout) {
        if (all_keys_released()) held_lockout = false;
        return;
    }
    for (uint8_t k = 0; k < 4; k++) {
        if (!key_down(k)) continue;

        uint8_t n = 0;
        while (key_down(k)) {
            if (++n > 0x13) {                            /* 20 ms debounce reached */
                if (current != k) {
                    while (key_down(k)) ;                /* firmware acts on release */
                    request = k;
                    return;
                }
                n = 0;
                for (;;) {                               /* long-press timer on the active key */
                    if (!key_down(k)) return;            /* short press on active key: nothing */
                    if (++n > 99) break;                 /* ~1 s */
                    HAL_Delay(10);
                }
                held_lockout = true;
                request = REQ_TOGGLE_MODE;
                return;
            }
            HAL_Delay(1);
        }
        return;                                          /* one key per pass */
    }
}

/* ---- LED / mux lines ----------------------------------------------------------- */
/* Written whenever `current` changes. Values per firmware index (0 = input 4 ... 3 = input 1).
 * Read against the panel (input 1..4) this is the table measured over SWD:
 *      PB3 (L4): 1 1 0 0   pair select
 *      PA4 etc:  1 0 1 0   odd/even select, three copies
 *      PA3 (L1): 0 1 1 0   LED common
 *      PA7 (L3): 0 1 0 1   LED line Z                                                  */
void apply_selection(void)                                         /* [FUN_08001e70] */
{
    static const uint8_t table[4][6] = {     /*  L1 L2 L3 L4 L5 L6 */
        /* index 0, input 4 */              {   0, 0, 1, 0, 0, 0 },
        /* index 1, input 3 */              {   1, 1, 0, 0, 1, 1 },
        /* index 2, input 2 */              {   1, 0, 1, 1, 0, 0 },
        /* index 3, input 1 */              {   0, 1, 0, 1, 1, 1 },
    };
    if (current == applied) return;
    applied = current;
    for (uint8_t i = 0; i < 6; i++)
        HAL_GPIO_WritePin(LINE[i].port, LINE[i].pin, table[current][i]);
}

/* ---- switching sequence -------------------------------------------------------- */
/* Non-blocking: the timer is decremented once per main-loop pass. 0x4FFF passes
 * measured ~580 ms, i.e. about 28 us per loop at 8 MHz.
 *
 * NOTE: Ghidra shows a call to FUN_08002450 here. That is not a function, it is Keil's
 * __ARM_common_switch8 jump-table helper: a switch(request) whose case bodies Ghidra
 * did not recover. The case 0..3 path below is what the decompiler did show; the
 * case 4 behaviour (toggle mode_flag, keep the current input, still run the full
 * sequence) is inferred from the code that follows and from what the board does.  */
void switch_sequence(void)                                         /* [FUN_08001a74] */
{
    if (switch_timer && --switch_timer == 0) {           /* end of the switch window */
        HAL_GPIO_WritePin(HUB_RESET_N, 1);
        HAL_GPIO_WritePin(VBUS_CUT,    0);
        HAL_GPIO_WritePin(MODE_OUT,    mode_flag ? 0 : 1);
    }

    if (request == NO_REQUEST) return;

    switch (request) {                                   /* [FUN_08002450] */
    case 0: case 1: case 2: case 3:
        current = request;
        break;
    case REQ_TOGGLE_MODE:                                /* inferred */
        mode_flag = !mode_flag;
        break;
    }
    HAL_GPIO_WritePin(HUB_RESET_N, 0);                   /* start the window */
    HAL_GPIO_WritePin(VBUS_CUT,    1);
    switch_timer = 0x4FFF;

    settings[0] = 0xFFFF0000u | (mode_flag ? 0x100u : 0) | current;
    save_settings();                                     /* [FUN_08002414] */
    request = NO_REQUEST;
}

/* ---- settings ---------------------------------------------------------------------- */
/* Written on EVERY switch, but never read back: nothing in the image reads 0x08003000.
 * Power-on always starts on index 2 (panel input 2).                                   */
void save_settings(void)                                           /* [FUN_08002414] */
{
    HAL_FLASH_Unlock();                                  /* [FUN_08000ca8] */
    HAL_FLASHEx_Erase(PAGE_ERASE, 0x08003000, 2 pages);  /* [FUN_0800022c] */
    HAL_FLASH_Program(PAGE, 0x08003000, &settings[0]);   /* [FUN_08000258] 128 bytes */
    HAL_FLASH_Program(PAGE, 0x08003080, &settings[32]);  /*                128 bytes */
    HAL_FLASH_Lock();                                    /* [FUN_08000acc] */
}

/* ---- UART: serial control protocol, works on this hub ------------------------------- */
/* uart_init() [FUN_0800030c] brings up USART1 at 115200 8N1 and its MspInit
 * [FUN_08001758] puts PA2 (TX) and PA3 (RX) into AF1. The reading below that
 * gpio_init() takes those pins away is wrong for this board: tested over an FTDI on
 * 2026-10-24, the hub echoes and switches on UART frames.
 *
 * Flow: app_main's [FUN_08000408] is HAL_UART_Transmit_IT of 1 byte, not a receive.
 * Its TX-complete [FUN_08001a54] arms Receive_IT(3); RX-complete [FUN_08001874]
 * echoes the 3 bytes and parses them; that echo's TX-complete re-arms the receive.
 *
 * Frame: 3 bytes, { 0xAA, index, checksum } with (0xAA + index + checksum) & 0xFF == 0,
 * i.e. checksum = (0x56 - index) & 0xFF. index 4 toggles the mode flag (untested).
 * Measured on the panel, UART index maps as input = index + 1, NOT the 4 - index of
 * the key-scan numbering at the top of this file (that conflict is unresolved):
 *   AA 00 56 = input 1, AA 01 55 = input 2, AA 02 54 = input 3, AA 03 53 = input 4.
 * Observed behaviour:
 *   - frames starting with 0xAA are echoed byte for byte, valid checksum or not;
 *     frames not starting with 0xAA get no echo but the receiver keeps working.
 *   - stray bytes or the wrong baud wedge the receiver (likely an overrun ending the
 *     HAL receive, which nothing re-arms) until the hub is power-cycled. Send only
 *     whole 3-byte frames, one at a time.
 *   - at power-up the line shows 00 80 00 00 80 (boot byte plus power-on noise).  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h)                /* [FUN_08001874] */
{
    HAL_UART_Receive_IT(h, uart_frame, 3);
    if (uart_frame[0] == 0xAA && (uint8_t)(uart_frame[1] + 0xAA + uart_frame[2]) == 0)
        request = uart_frame[1];
    else
        request = NO_REQUEST;
}
/* HAL_UART_ErrorCallback [FUN_08001548] printf("Uart Error, ErrorCode = %d\r\n") is the
 * only string in the image; the printf [FUN_08002430/FUN_0800248c] also goes to USART1. */

/* =====================================================================================
 * What this means for the controller
 * =====================================================================================
 * 1. Selection state is exactly the two bits already decoded over SWD:
 *      pair    = PB3   (1 = inputs 1-2, 0 = inputs 3-4)
 *      odd     = PA4   (1 = inputs 1 or 3)
 *      input   = (pair ? 0 : 2) + (odd ? 1 : 2)
 *    or, from the firmware's own variable: input = 4 - *(uint8_t *)0x200001f6.
 *    The "switch in progress" window is PF0 low / PA5 high, ~580 ms, hub reset plus
 *    device power cut, and it also runs after a mode toggle.
 *
 * 2. Simulated presses: >= 20 ms down then release, on the key of a non-selected input.
 *    Keep them well under 1 s: holding the ACTIVE input's key for 1 s toggles the mode
 *    flag and flips PB0. A 100 ms press on the active key is ignored, which is what you
 *    want from the probe's 1-4 commands.
 *
 * 3. The write-only settings page means "remembers the last input" does not work: it
 *    always boots to input 2. A power-cycle from input 4 confirms it. Cheap to check.
 *
 * 4. PB0 / mode_flag is the one open question: it is persisted, toggled by the hidden
 *    long press, and drives a static output. Trace PB0 to see what it feeds (U7, the
 *    hub, the audio path), or toggle it with a 1 s hold on the active key and watch
 *    what changes; the d monitor will show PB0 flipping at the end of the window.
 * ===================================================================================== */