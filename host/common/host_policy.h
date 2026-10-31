/*
 * host_policy.h - what a host daemon does with each STATE from the core. Shared by the
 * Windows daemon (C++) and the Mac daemon (Swift, via a bridging header), so both hosts
 * follow the same rules. Pure C, no I/O: test with host/common/test_host_policy.c.
 *
 * Rules (PROJECT_KNOWLEDGE.md §4, host daemons):
 *   - Topology: 2 monitors on my input = extend, 1 = mirror onto it, 0 = leave alone.
 *   - The core never sleeps the monitors; they follow their host's signal. The host with
 *     the keyboard (HID hub) leads: its displays sleep and wake the usual way (idle
 *     timeout, lock, a key mapped to "displays off").
 *   - Following: when I'm on a monitor but the keyboard is on the other host (split mode),
 *     I keep my displays awake while the leader's are on, sleep them when the leader's
 *     sleep, and wake them when the leader's wake (STATE.hosts).
 *   - A monitor coming (back) to my input wakes my displays. So does my STATE.wake nibble
 *     changing: a deck key asked for me, even if I already had the monitors or keyboard.
 *   - My STATE.sleep nibble changing (the deck's Screens off, sent to the keyboard's host)
 *     sleeps my displays.
 */
#ifndef HOST_POLICY_H
#define HOST_POLICY_H

#include <stdint.h>
#include <string.h>

#include "kvm_proto.h"

typedef struct {
    uint8_t my_input;   /* enum kvm_input this host is cabled to: KVM_IN_DP (PC), KVM_IN_USBC (Mac) */
    uint8_t started;    /* a STATE has been seen */
    uint8_t visible;    /* bit per kvm_mon_id: showing my input now */
    uint8_t mine;       /* bit per kvm_mon_id: last real input seen there was mine (a monitor
                         * in standby because my displays sleep reads KVM_IN_OFF, still mine) */
    uint8_t topology;   /* enum kvm_topology last asked for (NONE until the first) */
    uint8_t mirror_mon; /* kvm_mon_id mirrored onto, when topology is MIRROR */
    uint8_t wake;       /* my STATE.wake nibble, last seen */
    uint8_t sleep;      /* my STATE.sleep nibble, last seen */
    uint8_t following;  /* last STATE: I follow the keyboard's host */
    uint8_t leader_on;  /* last STATE: the leader's displays were on */
} host_policy_t;

typedef struct {
    uint8_t sleep_displays;    /* sleep this host's displays now */
    uint8_t wake_displays;     /* wake them now */
    uint8_t keep_awake;        /* level: hold "display required" (stop idle sleep) while set */
    uint8_t set_topology;      /* apply topology (and mirror_mon) now */
    uint8_t topology;          /* enum kvm_topology: EXTEND or MIRROR */
    uint8_t mirror_mon;        /* MIRROR: the kvm_mon_id to show everything on */
    uint8_t hidden_mon;        /* MIRROR: the other monitor (the one to hide) */
} host_actions_t;

static inline void host_policy_init(host_policy_t *p, uint8_t my_input) {
    memset(p, 0, sizeof *p);
    p->my_input = my_input;
    p->topology = KVM_TOPO_NONE;
}

/* This host as a kvm_usb_port (KVM_USB_PC / KVM_USB_MAC), or KVM_USB_UNKNOWN. */
static inline uint8_t host_policy_me(uint8_t my_input) {
    if (my_input == KVM_IN_DP) return KVM_USB_PC;
    if (my_input == KVM_IN_USBC) return KVM_USB_MAC;
    return KVM_USB_UNKNOWN;
}

/* My 4-bit counter in a STATE.wake / STATE.sleep byte (PC low, Mac high); 0xFF if none. */
static inline uint8_t host_policy_nibble(uint8_t my_input, uint8_t counters) {
    const uint8_t me = host_policy_me(my_input);
    if (me == KVM_USB_PC) return counters & 0x0F;
    if (me == KVM_USB_MAC) return counters >> 4;
    return 0xFF;
}

static inline host_actions_t host_policy_on_state(host_policy_t *p, const kvm_state_t *st) {
    host_actions_t act;
    memset(&act, 0, sizeof act);

    const unsigned count = st->mon_count < 2 ? st->mon_count : 2;  /* left and right only */
    uint8_t visible = 0, assigned = 0;
    for (unsigned m = 0; m < count; m++) {
        const uint8_t bit = (uint8_t)(1u << m);
        const uint8_t in = st->mon[m];
        if (in == KVM_IN_DP || in == KVM_IN_HDMI || in == KVM_IN_USBC) {
            if (in == p->my_input) p->mine |= bit; else p->mine &= (uint8_t)~bit;
        }
        if (in == p->my_input) visible |= bit;
        if (in == p->my_input || (in == KVM_IN_OFF && (p->mine & bit))) assigned |= bit;
    }

    const uint8_t me = host_policy_me(p->my_input);
    const uint8_t leader = st->usb[KVM_HUB_HID];
    const uint8_t following = assigned && me != KVM_USB_UNKNOWN && leader != me &&
                              (leader == KVM_USB_PC || leader == KVM_USB_MAC) &&
                              (st->hosts & KVM_HOST_SEEN(leader));
    const uint8_t leader_on = following && (st->hosts & KVM_HOST_DISPLAYS_ON(leader));
    const uint8_t wake = host_policy_nibble(p->my_input, st->wake);
    const uint8_t sleep = host_policy_nibble(p->my_input, st->sleep);

    if (p->started) {
        if (following && p->following && p->leader_on && !leader_on) act.sleep_displays = 1;
        if (following && p->following && !p->leader_on && leader_on) act.wake_displays = 1;
        if (sleep != p->sleep) act.sleep_displays = 1;
        if (!p->visible && visible) act.wake_displays = 1;
        if (wake != p->wake) act.wake_displays = 1;
        if (act.wake_displays) act.sleep_displays = 0;  /* asked for me beats going dark */
    }
    act.keep_awake = leader_on;

    uint8_t topo = KVM_TOPO_NONE, mirror = 0;
    if (assigned == 0x3) {
        topo = KVM_TOPO_EXTEND;
    } else if (assigned) {
        topo = KVM_TOPO_MIRROR;
        mirror = assigned == 0x1 ? KVM_MON_LEFT : KVM_MON_RIGHT;
    }
    if (topo != KVM_TOPO_NONE && (topo != p->topology || (topo == KVM_TOPO_MIRROR && mirror != p->mirror_mon))) {
        act.set_topology = 1;
        act.topology = topo;
        act.mirror_mon = mirror;
        act.hidden_mon = mirror == KVM_MON_LEFT ? KVM_MON_RIGHT : KVM_MON_LEFT;
        p->topology = topo;
        p->mirror_mon = mirror;
    }

    p->visible = visible;
    p->wake = wake;
    p->sleep = sleep;
    p->following = following;
    p->leader_on = leader_on;
    p->started = 1;
    return act;
}

#endif /* HOST_POLICY_H */
