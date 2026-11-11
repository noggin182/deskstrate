/*
 * kvm_proto.h - wire format shared by the core, the deck, the keyboard's ESP and the
 * PC/Mac daemons. C++ only (the Mac daemon imports it through Swift's C++ interop).
 *
 * The core is the source of truth; everyone else asks. UDP, KVM_PORT on every node.
 * Every datagram is printable ASCII: 'K', a type letter, then fields separated by single
 * spaces. The length is fixed per type.
 *
 *   "KS LRHM pcv mcv"  STATE, 15 bytes. Core -> all. Broadcast on every change and every
 *                      KVM_HEARTBEAT_MS, and sent straight back to whoever sent a REQUEST.
 *   "KR LRHM pcv mcv"  REQUEST, 15 bytes. Deck/keyboard/hosts -> core. The state wanted;
 *                      '-' bytes are left alone, so "KR ---- --- ---" changes nothing and
 *                      just asks for a STATE. A host reports its own volume in its field.
 *   "KV hA"            VOLUME, 5 bytes. Keyboard ESP -> hosts, broadcast. Host 'P' or 'M',
 *                      action '+' up, '-' down, '/' mute toggle. Only the daemon on that
 *                      host acts on it (pressing its real volume key); the core ignores it.
 *                      Broadcast because the Mac can't receive unicast from the LAN. Not
 *                      resent: a lost one is a missed step (resending a toggle could undo it).
 *
 * A REQUEST is idempotent: a sender that doesn't see its change in a STATE simply sends
 * it again.
 *
 * LRHM, indexed by enum kvm_slot:
 *   monitors  'P' PC (DisplayPort), 'M' Mac (USB-C), 'G' guest (HDMI),
 *             'X' input unreadable (STATE) / keep the input (REQUEST),
 *             lower case = standby (below): 'p' 'm' 'g' 'x'
 *             STATE only: '^' not connected, '~' not connected and the desk asleep
 *             ('^' | 0x20, like the letters); REQUEST only: '-' leave alone
 *   hubs      the selected port: 'P' PC (0), 'M' Mac (1), '1' guest 1 (2, on the left
 *             monitor's HDMI), '2' guest 2 (3, on the right monitor's HDMI),
 *             '-' not connected (STATE) / leave alone (REQUEST)
 *
 * pcv, mcv: the PC's and the Mac's volume, 3 bytes each (enum kvm_level_host):
 *   "00%".."99%"  level 0-99, "**%" level 100 (the host's 0.0-1.0 volume x 100, rounded)
 *   "42/"         muted, at that level
 *   "---"         unknown (STATE) / leave alone (REQUEST)
 * Each host owns its own volume: it reports it with a REQUEST on every change, every
 * KVM_LEVEL_REPORT_MS, and whenever a STATE shows it wrong. The core keeps the last report
 * and drops it to "---" after KVM_LEVEL_EXPIRE_MS without one. Nobody sets a volume from
 * a STATE; KV is the only way to change one.
 *
 * Any other byte, in any message type, makes kvm_decode() drop the whole datagram.
 *
 * Examples: everything on the PC = "KS PPPP 42% ---", split and asleep with the Mac
 * muted at 42% = "KS pmPM 42% 42/", "KR P--- --- ---" moves only the left monitor to the PC,
 * "KR ---- --- 42%" is the Mac reporting 42%.
 *
 * Standby is the desk's sleep: what the core has been asked for, not the monitors' power
 * read back (the core's DDC comes in over HDMI, which a Dell ignores for standby unless
 * HDMI is showing, so it can't put a monitor showing DP or USB-C into standby). The core
 * lower-cases every monitor byte in STATE while the desk is asleep ('^' becomes '~'). A REQUEST with a
 * lower-case monitor byte puts the desk to sleep ("KR xx-- --- ---": inputs kept); any other
 * REQUEST whose slots aren't all '-' wakes it ("KR XX-- --- ---" just wakes), so asking for an input
 * wakes everything. Volume fields never sleep or wake the desk. Nobody acts on a change: every listener follows each STATE.
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
#define KVM_LEVEL_REPORT_MS  10000u /* a host re-reports its volume this often */
#define KVM_LEVEL_EXPIRE_MS  30000u /* the core forgets a host's volume after this long */

#define KVM_MAGIC         0x4Bu     /* 'K' */
#define KVM_STATE_LEN     15u       /* STATE and REQUEST */
#define KVM_VOLUME_LEN    5u
#define KVM_MAX_LEN       KVM_STATE_LEN

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

/* The volume fields of a STATE / REQUEST, in wire order. */
enum kvm_level_host {
    KVM_LEVEL_PC  = 0,
    KVM_LEVEL_MAC = 1,
    KVM_LEVELS    = 2,
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
    KVM_LEVEL_LEN  = 3,    /* bytes in a volume field */
    KVM_LEVEL_MAX  = '*',  /* volume field digits "**": level 100 */
    KVM_LEVEL_ON   = '%',  /* volume field status: not muted */
    KVM_LEVEL_MUTED = '/', /* volume field status: muted */
};

/* A decoded message. Not the wire layout: kvm_encode() / kvm_decode() convert. */
typedef struct {
    uint8_t type;                              /* enum kvm_type */
    uint8_t b[KVM_SLOTS];                      /* STATE / REQUEST: enum kvm_slot */
    uint8_t level[KVM_LEVELS][KVM_LEVEL_LEN];  /* STATE / REQUEST: enum kvm_level_host */
    uint8_t host;                              /* VOLUME: KVM_HOST_PC / KVM_HOST_MAC */
    uint8_t action;                            /* VOLUME: enum kvm_volume */
} kvm_msg_t;

