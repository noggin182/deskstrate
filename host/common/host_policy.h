/*
 * host_policy.h - what a host daemon does with each STATE from the core. Shared by the
 * Windows daemon (C++) and the Mac daemon (Swift, via a bridging header), so both hosts
 * follow the same rules. Pure C, no I/O: test with host/common/test_host_policy.c.
 *
 * Rules (PROJECT_KNOWLEDGE.md §4, host daemons):
 *   - Topology: 2 monitors on my input = extend, 1 = mirror onto it, 0 = leave alone.
 *   - Screens off: once every monitor I was on is in STATE.off, sleep my displays.
 *   - A monitor coming (back) to my input wakes my displays.
 *   - So does my STATE.wake nibble changing: a deck key asked for me, even if I already
 *     had the monitors (or the keyboard).
 *   - My displays waking locally (mouse, keyboard) while Screens off holds my monitors:
 *     ask the core to put those monitors back on my input (host_policy_reclaim()).
 */
#ifndef HOST_POLICY_H
#define HOST_POLICY_H

#include <stdint.h>
#include <string.h>

#include "kvm_proto.h"

typedef struct {
    uint8_t my_input;   /* enum kvm_input this host is cabled to: KVM_IN_DP (PC), KVM_IN_USBC (Mac) */
    uint8_t started;    /* a STATE has been seen */
    uint8_t mine;       /* bit per kvm_mon_id: last real input seen there was mine */
    uint8_t visible;    /* bit per kvm_mon_id: showing my input now */
    uint8_t asleep;     /* bit per kvm_mon_id: mine, and held off by Screens off */
    uint8_t sleeping;   /* Screens off holds every monitor I'm on, and none shows me */
    uint8_t topology;   /* enum kvm_topology last asked for (NONE until the first) */
    uint8_t mirror_mon; /* kvm_mon_id mirrored onto, when topology is MIRROR */
    uint8_t wake;       /* my STATE.wake nibble, last seen */
} host_policy_t;

typedef struct {
    uint8_t sleep_displays;    /* sleep this host's displays now */
    uint8_t wake_displays;     /* wake them now */
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

/* My 4-bit wake counter in STATE.wake (PC low, Mac high); 0xFF if I have none. */
static inline uint8_t host_policy_wake_nibble(uint8_t my_input, uint8_t wake) {
    if (my_input == KVM_IN_DP) return wake & 0x0F;
    if (my_input == KVM_IN_USBC) return wake >> 4;
    return 0xFF;
}

static inline int host_policy_is_real_input(uint8_t in) {
    return in == KVM_IN_DP || in == KVM_IN_HDMI || in == KVM_IN_USBC;
}

static inline host_actions_t host_policy_on_state(host_policy_t *p, const kvm_state_t *st) {
    host_actions_t act;
    memset(&act, 0, sizeof act);

    const unsigned count = st->mon_count < 2 ? st->mon_count : 2;  /* left and right only */
    uint8_t visible = 0, asleep = 0, assigned = 0;
    for (unsigned m = 0; m < count; m++) {
        const uint8_t bit = (uint8_t)(1u << m);
        const uint8_t in = st->mon[m];
        if (host_policy_is_real_input(in)) {
            if (in == p->my_input) p->mine |= bit; else p->mine &= (uint8_t)~bit;
        }
        if (in == p->my_input) visible |= bit;
        if ((p->mine & bit) && in == KVM_IN_OFF && (st->off & bit)) asleep |= bit;
    }
    /* Monitors that are asleep for Screens off keep their place in the layout. */
    assigned = visible | asleep;

    const uint8_t sleeping = !visible && asleep;
    const uint8_t wake = host_policy_wake_nibble(p->my_input, st->wake);
    if (p->started) {
        if (!p->visible && visible) act.wake_displays = 1;
        if (wake != p->wake) act.wake_displays = 1;
        if (sleeping && !p->sleeping) act.sleep_displays = 1;
    }
    p->wake = wake;

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
    p->asleep = asleep;
    p->sleeping = sleeping;
    p->started = 1;
    return act;
}

/* Monitors to ask back onto my input (KVM_ACT_MON) after my displays woke locally. */
static inline uint8_t host_policy_reclaim(const host_policy_t *p) {
    return p->sleeping ? p->asleep : 0;
}

#endif /* HOST_POLICY_H */
