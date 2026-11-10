/*
 * kvm_proto.h - wire format shared by the core, the deck, the keyboard's ESP and the
 * PC/Mac daemons. C++ only (the Mac daemon imports it through Swift's C++ interop).
 *
 * The core is the source of truth; everyone else asks. UDP, KVM_PORT on every node,
 * and every datagram is 6 printable ASCII bytes: 'K', a type letter, then 4 bytes.
 *
 *   "KS" STATE    core -> all. Broadcast on every change and every KVM_HEARTBEAT_MS,
 *                 and sent straight back to whoever sent a REQUEST.
 *   "KR" REQUEST  deck/keyboard/hosts -> core. The state wanted; '-' bytes are left
 *                 alone, so "KR----" changes nothing and just asks for a STATE.
 *   "KV" VOLUME   keyboard ESP -> hosts, broadcast. [host, action, '-', '-']: host 'P'
 *                 or 'M', action '+' up, '-' down, '/' mute toggle. Only the daemon on
 *                 that host acts on it; the core ignores it. Broadcast because the Mac's
 *                 VPN keeps it from receiving unicast from the LAN. Not resent: a lost
 *                 one is a missed step (resending a mute toggle could undo it).
 *
 * A REQUEST is idempotent: a sender that doesn't see its change in a STATE simply sends
 * it again.
 *
 * State bytes, indexed by enum kvm_slot:
 *   monitors  'P' PC (DisplayPort), 'M' Mac (USB-C), 'G' guest (HDMI),
 *             'X' input unreadable (STATE) / keep the input (REQUEST),
 *             lower case = standby (below): 'p' 'm' 'g' 'x'
 *             STATE only: '^' not connected, '~' not connected and the desk asleep
 *             ('^' | 0x20, like the letters); REQUEST only: '-' leave alone
 *   hubs      the selected port: 'P' PC (0), 'M' Mac (1), '1' guest 1 (2, on the left
 *             monitor's HDMI), '2' guest 2 (3, on the right monitor's HDMI),
 *             '-' not connected (STATE) / leave alone (REQUEST)
 * Any other byte, in any message type, makes kvm_decode() drop the whole datagram.
 *
 * Examples: everything on the PC = "KSPPPP", everything on the Mac = "KSMMMM", split and
 * asleep = "KSpmPM", "KRP---" moves only the left monitor to the PC.
 *
 * Standby is the desk's sleep: what the core has been asked for, not the monitors' power
 * read back (the core's DDC comes in over HDMI, which a Dell ignores for standby unless
 * HDMI is showing, so it can't put a monitor showing DP or USB-C into standby). The core
 * lower-cases every monitor byte in STATE while the desk is asleep ('^' becomes '~'). A REQUEST with a
 * lower-case monitor byte puts the desk to sleep ("KRxx--": inputs kept); any other
 * REQUEST that isn't "KR----" wakes it ("KRXX--" just wakes), so asking for an input
 * wakes everything. Nobody acts on a change: every listener follows each STATE.
 *   hosts     the monitors showing their input go to DDC standby while asleep and back
 *             on while not
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
    KVM_STATE   = 'S',
    KVM_REQUEST = 'R',
    KVM_VOLUME  = 'V',
};

enum kvm_volume {
    KVM_VOL_UP   = '+',
    KVM_VOL_DOWN = '-',
    KVM_VOL_MUTE = '/',  /* toggle */
};

enum kvm_slot {
    KVM_MON_LEFT  = 0,
    KVM_MON_RIGHT = 1,
    KVM_HUB_HID   = 2,  /* keyboard + mouse: follows the host being worked on */
    KVM_HUB_MEDIA = 3,  /* camera, headset, sound card: stays put across switches (calls) */
    KVM_SLOTS     = 4,
};

/* Monitor bytes (upper case: awake), and the PC / Mac in a hub byte or a VOLUME. */
enum kvm_host {
    KVM_HOST_PC    = 'P',  /* monitor: DisplayPort; hub port 0 */
    KVM_HOST_MAC   = 'M',  /* monitor: USB-C; hub port 1 */
    KVM_HOST_GUEST = 'G',  /* monitor: HDMI */
    KVM_HOST_KEEP  = 'X',  /* monitor: unreadable (STATE) / keep the input (REQUEST) */
};

/* An anonymous enum, not macros, so Swift imports them as plain integers. */
enum {
    KVM_HUB_GUEST1 = '1',  /* hub byte, port 2: the guest on the left monitor's HDMI */
    KVM_HUB_GUEST2 = '2',  /* hub byte, port 3: the guest on the right monitor's HDMI */
    KVM_UNKNOWN    = '-',  /* hub byte: not connected (STATE); any byte: leave alone (REQUEST) */
    KVM_STANDBY    = 0x20, /* monitor byte: lower case = the desk is asleep (STATE) / sleep it (REQUEST) */
    KVM_MON_NC     = '^',  /* monitor byte, STATE only: not connected */
    KVM_MON_NC_ASLEEP = '~',  /* monitor byte, STATE only: not connected, desk asleep (KVM_MON_NC | KVM_STANDBY) */
};

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

