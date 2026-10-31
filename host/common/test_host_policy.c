/* Tests for host_policy.h. Run: cc -I components/kvm_proto/include host/common/test_host_policy.c && ./a.out */
#include <stdio.h>
#include <stdlib.h>

#include "host_policy.h"

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static kvm_state_t state(uint8_t left, uint8_t right, uint8_t off) {
    kvm_state_t st;
    memset(&st, 0, sizeof st);
    st.mon_count = 2;
    st.mon[KVM_MON_LEFT] = left;
    st.mon[KVM_MON_RIGHT] = right;
    st.off = off;
    return st;
}

static host_actions_t feed_wake(host_policy_t *p, uint8_t left, uint8_t right, uint8_t off, uint8_t wake) {
    kvm_state_t st = state(left, right, off);
    st.wake = wake;
    return host_policy_on_state(p, &st);
}

static host_actions_t feed(host_policy_t *p, uint8_t left, uint8_t right, uint8_t off) {
    return feed_wake(p, left, right, off, 0);
}

static void test_topology(void) {
    host_policy_t pc;
    host_policy_init(&pc, KVM_IN_DP);
    host_actions_t a = feed(&pc, KVM_IN_DP, KVM_IN_DP, 0);
    CHECK(a.set_topology && a.topology == KVM_TOPO_EXTEND);
    CHECK(!a.wake_displays && !a.sleep_displays);  /* first STATE: no display actions */

    a = feed(&pc, KVM_IN_DP, KVM_IN_DP, 0);
    CHECK(!a.set_topology);                        /* unchanged: nothing to do */

    a = feed(&pc, KVM_IN_DP, KVM_IN_USBC, 0);      /* split, PC left */
    CHECK(a.set_topology && a.topology == KVM_TOPO_MIRROR);
    CHECK(a.mirror_mon == KVM_MON_LEFT && a.hidden_mon == KVM_MON_RIGHT);

    a = feed(&pc, KVM_IN_USBC, KVM_IN_DP, 0);      /* swapped */
    CHECK(a.set_topology && a.mirror_mon == KVM_MON_RIGHT && a.hidden_mon == KVM_MON_LEFT);

    a = feed(&pc, KVM_IN_USBC, KVM_IN_USBC, 0);    /* everything to the Mac: leave alone */
    CHECK(!a.set_topology);
    a = feed(&pc, KVM_IN_USBC, KVM_IN_DP, 0);      /* back to the same mirror: already set */
    CHECK(!a.set_topology);
    CHECK(a.wake_displays);                        /* a monitor came back to the PC */

    a = feed(&pc, KVM_IN_KEEP, KVM_IN_DP, 0);      /* left monitor unplugged */
    CHECK(!a.set_topology);
}

static void test_screens_off(void) {
    host_policy_t pc, mac;
    host_policy_init(&pc, KVM_IN_DP);
    host_policy_init(&mac, KVM_IN_USBC);
    feed(&pc, KVM_IN_DP, KVM_IN_USBC, 0);
    feed(&mac, KVM_IN_DP, KVM_IN_USBC, 0);

    /* Screens off: both monitors latched. */
    host_actions_t a = feed(&pc, KVM_IN_OFF, KVM_IN_OFF, 0x3);
    host_actions_t b = feed(&mac, KVM_IN_OFF, KVM_IN_OFF, 0x3);
    CHECK(a.sleep_displays && !a.set_topology);    /* layout stays as it was */
    CHECK(b.sleep_displays && !b.set_topology);
    CHECK(host_policy_reclaim(&pc) == 0x1);
    CHECK(host_policy_reclaim(&mac) == 0x2);

    a = feed(&pc, KVM_IN_OFF, KVM_IN_OFF, 0x3);    /* heartbeat: once only */
    CHECK(!a.sleep_displays);

    /* Everything to the PC: the PC wakes, the Mac stays asleep. */
    a = feed(&pc, KVM_IN_DP, KVM_IN_DP, 0);
    b = feed(&mac, KVM_IN_DP, KVM_IN_DP, 0);
    CHECK(a.wake_displays && a.set_topology && a.topology == KVM_TOPO_EXTEND);
    CHECK(!b.wake_displays && !b.sleep_displays);
    CHECK(host_policy_reclaim(&mac) == 0);
}

