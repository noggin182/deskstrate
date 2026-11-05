/*
 * kvm_proto.h - wire format shared by the core, the deck and the PC/Mac daemons.
 * C++ only (the Mac daemon imports it through Swift's C++ interop).
 *
 * The core is the source of truth; everyone else asks. UDP, KVM_PORT on every node,
 * and every datagram is 6 bytes: a 2-byte magic then the 4 state bytes.
 *
 *   "KS" STATE    core -> all. Broadcast on every change and every KVM_HEARTBEAT_MS,
 *                 and sent straight back to whoever sent a REQUEST.
 *   "KR" REQUEST  deck/hosts -> core. The state wanted; 0xFF bytes are left alone, so
 *                 KR FF FF FF FF changes nothing and just asks for a STATE.
 *
 * A REQUEST is idempotent: a sender that doesn't see its change in a STATE simply sends
 * it again.
 *
 * State bytes, indexed by enum kvm_slot:
 *   monitors  bits 0-3 = the host on it (enum kvm_host), bit 7 = standby, bits 4-6 = 0
 *   hubs      the selected port, 0-3 (0 PC, 1 Mac, 2 and 3 guests)
 *   0xFF      unknown (not connected) in a STATE; leave alone in a REQUEST
 *
 * Examples: everything on the PC = 00 00 00 00, everything on the Mac = 01 01 01 01,
 * a REQUEST of 00 FF FF FF moves only the left monitor to the PC.
 *
 * In a REQUEST a monitor's low nibble 0xF keeps its input, and bit 7 is ignored for now:
 * the core reports standby but doesn't change monitor power.
 *
 * No authentication: this trusts the LAN.
 */
#ifndef KVM_PROTO_H
#define KVM_PROTO_H

#include <stdint.h>
#include <string.h>

#define KVM_PORT          18200u    /* IANA unassigned (18188-18240) */
#define KVM_HEARTBEAT_MS  2000u
#define KVM_TIMEOUT_MS    5000u     /* no STATE for this long: the core is gone */

#define KVM_MAGIC         0x4Bu     /* 'K' */
#define KVM_MSG_LEN       6u

enum kvm_type {
    KVM_STATE   = 0x53,  /* 'S' */
    KVM_REQUEST = 0x52,  /* 'R' */
};

enum kvm_slot {
    KVM_MON_LEFT  = 0,
    KVM_MON_RIGHT = 1,
    KVM_HUB_HID   = 2,  /* keyboard + mouse: follows the host being worked on */
    KVM_HUB_MEDIA = 3,  /* camera, headset, sound card: stays put across switches (calls) */
    KVM_SLOTS     = 4,
};

enum kvm_host {
    KVM_HOST_PC    = 0,  /* monitor: DisplayPort */
    KVM_HOST_MAC   = 1,  /* monitor: USB-C */
    KVM_HOST_GUEST = 3,  /* monitor: HDMI */
};

#define KVM_UNKNOWN    0xFFu  /* whole byte: unknown (STATE) / leave alone (REQUEST) */
#define KVM_STANDBY    0x80u  /* monitor byte: in standby */
#define KVM_HOST_MASK  0x0Fu  /* monitor byte: the host */
#define KVM_HOST_KEEP  0x0Fu  /* monitor host nibble: unknown (STATE) / keep (REQUEST) */

typedef struct {
    uint8_t magic;              /* KVM_MAGIC */
    uint8_t type;               /* enum kvm_type */
    uint8_t b[KVM_SLOTS];       /* enum kvm_slot */
} kvm_msg_t;

static_assert(sizeof(kvm_msg_t) == KVM_MSG_LEN, "kvm_msg_t layout");

static inline kvm_msg_t kvm_msg(uint8_t type, const uint8_t b[KVM_SLOTS]) {
    kvm_msg_t m;
    m.magic = (uint8_t)KVM_MAGIC;
    m.type = type;
    memcpy(m.b, b, KVM_SLOTS);
    return m;
}

/* Decodes a received datagram: 1 if it is a kvm_proto message of that type. */
static inline int kvm_decode(const void *buf, int len, uint8_t type, kvm_msg_t *out) {
    if (len != (int)KVM_MSG_LEN) return 0;
    memcpy(out, buf, KVM_MSG_LEN);
    return out->magic == KVM_MAGIC && out->type == type;
}

/* The host shown on a monitor, or KVM_HOST_KEEP if unknown or not connected. */
static inline uint8_t kvm_mon_host(uint8_t b) {
    return b == KVM_UNKNOWN ? KVM_HOST_KEEP : (uint8_t)(b & KVM_HOST_MASK);
}

static inline int kvm_mon_standby(uint8_t b) {
    return b != KVM_UNKNOWN && (b & KVM_STANDBY) != 0;
}

#endif /* KVM_PROTO_H */