/* A monitor byte for a monitor the core can see (not '^' / '~', nor '-'). */
static inline int kvm_mon_connected(uint8_t b) {
    return b != KVM_MON_NC && b != KVM_MON_NC_ASLEEP && b != KVM_UNKNOWN;
}

/* The host shown on a monitor (upper case), or KVM_HOST_KEEP if unknown or not connected. */
static inline uint8_t kvm_mon_host(uint8_t b) {
    return kvm_mon_connected(b) ? (uint8_t)(b & ~KVM_STANDBY) : (uint8_t)KVM_HOST_KEEP;
}

/* Lower case: the desk is asleep (STATE) / put it to sleep (REQUEST). */
static inline int kvm_mon_standby(uint8_t b) {
    return (b >= 'a' && b <= 'z') || b == KVM_MON_NC_ASLEEP;
}

/* Hub byte <-> port number 0-3; kvm_hub_port() gives 0xFF for '-' or anything else. */
static inline uint8_t kvm_hub_port(uint8_t b) {
    switch (b) {
        case KVM_HOST_PC: return 0;
        case KVM_HOST_MAC: return 1;
        case KVM_HUB_GUEST1: return 2;
        case KVM_HUB_GUEST2: return 3;
    }
    return 0xFF;
}

static inline uint8_t kvm_hub_byte(uint8_t port) {
    static const uint8_t bytes[4] = {KVM_HOST_PC, KVM_HOST_MAC, KVM_HUB_GUEST1, KVM_HUB_GUEST2};
    return port < 4 ? bytes[port] : (uint8_t)KVM_UNKNOWN;
}

/* A STATE slot the core can see: a monitor not '^' / '~', a hub not '-'. */
static inline int kvm_connected(unsigned slot, uint8_t b) {
    return slot <= KVM_MON_RIGHT ? kvm_mon_connected(b) : b != KVM_UNKNOWN;
}

/* True if every byte is valid for that message type (the table at the top). */
static inline int kvm_valid(uint8_t type, const uint8_t b[KVM_SLOTS]) {
    if (type == KVM_VOLUME) {
        return (b[0] == KVM_HOST_PC || b[0] == KVM_HOST_MAC) &&
               (b[1] == KVM_VOL_UP || b[1] == KVM_VOL_DOWN || b[1] == KVM_VOL_MUTE) &&
               b[2] == KVM_UNKNOWN && b[3] == KVM_UNKNOWN;
    }
    if (type != KVM_STATE && type != KVM_REQUEST) return 0;
    for (unsigned i = KVM_MON_LEFT; i <= KVM_MON_RIGHT; i++) {
        if (type == KVM_STATE && (b[i] == KVM_MON_NC || b[i] == KVM_MON_NC_ASLEEP)) continue;
        if (type == KVM_REQUEST && b[i] == KVM_UNKNOWN) continue;
        if (!kvm_mon_connected(b[i])) return 0;  /* '-' in a STATE, '^' / '~' in a REQUEST */
        switch (kvm_mon_host(b[i])) {
            case KVM_HOST_PC: case KVM_HOST_MAC: case KVM_HOST_GUEST: case KVM_HOST_KEEP: break;
            default: return 0;
        }
    }
    for (unsigned i = KVM_HUB_HID; i <= KVM_HUB_MEDIA; i++) {
        if (b[i] != KVM_UNKNOWN && kvm_hub_port(b[i]) > 3) return 0;
    }
    return 1;
}

/* Decodes a received datagram: 1 if it is a valid kvm_proto message of that type. A
 * datagram with any byte out of the table is dropped whole. */
static inline int kvm_decode(const void *buf, int len, uint8_t type, kvm_msg_t *out) {
    if (len != (int)KVM_MSG_LEN) return 0;
    memcpy(out, buf, KVM_MSG_LEN);
    return out->magic == KVM_MAGIC && out->type == type && kvm_valid(type, out->b);
}

/* STATE: the desk is asleep. */
static inline int kvm_asleep(const uint8_t b[KVM_SLOTS]) {
    return kvm_mon_standby(b[KVM_MON_LEFT]) || kvm_mon_standby(b[KVM_MON_RIGHT]);
}

/* REQUEST: 1 = sleep, 0 = wake, -1 = neither (all '-', just asks for a STATE). */
static inline int kvm_request_sleep(const uint8_t b[KVM_SLOTS]) {
    int any = 0;
    for (unsigned i = 0; i < KVM_SLOTS; i++) any |= b[i] != KVM_UNKNOWN;
    if (!any) return -1;
    return kvm_asleep(b);
}

/* REQUEST bytes for sleeping / waking the desk without moving anything. */
#define KVM_SLEEP_REQUEST {'x', 'x', KVM_UNKNOWN, KVM_UNKNOWN}  /* KVM_HOST_KEEP, lower case */
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
