/*
 * kvm_proto.h - wire format shared by the core, the deck and the PC/Mac daemons.
 *
 * Nodes: the core (central box, source of truth), the deck (desk unit) and the
 * host daemons (PC, Mac). Only the core changes state; everyone else asks.
 *
 * Transport: UDP, one message per datagram, KVM_PORT on every node.
 *   core  -> all   : KVM_STATE  (on change + every KVM_HEARTBEAT_MS)
 *   core  -> one   : KVM_RESULT (reply to a REQUEST), then KVM_STATE (the current one)
 *   hosts -> core  : KVM_STATUS (reply to each STATE + on local change)
 *   hosts,
 *   deck  -> core  : KVM_REQUEST
 * Replies go to the source address/port of the datagram being answered.
 *
 * Layout rules (so no packing pragmas are needed):
 *   - every struct is a multiple of 8 bytes (hdr.counter is 8-byte aligned)
 *   - little-endian on the wire (ESP32, x86-64 and Apple Silicon all are)
 *   - fields ordered by size so every struct is naturally aligned with no padding;
 *     the _Static_asserts below fail the build if that ever stops being true
 *   - decode by memcpy() from the receive buffer, never by casting the pointer
 *
 * Authentication: tag = first 16 bytes of HMAC-SHA256(key, all bytes before the tag).
 * Freshness: every sender's hdr.counter must be strictly greater than the last one
 * the receiver accepted from that sender (see KVM_COUNTER_* notes).
 */
#ifndef KVM_PROTO_H
#define KVM_PROTO_H

#include <stdint.h>

#define KVM_PORT          18200u        /* IANA unassigned (18188-18240); keep it configurable */
#define KVM_MAGIC         0x314D564Bu   /* "KVM1" as bytes on the wire */
#define KVM_VERSION       1u
#define KVM_TAG_LEN       16u
#define KVM_KEY_LEN       32u
#define KVM_MAX_MON       4u
#define KVM_HEARTBEAT_MS  2000u
#define KVM_HOST_TIMEOUT_MS 5000u       /* no STATUS for this long => host considered gone */

/* ---- enums (sent as uint8_t) -------------------------------------------- */

enum kvm_type {
    KVM_STATE   = 1,
    KVM_STATUS  = 2,
    KVM_REQUEST = 3,
    KVM_RESULT  = 4,
};

/* Node ids: bit 7 = reserved for special values, bit 6 = 0 controller / 1 host device. */
#define KVM_NODE_F_RESERVED 0x80u
#define KVM_NODE_F_DEVICE   0x40u
#define KVM_NODE_IS_DEVICE(n) (((n) & (KVM_NODE_F_RESERVED | KVM_NODE_F_DEVICE)) == KVM_NODE_F_DEVICE)

enum kvm_node {
    KVM_NODE_CORE  = 0x00,  /* controllers: 0x00-0x3F */
    KVM_NODE_DECK  = 0x01,
    KVM_NODE_PC    = 0x40,  /* host devices: 0x40-0x7F */
    KVM_NODE_MAC   = 0x41,
    KVM_NODE_NONE  = 0xFF,  /* writer: nobody may send DDC */
};

/* Desired/actual monitor input. Real inputs are the Dell P2723DE VCP 0x60 values,
 * so the writer can send them straight through. */
enum kvm_input {
    KVM_IN_KEEP = 0x00,     /* desired: don't touch    | actual: unknown/unreadable */
    KVM_IN_DP   = 0x0F,     /* 15 - PC                                             */
    KVM_IN_HDMI = 0x11,     /* 17 - guest                                          */
    KVM_IN_USBC = 0x1B,     /* 27 - Mac                                            */
    KVM_IN_OFF  = 0xFF,     /* desired: standby (VCP 0xD6 = 4)                      */
};

