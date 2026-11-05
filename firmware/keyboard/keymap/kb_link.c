// See kb_link.h.
#include QMK_KEYBOARD_H
#include <string.h>
#include "kb_link.h"
#include "kb_link_proto.h"
#include "transport.h"
#include "lpm.h"
#include "battery.h"
#include "os_detection.h"
#include "version.h"

#define TX_PIN_BIT 12  // PB12
#define RX_BUF_SIZE 128u

static uint8_t           rx_dma_buf[RX_BUF_SIZE];
static uint16_t          rx_tail;
static struct kb_link_rx rx;
static bool              up;
static uint8_t           tx_seq;
static uint32_t          status_at;
static uint16_t          resends;

// The KVM request waiting for its ack.
static bool     kvm_pending;
static uint8_t  kvm_state[4];
static uint8_t  kvm_seq;
static uint8_t  kvm_tries;
static uint32_t kvm_sent_at;

// The core's latest STATE, as the ESP passed it on (not used yet).
static uint8_t core_state[4] = {0xFF, 0xFF, 0xFF, 0xFF};

// ---------------------------------------------------------------- TX: bit-banged on PB12

static void tx_line(bool high) {
    GPIOB->BSRR.W = high ? (1u << TX_PIN_BIT) : (1u << (TX_PIN_BIT + 16));
}

static void tx_byte(uint8_t b) {
    const uint32_t bit = STM32_HCLK / KB_LINK_BAUD;  // CPU cycles per bit (833 at 48 MHz)
    // start bit, 8 data bits LSB first, stop bit
    uint16_t frame = (uint16_t)((b << 1) | (1u << 9));
    chSysLock();
    uint32_t t = DWT->CYCCNT;
    for (uint8_t i = 0; i < 10; i++) {
        tx_line(frame & 1u);
        frame >>= 1;
        t += bit;
        while ((int32_t)(DWT->CYCCNT - t) < 0) {
        }
    }
    chSysUnlock();
}

static void send_frame(uint8_t type, uint8_t seq, const void *payload, uint8_t len) {
    uint8_t buf[KB_LINK_MAX_FRAME];
    const uint8_t n = kb_link_encode(buf, type, seq, payload, len);
    for (uint8_t i = 0; i < n; i++) tx_byte(buf[i]);
}

static void send_text(const char *text) {
    send_frame(KR_TEXT, tx_seq++, text, (uint8_t)strnlen(text, KB_LINK_MAX_PAYLOAD));
}

// ---------------------------------------------------------------- RX: USART2 + DMA on PA3

static void rx_start(void) {
    // PA3 as USART2_RX (AF7), pulled up so a sleeping ESP (TX floating) reads as idle.
    palSetLineMode(A3, PAL_MODE_ALTERNATE(7) | PAL_STM32_PUPDR_PULLUP);
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN;
    (void)RCC->AHB1ENR;

    // USART2_RX is DMA1 stream 5, channel 4: circular into rx_dma_buf, no interrupts.
    DMA1_Stream5->CR = 0;
    while (DMA1_Stream5->CR & DMA_SxCR_EN) {
    }
    DMA1->HIFCR = DMA_HIFCR_CTCIF5 | DMA_HIFCR_CHTIF5 | DMA_HIFCR_CTEIF5 | DMA_HIFCR_CDMEIF5 | DMA_HIFCR_CFEIF5;
    DMA1_Stream5->PAR  = (uint32_t)&USART2->DR;
    DMA1_Stream5->M0AR = (uint32_t)rx_dma_buf;
    DMA1_Stream5->NDTR = RX_BUF_SIZE;
    DMA1_Stream5->FCR  = 0;
    DMA1_Stream5->CR   = (4u << DMA_SxCR_CHSEL_Pos) | DMA_SxCR_MINC | DMA_SxCR_CIRC | DMA_SxCR_EN;

    USART2->CR1 = 0;
    USART2->BRR = (STM32_PCLK1 + KB_LINK_BAUD / 2) / KB_LINK_BAUD;  // oversampling by 16
    USART2->CR2 = 0;
    USART2->CR3 = USART_CR3_DMAR;
    USART2->CR1 = USART_CR1_UE | USART_CR1_RE;  // receive only: PA2 stays Keychron's
    rx_tail = 0;
    memset(&rx, 0, sizeof rx);
}

static void rx_stop(void) {
    USART2->CR1 = 0;
    DMA1_Stream5->CR = 0;
    palSetLineMode(A3, PAL_MODE_INPUT_PULLUP);
}

