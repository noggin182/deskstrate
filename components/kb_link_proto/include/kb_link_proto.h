/*
 * kb_link_proto.h - the wire link between the keyboard's STM32 (firmware/keyboard) and
 * the ESP32 inside it (firmware/kb). Plain C, shared by both: build.sh copies it into
 * the QMK keymap.
 *
 * Physical: two wires, 8N1 at KB_LINK_BAUD, 3.3 V, 1 kOhm in series on each.
 *   keyboard PB12 -> ESP RX   bit-banged by the STM32 (PB12 has no UART), received
 *                             by the ESP's hardware UART
 *   ESP TX -> keyboard PA3    ESP hardware UART, received by the STM32's USART2 (DMA)
 * The keyboard holds PB12 high (UART idle) while it is in cable mode with USB power,
 * and low otherwise. The ESP reads that level as "keyboard on USB": it stays awake
 * while it is high and deep-sleeps after it has been low a while (firmware/kb/src/sleep.h).
 *
 * Framing, both directions:
 *   0xA5  type  seq  len  payload[len]  crc
 *   crc = CRC-8 (poly 0x07, init 0x00) over type, seq, len and the payload.
 * A receiver hunts for 0xA5, reads the header, and drops the frame if len is too big,
 * a byte is late (KB_LINK_BYTE_GAP_MS), or the CRC is wrong; then it hunts again. So a
 * corrupted byte costs one frame, never a wrong action.
 *
 * Reliability: KR_KVM_ACTION is acknowledged. The ESP answers every good one with
 * EK_ACK carrying its seq; the keyboard resends it (same seq) every KB_LINK_ACK_MS until
 * acked, up to KB_LINK_TRIES times. The ESP acts on a seq once and re-acks repeats.
 * Everything else is sent periodically or on change and simply replaced by the next one.
 * seq counts up per sender, wrapping; unacked frames carry it too (for loss stats).
 */
#ifndef KB_LINK_PROTO_H
#define KB_LINK_PROTO_H

#include <stdint.h>

#define KB_LINK_BAUD         57600u
#define KB_LINK_SYNC         0xA5u
#define KB_LINK_MAX_PAYLOAD  32u
#define KB_LINK_MAX_FRAME    (5u + KB_LINK_MAX_PAYLOAD)
#define KB_LINK_BYTE_GAP_MS  20u    /* a frame's bytes arrive back to back */
#define KB_LINK_ACK_MS       50u
#define KB_LINK_TRIES        5u
#define KB_LINK_STATUS_MS    2000u  /* keyboard -> ESP status heartbeat */
#define KB_LINK_VERSION      2u   /* 2: KR_KVM_ACTION replaced KR_KVM_REQUEST */

/* keyboard -> ESP */
enum kb_link_kr_type {
    KR_HELLO        = 0x01, /* [version] + text: sent when the link (re)starts */
    KR_KVM_ACTION   = 0x02, /* 1 byte: enum kb_link_action, a KVM key pressed; acked */
    KR_STATUS       = 0x03, /* struct kb_link_status */
    KR_TEXT         = 0x04, /* text, for the ESP's log and web page */
};

/* KR_KVM_ACTION: what a KVM key asks for. The ESP works out the request from the core's
 * latest STATE with components/kvm_controls (the same rules as the deck's keys). */
enum kb_link_action {
    KB_ACT_LEFT       = 1, /* deck bottom-left: all to PC; split: keyboard to the left screen */
    KB_ACT_RIGHT      = 2, /* deck bottom-right: all to Mac; split: keyboard to the right screen */
    KB_ACT_SPLIT      = 3, /* enter / leave split */
    KB_ACT_SWAP       = 4, /* swap the monitors; the media hub stays with its screen */
    KB_ACT_OTHER_VOLU = 5, /* volume up on the PC/Mac that doesn't have the keyboard */
    KB_ACT_OTHER_VOLD = 6, /* volume down, same host */
    KB_ACT_OTHER_MUTE = 7, /* mute toggle, same host */
    KB_ACT_SLEEP      = 8, /* both hosts put their displays to sleep (kvm_proto SLEEP) */
};