/* A STATE or REQUEST with both volume fields "---". */
static inline kvm_msg_t kvm_msg(uint8_t type, const uint8_t b[KVM_SLOTS]) {
    kvm_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = type;
    memcpy(m.b, b, KVM_SLOTS);
    memset(m.level, KVM_UNKNOWN, sizeof m.level);
    return m;
}

static inline kvm_msg_t kvm_volume_msg(uint8_t host, uint8_t action) {
    kvm_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = KVM_VOLUME;
    memset(m.b, KVM_UNKNOWN, sizeof m.b);
    memset(m.level, KVM_UNKNOWN, sizeof m.level);
    m.host = host;
    m.action = action;
    return m;
}

/* Writes the wire bytes (no terminator) and returns their count, 0 for an unknown type. */
static inline unsigned kvm_encode(const kvm_msg_t *m, uint8_t out[KVM_MAX_LEN]) {
    out[0] = (uint8_t)KVM_MAGIC;
    out[1] = m->type;
    out[2] = ' ';
    if (m->type == KVM_VOLUME) {
        out[3] = m->host;
        out[4] = m->action;
        return KVM_VOLUME_LEN;
    }
    if (m->type != KVM_STATE && m->type != KVM_REQUEST) return 0;
    memcpy(out + 3, m->b, KVM_SLOTS);
    out[7] = ' ';
    memcpy(out + 8, m->level[KVM_LEVEL_PC], KVM_LEVEL_LEN);
    out[11] = ' ';
    memcpy(out + 12, m->level[KVM_LEVEL_MAC], KVM_LEVEL_LEN);
    return KVM_STATE_LEN;
}

/* ---- volume fields ---------------------------------------------------------------- */

/* A volume field: level 0-100 (clamped), or < 0 for "---". */
static inline void kvm_level_set(uint8_t f[KVM_LEVEL_LEN], int level, int muted) {
    if (level < 0) {
        memset(f, KVM_UNKNOWN, KVM_LEVEL_LEN);
        return;
    }
    if (level >= 100) {
        f[0] = f[1] = (uint8_t)KVM_LEVEL_MAX;
    } else {
        f[0] = (uint8_t)('0' + level / 10);
        f[1] = (uint8_t)('0' + level % 10);
    }
    f[2] = (uint8_t)(muted ? KVM_LEVEL_MUTED : KVM_LEVEL_ON);
}

/* A host's 0.0-1.0 volume as a level 0-100, rounded. */
static inline int kvm_level_from_scalar(float v) {
    if (!(v > 0.0f)) return 0;  /* also NaN */
    if (v >= 1.0f) return 100;
    return (int)(v * 100.0f + 0.5f);
}

/* The level 0-100 of a valid field, or -1 for "---". */
static inline int kvm_level_get(const uint8_t f[KVM_LEVEL_LEN]) {
    if (f[0] == KVM_UNKNOWN) return -1;
    if (f[0] == KVM_LEVEL_MAX) return 100;
    return (f[0] - '0') * 10 + (f[1] - '0');
}

static inline int kvm_level_muted(const uint8_t f[KVM_LEVEL_LEN]) {
    return f[2] == KVM_LEVEL_MUTED;
}

static inline int kvm_level_valid(const uint8_t f[KVM_LEVEL_LEN]) {
    if (f[0] == KVM_UNKNOWN) return f[1] == KVM_UNKNOWN && f[2] == KVM_UNKNOWN;
    const int digits = (f[0] >= '0' && f[0] <= '9' && f[1] >= '0' && f[1] <= '9') ||
                       (f[0] == KVM_LEVEL_MAX && f[1] == KVM_LEVEL_MAX);
    return digits && (f[2] == KVM_LEVEL_ON || f[2] == KVM_LEVEL_MUTED);
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

/* True if every slot byte is valid for a STATE / REQUEST (the table at the top). */
static inline int kvm_slots_valid(uint8_t type, const uint8_t b[KVM_SLOTS]) {
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
 * datagram of the wrong length, or with any byte out of the table, is dropped whole. */
static inline int kvm_decode(const void *buf, int len, uint8_t type, kvm_msg_t *out) {
    const uint8_t *p = (const uint8_t *)buf;
    if (len < 3 || p[0] != KVM_MAGIC || p[1] != type || p[2] != ' ') return 0;
    memset(out, 0, sizeof *out);
    memset(out->b, KVM_UNKNOWN, sizeof out->b);
    memset(out->level, KVM_UNKNOWN, sizeof out->level);
    out->type = type;
    if (type == KVM_VOLUME) {
        if (len != (int)KVM_VOLUME_LEN) return 0;
        out->host = p[3];
        out->action = p[4];
        return (out->host == KVM_HOST_PC || out->host == KVM_HOST_MAC) &&
               (out->action == KVM_VOL_UP || out->action == KVM_VOL_DOWN || out->action == KVM_VOL_MUTE);
    }
    if (len != (int)KVM_STATE_LEN || p[7] != ' ' || p[11] != ' ') return 0;
    memcpy(out->b, p + 3, KVM_SLOTS);
    memcpy(out->level[KVM_LEVEL_PC], p + 8, KVM_LEVEL_LEN);
    memcpy(out->level[KVM_LEVEL_MAC], p + 12, KVM_LEVEL_LEN);
    return kvm_slots_valid(type, out->b) && kvm_level_valid(out->level[KVM_LEVEL_PC]) &&
           kvm_level_valid(out->level[KVM_LEVEL_MAC]);
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