/* The two 1:4 USB hubs, by role. */
enum kvm_usb_hub {
    KVM_HUB_HID   = 0,      /* keyboard + mouse: follows the host being worked on */
    KVM_HUB_MEDIA = 1,      /* camera, headset, sound card: stays put across switches (calls) */
};
#define KVM_HUB_COUNT 2u

enum kvm_usb_port {         /* host selected on a hub (same numbering on both hubs) */
    KVM_USB_PC     = 0,
    KVM_USB_MAC    = 1,
    KVM_USB_GUEST1 = 2,     /* HDMI guest on the left monitor  */
    KVM_USB_GUEST2 = 3,     /* HDMI guest on the right monitor */
    KVM_USB_UNKNOWN = 0xFF,
};

enum kvm_mon_id { KVM_MON_LEFT = 0, KVM_MON_RIGHT = 1 };  /* daemons map these to EDID serials */

enum kvm_power    { KVM_PWR_UNKNOWN = 0, KVM_PWR_ON = 1, KVM_PWR_STANDBY = 4 };  /* VCP 0xD6 */
enum kvm_displays { KVM_DISP_ON = 0, KVM_DISP_ASLEEP = 1, KVM_DISP_LOCKED = 2 };
enum kvm_topology { KVM_TOPO_NONE = 0, KVM_TOPO_EXTEND = 1, KVM_TOPO_MIRROR = 2 };

enum kvm_hdr_flags {
    KVM_F_HEARTBEAT = 1u << 0,  /* STATE: repeat of an unchanged seq */
    KVM_F_GOING_DOWN = 1u << 1, /* STATUS: host about to sleep/shut down/undock */
};

/* ---- header: 16 bytes --------------------------------------------------- */

typedef struct {
    uint32_t magic;     /* KVM_MAGIC */
    uint8_t  version;   /* KVM_VERSION */
    uint8_t  type;      /* enum kvm_type */
    uint8_t  sender;    /* enum kvm_node */
    uint8_t  flags;     /* enum kvm_hdr_flags */
    uint64_t counter;   /* strictly increasing per sender, see below */
} kvm_hdr_t;

/*
 * KVM_COUNTER - replay protection without needing a clock on the ESP32 nodes:
 *   core, deck: (boot_epoch << 32) | n   boot_epoch is a uint32 in NVS, incremented on
 *                                        every boot before the first send; n counts packets.
 *   hosts:      Unix time in milliseconds (they have an RTC + NTP).
 * Receivers keep the last accepted counter per sender and drop anything <= it.
 * Once the core has SNTP time it may additionally drop host packets whose counter
 * is more than KVM_FRESH_WINDOW_MS from its own clock (blocks replays after it reboots).
 */
#define KVM_FRESH_WINDOW_MS 30000u

/* ---- STATE: core -> all (48 bytes) ------------------------------------- */

typedef struct {
    kvm_hdr_t hdr;
    uint32_t  seq;                  /* bumps only when the desired state changes */
    uint8_t   writer;               /* enum kvm_node: the only node allowed to send DDC */
    uint8_t   preset;               /* core preset id that produced this state, 0xFF = ad hoc */
    uint8_t   mon_count;            /* valid entries in mon[] */
    uint8_t   usb[KVM_HUB_COUNT];   /* enum kvm_usb_port, indexed by enum kvm_usb_hub */
    uint8_t   mon[KVM_MAX_MON];     /* enum kvm_input, desired, indexed by enum kvm_mon_id */
    uint8_t   reserved[3];          /* zero; keeps the size a multiple of 8 */
    uint8_t   tag[KVM_TAG_LEN];
} kvm_state_t;

/* ---- STATUS: host -> core (56 bytes) ----------------------------------- */

typedef struct {
    uint8_t seen;       /* host can see this monitor (Mac undocked => 0) */
    uint8_t input;      /* enum kvm_input, read over DDC (& 0xFF) */
    uint8_t power;      /* enum kvm_power, read over DDC */
    uint8_t err;        /* 0 = ok, else enum kvm_err */
} kvm_mon_status_t;