static void test_partial_off(void) {
    host_policy_t pc;
    host_policy_init(&pc, KVM_IN_DP);
    feed(&pc, KVM_IN_DP, KVM_IN_DP, 0);
    /* Only the left monitor off: the PC still shows on the right, so it stays awake. */
    host_actions_t a = feed(&pc, KVM_IN_OFF, KVM_IN_DP, 0x1);
    CHECK(!a.sleep_displays && !a.set_topology);
    /* Then the right one too. */
    a = feed(&pc, KVM_IN_OFF, KVM_IN_OFF, 0x3);
    CHECK(a.sleep_displays);
}

static void test_standby_without_latch(void) {
    host_policy_t pc;
    host_policy_init(&pc, KVM_IN_DP);
    feed(&pc, KVM_IN_DP, KVM_IN_DP, 0);
    /* A monitor reading standby on its own (no signal) is not Screens off. */
    host_actions_t a = feed(&pc, KVM_IN_OFF, KVM_IN_OFF, 0);
    CHECK(!a.sleep_displays);
    CHECK(host_policy_reclaim(&pc) == 0);
    a = feed(&pc, KVM_IN_DP, KVM_IN_DP, 0);
    CHECK(a.wake_displays && !a.set_topology);
}

static void test_never_mine(void) {
    host_policy_t mac;
    host_policy_init(&mac, KVM_IN_USBC);
    feed(&mac, KVM_IN_DP, KVM_IN_DP, 0);
    host_actions_t a = feed(&mac, KVM_IN_OFF, KVM_IN_OFF, 0x3);
    CHECK(!a.sleep_displays);                      /* not on any monitor: nothing to sleep */
    CHECK(host_policy_reclaim(&mac) == 0);
}

static void test_wake_request(void) {
    host_policy_t pc, mac;
    host_policy_init(&pc, KVM_IN_DP);
    host_policy_init(&mac, KVM_IN_USBC);
    host_actions_t a = feed_wake(&pc, KVM_IN_DP, KVM_IN_DP, 0, 0x35);  /* first: just note it */
    host_actions_t b = feed_wake(&mac, KVM_IN_DP, KVM_IN_DP, 0, 0x35);
    CHECK(!a.wake_displays && !b.wake_displays);
    a = feed_wake(&pc, KVM_IN_DP, KVM_IN_DP, 0, 0x36);  /* PC asked for, already showing */
    b = feed_wake(&mac, KVM_IN_DP, KVM_IN_DP, 0, 0x36);
    CHECK(a.wake_displays && !a.set_topology);
    CHECK(!b.wake_displays);
    a = feed_wake(&pc, KVM_IN_DP, KVM_IN_DP, 0, 0x46);  /* Mac asked for (e.g. the keyboard) */
    b = feed_wake(&mac, KVM_IN_DP, KVM_IN_DP, 0, 0x46);
    CHECK(!a.wake_displays && b.wake_displays);
    a = feed_wake(&pc, KVM_IN_DP, KVM_IN_DP, 0, 0x46);  /* heartbeat: once only */
    CHECK(!a.wake_displays);
    a = feed_wake(&pc, KVM_IN_DP, KVM_IN_DP, 0, 0x40);  /* nibble wraps 0xF -> 0x0 */
    CHECK(a.wake_displays);
}

int main(void) {
    test_topology();
    test_screens_off();
    test_partial_off();
    test_standby_without_latch();
    test_never_mine();
    test_wake_request();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    printf("host_policy: all tests passed\n");
    return EXIT_SUCCESS;
}
