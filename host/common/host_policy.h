/*
 * host_policy.h - what a host daemon does with each STATE from the core. Shared by the
 * Windows daemon and the Mac daemon (Swift, via C++ interop and a bridging header), so both
 * hosts follow the same rules. No I/O: test with host/common/test_host_policy.cpp.
 *
 * Rules:
 *   - Topology: 2 monitors on my input = extend, 1 = mirror onto it, 0 = leave alone.
 *     A monitor in standby on my input keeps its place in the layout.
 *   - A monitor coming (back) to my input, awake, wakes my displays.
 *   - So does the keyboard (HID hub) moving to me.
 *   - Monitor power follows the desk's sleep (standby in STATE, kvm_proto.h): while the
 *     desk is asleep, the monitors showing my input go to DDC standby; while it's awake,
 *     back on. The core can't do it for my inputs. Asked on every STATE, not only on a
 *     change, but at most every HOST_POWER_EVERY STATEs while the answer is the same
 *     (the daemon skips a monitor already there).
 */
#ifndef HOST_POLICY_H
#define HOST_POLICY_H

#include <stdint.h>
#include <string.h>

#include "kvm_proto.h"

enum host_topology { HOST_TOPO_NONE = 0, HOST_TOPO_EXTEND = 1, HOST_TOPO_MIRROR = 2 };
enum host_power { HOST_POWER_NONE = 0, HOST_POWER_STANDBY = 1, HOST_POWER_ON = 2 };

#define HOST_POWER_EVERY 3u  /* STATEs: about 6 s at the 2 s heartbeat */

typedef struct {
    uint8_t my_host;    /* enum kvm_host: KVM_HOST_PC or KVM_HOST_MAC (also my hub byte) */
    uint8_t started;    /* a STATE has been seen */
    uint8_t visible;    /* bit per monitor slot: showing my input, awake */
    uint8_t keyboard;   /* the HID hub is on me */
    uint8_t topology;   /* enum host_topology last asked for (NONE until the first) */
    uint8_t mirror_mon; /* monitor slot mirrored onto, when topology is MIRROR */
    uint8_t power;      /* enum host_power last asked for */
    uint8_t power_wait; /* STATEs until it's asked again */
} host_policy_t;

typedef struct {
    uint8_t wake_displays;     /* wake this host's displays now */
    uint8_t set_topology;      /* apply topology (and mirror_mon) now */
    uint8_t topology;          /* enum host_topology: EXTEND or MIRROR */
    uint8_t mirror_mon;        /* MIRROR: the monitor slot to show everything on */
    uint8_t hidden_mon;        /* MIRROR: the other monitor (the one to hide) */
    uint8_t power;             /* enum host_power: DDC standby / on for my input's monitors */
} host_actions_t;

static inline void host_policy_init(host_policy_t *p, uint8_t my_host) {
    memset(p, 0, sizeof *p);
    p->my_host = my_host;
    p->topology = HOST_TOPO_NONE;
}

/* b: the STATE's 4 bytes, by enum kvm_slot. */
static inline host_actions_t host_policy_on_state(host_policy_t *p, const uint8_t b[KVM_SLOTS]) {
    host_actions_t act;
    memset(&act, 0, sizeof act);

    uint8_t visible = 0, assigned = 0;
    for (unsigned m = KVM_MON_LEFT; m <= KVM_MON_RIGHT; m++) {
        const uint8_t bit = (uint8_t)(1u << m);
        if (kvm_mon_host(b[m]) != p->my_host) continue;
        assigned |= bit;
        if (!kvm_mon_standby(b[m])) visible |= bit;
    }
    const uint8_t keyboard = b[KVM_HUB_HID] == p->my_host;

    if (p->started) {
        if (visible & ~p->visible) act.wake_displays = 1;
        if (keyboard && !p->keyboard) act.wake_displays = 1;
    }

    uint8_t topo = HOST_TOPO_NONE, mirror = 0;
    if (assigned == 0x3) {
        topo = HOST_TOPO_EXTEND;
    } else if (assigned) {
        topo = HOST_TOPO_MIRROR;
        mirror = assigned == 0x1 ? KVM_MON_LEFT : KVM_MON_RIGHT;
    }
    if (topo != HOST_TOPO_NONE && (topo != p->topology || (topo == HOST_TOPO_MIRROR && mirror != p->mirror_mon))) {
        act.set_topology = 1;
        act.topology = topo;
        act.mirror_mon = mirror;
        act.hidden_mon = mirror == KVM_MON_LEFT ? KVM_MON_RIGHT : KVM_MON_LEFT;
        p->topology = topo;
        p->mirror_mon = mirror;
    }

    if (assigned) {
        const uint8_t power = kvm_asleep(b) ? HOST_POWER_STANDBY : HOST_POWER_ON;
        if (power != p->power || p->power_wait == 0) {
            act.power = power;
            p->power = power;
            p->power_wait = HOST_POWER_EVERY;
        }
        p->power_wait--;
    }

    p->visible = visible;
    p->keyboard = keyboard;
    p->started = 1;
    return act;
}

#endif /* HOST_POLICY_H */