typedef struct {
    kvm_hdr_t        hdr;
    uint32_t         ack_seq;           /* last STATE seq fully applied */
    uint8_t          awake;             /* 0 = about to sleep */
    uint8_t          displays;          /* enum kvm_displays */
    uint8_t          topology;          /* enum kvm_topology actually applied */
    uint8_t          mon_count;
    kvm_mon_status_t mon[KVM_MAX_MON];
    uint8_t          tag[KVM_TAG_LEN];
} kvm_status_t;

enum kvm_err { KVM_ERR_NONE = 0, KVM_ERR_DDC_TIMEOUT = 1, KVM_ERR_NOT_FOUND = 2, KVM_ERR_TOPOLOGY = 3 };

/* ---- REQUEST: host or deck -> core (40 bytes) -------------------------- */
/* Deck keys, or hotkeys on whichever host currently has the keyboard. The core
 * decides; the outcome arrives as a RESULT, then the current STATE sent straight back
 * to the requester (and, if accepted, broadcast as usual). */

enum kvm_action {
    KVM_ACT_PRESET      = 1,  /* a = preset id (presets live in core config) */
    KVM_ACT_NEXT_PRESET = 2,
    KVM_ACT_PREV_PRESET = 3,
    KVM_ACT_USB         = 4,  /* a = enum kvm_usb_hub, b = enum kvm_usb_port */
    KVM_ACT_MON         = 5,  /* a = enum kvm_mon_id, b = enum kvm_input */
    KVM_ACT_SWAP        = 6,  /* split mode: swap which host owns which monitor */
    KVM_ACT_FOCUS       = 7,  /* HID hub follows the owner of monitor a (or the cursor's monitor); MEDIA untouched */
    KVM_ACT_ALL_OFF     = 8,  /* every monitor to standby */
    KVM_ACT_GET_STATE   = 9,  /* no change: just the RESULT + STATE reply (a client starting up) */
    KVM_ACT_USER        = 0x80, /* 0x80-0xFF: free for custom bindings handled on the core */
};

typedef struct {
    kvm_hdr_t hdr;
    uint32_t  req_id;       /* chosen by the host, echoed in RESULT */
    uint8_t   action;       /* enum kvm_action */
    uint8_t   a;
    uint8_t   b;
    uint8_t   c;
    uint8_t   tag[KVM_TAG_LEN];
} kvm_request_t;

/* ---- RESULT: core -> requester (48 bytes) ------------------------------ */

enum kvm_result_code {
    KVM_RES_OK       = 0,
    KVM_RES_UNKNOWN  = 1,   /* unknown action or argument */
    KVM_RES_REJECTED = 2,   /* valid but not allowed now (e.g. target host asleep) */
    KVM_RES_BUSY     = 3,   /* previous change still settling, try again */
};

typedef struct {
    kvm_hdr_t hdr;
    uint32_t  req_id;
    uint32_t  new_seq;      /* STATE seq that carries the change (0 if none) */
    uint8_t   code;         /* enum kvm_result_code */
    uint8_t   reserved[7];  /* zero; keeps the size a multiple of 8 */
    uint8_t   tag[KVM_TAG_LEN];
} kvm_result_t;

/* ---- layout checks ------------------------------------------------------ */

#ifdef __cplusplus
#define KVM_ASSERT static_assert
#else
#define KVM_ASSERT _Static_assert
#endif
KVM_ASSERT(sizeof(kvm_hdr_t)     == 16, "hdr layout");
KVM_ASSERT(sizeof(kvm_state_t)   == 48, "state layout");
KVM_ASSERT(sizeof(kvm_status_t)  == 56, "status layout");
KVM_ASSERT(sizeof(kvm_request_t) == 40, "request layout");
KVM_ASSERT(sizeof(kvm_result_t)  == 48, "result layout");

#endif /* KVM_PROTO_H */