/* ESP -> keyboard */
enum kb_link_ek_type {
    EK_ACK          = 0x81, /* 1 byte: the seq being acknowledged */
    EK_KVM_STATE    = 0x82, /* 4 bytes: the core's latest kvm_proto STATE */
    EK_TEXT         = 0x83, /* text; the keyboard echoes it back as KR_TEXT "echo: ..." */
};

/* KR_STATUS payload, little-endian, packed. */
struct __attribute__((packed)) kb_link_status {
    uint8_t  transport;      /* Keychron transport_t: 1 USB, 2 Bluetooth, 4 2.4 GHz */
    uint8_t  usb_power;      /* 1 if USB power is present */
    uint8_t  host_os;        /* QMK os_variant_t: 0 unsure, 1 Linux, 2 Windows, 3 macOS, 4 iOS */
    uint8_t  base_layer;     /* default layer: 0 Mac, 2 Windows */
    uint8_t  battery_pct;    /* 0-100 */
    uint8_t  reserved;
    uint16_t resends;        /* KVM requests sent again for want of an ack */
    uint16_t rx_bad;         /* frames from the ESP dropped (CRC, length, timeout) */
    uint16_t rx_good;        /* frames from the ESP accepted */
    uint32_t uptime_s;
};

#ifdef __cplusplus
static_assert(sizeof(struct kb_link_status) == 16, "kb_link_status layout");
#else
_Static_assert(sizeof(struct kb_link_status) == 16, "kb_link_status layout");
#endif

static inline uint8_t kb_link_crc8(uint8_t crc, const uint8_t *p, uint8_t n) {
    while (n--) {
        crc ^= *p++;
        for (uint8_t i = 0; i < 8; i++) crc = (uint8_t)((crc & 0x80u) ? ((unsigned)crc << 1) ^ 0x07u : (unsigned)crc << 1);
    }
    return crc;
}

/* Writes a frame into out (at least KB_LINK_MAX_FRAME bytes); returns its length, or 0
 * if the payload is too long. */
static inline uint8_t kb_link_encode(uint8_t *out, uint8_t type, uint8_t seq, const void *payload, uint8_t len) {
    if (len > KB_LINK_MAX_PAYLOAD) return 0;
    out[0] = KB_LINK_SYNC;
    out[1] = type;
    out[2] = seq;
    out[3] = len;
    for (uint8_t i = 0; i < len; i++) out[4 + i] = ((const uint8_t *)payload)[i];
    out[4 + len] = kb_link_crc8(0, out + 1, (uint8_t)(3 + len));
    return (uint8_t)(5 + len);
}

/* Receiver: feed bytes one at a time with the time in ms; returns 1 when a good frame
 * is complete (type/seq/len/payload valid until the next call), 0 otherwise. Counts
 * dropped frames in bad. */
struct kb_link_rx {
    uint8_t  buf[KB_LINK_MAX_FRAME];
    uint8_t  n;
    uint32_t last_ms;
    uint32_t bad;
    uint32_t good;
};

static inline int kb_link_feed(struct kb_link_rx *rx, uint8_t c, uint32_t now_ms) {
    if (rx->n && now_ms - rx->last_ms > KB_LINK_BYTE_GAP_MS) {
        rx->n = 0;
        rx->bad++;
    }
    rx->last_ms = now_ms;
    if (rx->n == 0) {
        if (c == KB_LINK_SYNC) rx->buf[rx->n++] = c;
        return 0;
    }
    rx->buf[rx->n++] = c;
    if (rx->n == 4 && rx->buf[3] > KB_LINK_MAX_PAYLOAD) {
        rx->n = 0;
        rx->bad++;
        return 0;
    }
    if (rx->n < 5 || rx->n < (uint8_t)(5 + rx->buf[3])) return 0;
    const uint8_t len = rx->buf[3];
    const int ok = kb_link_crc8(0, rx->buf + 1, (uint8_t)(3 + len)) == rx->buf[4 + len];
    rx->n = 0;
    if (ok) rx->good++; else rx->bad++;
    return ok;
}

#define KB_LINK_TYPE(rx)    ((rx)->buf[1])
#define KB_LINK_SEQ(rx)     ((rx)->buf[2])
#define KB_LINK_LEN(rx)     ((rx)->buf[3])
#define KB_LINK_PAYLOAD(rx) ((rx)->buf + 4)

#endif /* KB_LINK_PROTO_H */
