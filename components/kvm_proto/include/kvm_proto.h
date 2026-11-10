/*
 * kvm_proto.h - wire format shared by the core, the deck and the PC/Mac daemons.
 * C++ only (the Mac daemon imports it through Swift's C++ interop).
 *
 * The core is the source of truth; everyone else asks. UDP, KVM_PORT on every node,
 * and every datagram is 6 bytes: a 2-byte magic then 4 bytes (the state bytes, but
 * for VOLUME).
 *
 *   "KS" STATE    core -> all. Broadcast on every change and every KVM_HEARTBEAT_MS,
 *                 and sent straight back to whoever sent a REQUEST.
 *   "KR" REQUEST  deck/keyboard/hosts -> core. The state wanted; 0xFF bytes are left alone, so
 *                 KR FF FF FF FF changes nothing and just asks for a STATE.
 *
 *   "KV" VOLUME   keyboard ESP -> hosts, broadcast. [host, action, 0xFF, 0xFF]: host
 *                 is enum kvm_host (PC or Mac), action enum kvm_volume. Only the daemon
 *                 on that host acts on it; the core ignores it. Broadcast because the
 *                 Mac's VPN keeps it from receiving unicast from the LAN. Not resent:
 *                 a lost one is a missed step (resending a mute toggle could undo it).
 *
 * A REQUEST is idempotent: a sender that doesn't see its change in a STATE simply sends
 * it again.
 *
 * State bytes, indexed by enum kvm_slot:
 *   monitors  bits 0-3 = the host on it (enum kvm_host), bit 7 = standby (below),
 *             bits 4-6 = 0
 *   hubs      the selected port, 0-3 (0 PC, 1 Mac, 2 and 3 guests)
 *   0xFF      unknown (not connected) in a STATE; leave alone in a REQUEST
 *
 * Examples: everything on the PC = 00 00 00 00, everything on the Mac = 01 01 01 01,
 * a REQUEST of 00 FF FF FF moves only the left monitor to the PC.
 *
 * In a REQUEST a monitor's low nibble 0xF keeps its input.
 *
 * Standby is the desk's sleep: what the core has been asked for, not the monitors' power
 * read back (the core can't put a monitor showing DP or USB-C into standby: its DDC comes
 * in over HDMI, which a Dell ignores for standby unless HDMI is showing). The core sets
 * bit 7 on every connected monitor in STATE while the desk is asleep. A REQUEST with bit
 * 7 set in a monitor byte puts the desk to sleep (KR 8F 8F FF FF: inputs kept); any other
 * REQUEST that isn't all 0xFF wakes it (KR 0F 0F FF FF just wakes), so asking for an
 * input wakes everything. Nobody acts on a change: every listener follows each STATE.
 *   hosts     the monitors showing their input go to DDC standby while asleep and back
 *             on while not (the core's DDC comes in over HDMI, which a Dell ignores for
 *             standby unless HDMI is showing)
 *   core      the same for a monitor showing HDMI (a guest); it wakes one only when the
 *             desk wakes, since a guest that sends no signal would put it back to sleep
 *   deck, keyboard  dim their lights, and a key press wakes the desk
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
    KVM_VOLUME  = 0x56,  /* 'V' */
};

enum kvm_volume {
    KVM_VOL_UP   = 1,
    KVM_VOL_DOWN = 2,
    KVM_VOL_MUTE = 3,  /* toggle */
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
#define KVM_STANDBY    0x80u  /* monitor byte: the desk is asleep (STATE) / sleep it (REQUEST) */
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

/* STATE: the desk is asleep. */
static inline int kvm_asleep(const uint8_t b[KVM_SLOTS]) {
    return kvm_mon_standby(b[KVM_MON_LEFT]) || kvm_mon_standby(b[KVM_MON_RIGHT]);
}

/* REQUEST: 1 = sleep, 0 = wake, -1 = neither (all 0xFF, just asks for a STATE). */
static inline int kvm_request_sleep(const uint8_t b[KVM_SLOTS]) {
    int any = 0;
    for (unsigned i = 0; i < KVM_SLOTS; i++) any |= b[i] != KVM_UNKNOWN;
    if (!any) return -1;
    return kvm_asleep(b);
}

/* REQUEST bytes for sleeping / waking the desk without moving anything. */
#define KVM_SLEEP_REQUEST {KVM_STANDBY | KVM_HOST_KEEP, KVM_STANDBY | KVM_HOST_KEEP, KVM_UNKNOWN, KVM_UNKNOWN}
#define KVM_WAKE_REQUEST  {KVM_HOST_KEEP, KVM_HOST_KEEP, KVM_UNKNOWN, KVM_UNKNOWN}

/* True once a STATE shows everything a REQUEST asked for: each monitor's host (unless
 * kept), each hub's port, and asleep or awake. */
static inline int kvm_shows(const uint8_t state[KVM_SLOTS], const uint8_t want[KVM_SLOTS]) {
    for (unsigned i = 0; i < KVM_SLOTS; i++) {
        if (want[i] == KVM_UNKNOWN) continue;
        if (i == KVM_MON_LEFT || i == KVM_MON_RIGHT) {
            if (kvm_mon_host(want[i]) != KVM_HOST_KEEP && kvm_mon_host(state[i]) != kvm_mon_host(want[i])) return 0;
        } else if (state[i] != want[i]) {
            return 0;
        }
    }
    const int sleep = kvm_request_sleep(want);
    return sleep < 0 || kvm_asleep(state) == sleep;
}

#endif /* KVM_PROTO_H */