static void handle_frame(void) {
    const uint8_t  type = KB_LINK_TYPE(&rx), len = KB_LINK_LEN(&rx);
    const uint8_t *p    = KB_LINK_PAYLOAD(&rx);
    switch (type) {
        case EK_ACK:
            if (len == 1 && kvm_pending && p[0] == kvm_seq) kvm_pending = false;
            break;
        case EK_KVM_STATE:
            if (len == 4) memcpy(core_state, p, 4);
            break;
        case EK_TEXT: {
            char echo[KB_LINK_MAX_PAYLOAD + 1] = "echo: ";
            const uint8_t room = KB_LINK_MAX_PAYLOAD - 6;
            const uint8_t n    = len < room ? len : room;
            memcpy(echo + 6, p, n);
            echo[6 + n] = 0;
            send_text(echo);
            break;
        }
        default:
            break;
    }
}

static void rx_poll(void) {
    // A framing or overrun error doesn't stop the DMA; clear it so the USART keeps going
    // (SR then DR). The CRC drops whatever it corrupted.
    if (USART2->SR & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        (void)USART2->SR;
        (void)USART2->DR;
    }
    const uint16_t head = (uint16_t)(RX_BUF_SIZE - DMA1_Stream5->NDTR) % RX_BUF_SIZE;
    const uint32_t now  = timer_read32();
    while (rx_tail != head) {
        if (kb_link_feed(&rx, rx_dma_buf[rx_tail], now)) handle_frame();
        rx_tail = (uint16_t)((rx_tail + 1) % RX_BUF_SIZE);
    }
}

// ---------------------------------------------------------------- link

static void send_status(void) {
    struct kb_link_status s = {
        .transport   = (uint8_t)get_transport(),
        .usb_power   = usb_power_connected() ? 1 : 0,
        .host_os     = (uint8_t)detected_host_os(),
        .base_layer  = (uint8_t)get_highest_layer(default_layer_state),
        .battery_pct = battery_get_percentage(),
        .resends     = resends,
        .rx_bad      = (uint16_t)rx.bad,
        .rx_good     = (uint16_t)rx.good,
        .uptime_s    = timer_read32() / 1000,
    };
    send_frame(KR_STATUS, tx_seq++, &s, sizeof s);
    status_at = timer_read32();
}

static void link_up(void) {
    // DWT cycle counter for the bit timing.
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    tx_line(true);
    palSetLineMode(B12, PAL_MODE_OUTPUT_PUSHPULL);
    rx_start();
    up      = true;
    tx_seq  = 0;
    wait_ms(2);  // a little idle before the first start bit
    char hello[KB_LINK_MAX_PAYLOAD];
    hello[0] = KB_LINK_VERSION;
    strncpy(hello + 1, "Q3 Max " QMK_BUILDDATE, sizeof hello - 1);
    send_frame(KR_HELLO, tx_seq++, hello, (uint8_t)(1 + strnlen(hello + 1, sizeof hello - 1)));
    send_status();
}

static void link_down(void) {
    up          = false;
    kvm_pending = false;
    rx_stop();
    tx_line(false);  // low: the ESP's "keyboard off USB"
    palSetLineMode(B12, PAL_MODE_OUTPUT_PUSHPULL);
}

void kb_link_task(void) {
    static bool started = false;
    const bool  want    = get_transport() == TRANSPORT_USB && usb_power_connected();
    if (!started || want != up) {
        started = true;
        if (want) {
            link_up();
        } else {
            link_down();
        }
    }
    if (!up) return;

    rx_poll();
    if (kvm_pending && timer_elapsed32(kvm_sent_at) >= KB_LINK_ACK_MS) {
        if (kvm_tries >= KB_LINK_TRIES) {
            kvm_pending = false;
        } else {
            send_frame(KR_KVM_REQUEST, kvm_seq, kvm_state, 4);
            kvm_tries++;
            resends++;
            kvm_sent_at = timer_read32();
        }
    }
    if (timer_elapsed32(status_at) >= KB_LINK_STATUS_MS) send_status();
}

void kb_link_send_kvm_request(const uint8_t state[4]) {
    if (!up) return;
    memcpy(kvm_state, state, 4);
    kvm_seq     = tx_seq++;
    kvm_pending = true;
    kvm_tries   = 1;
    send_frame(KR_KVM_REQUEST, kvm_seq, kvm_state, 4);
    kvm_sent_at = timer_read32();
}
