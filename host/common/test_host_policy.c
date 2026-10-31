/* Tests for host_policy.h. Run: cc -I components/kvm_proto/include -I host/common host/common/test_host_policy.c && ./a.out */
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

#define BOTH_ON (KVM_HOST_SEEN(KVM_USB_PC) | KVM_HOST_DISPLAYS_ON(KVM_USB_PC) | \
                 KVM_HOST_SEEN(KVM_USB_MAC) | KVM_HOST_DISPLAYS_ON(KVM_USB_MAC))

typedef struct {
    uint8_t left, right, hid, hosts, wake, sleep;
} in_t;

static host_actions_t feed(host_policy_t *p, in_t in) {
    kvm_state_t st;
    memset(&st, 0, sizeof st);
    st.mon_count = 2;
    st.mon[KVM_MON_LEFT] = in.left;
    st.mon[KVM_MON_RIGHT] = in.right;
    st.usb[KVM_HUB_HID] = in.hid;
    st.usb[KVM_HUB_MEDIA] = in.hid;
    st.hosts = in.hosts;
    st.wake = in.wake;
    st.sleep = in.sleep;
    return host_policy_on_state(p, &st);
}

static void test_topology(void) {
    host_policy_t pc;
    host_policy_init(&pc, KVM_IN_DP);
    host_actions_t a = feed(&pc, (in_t){KVM_IN_DP, KVM_IN_DP, KVM_USB_PC, BOTH_ON, 0, 0});
    CHECK(a.set_topology && a.topology == KVM_TOPO_EXTEND);
    CHECK(!a.wake_displays && !a.sleep_displays);  /* first STATE: no display actions */

    a = feed(&pc, (in_t){KVM_IN_DP, KVM_IN_DP, KVM_USB_PC, BOTH_ON, 0, 0});
    CHECK(!a.set_topology);                        /* unchanged: nothing to do */

    a = feed(&pc, (in_t){KVM_IN_DP, KVM_IN_USBC, KVM_USB_PC, BOTH_ON, 0, 0});
    CHECK(a.set_topology && a.topology == KVM_TOPO_MIRROR);
    CHECK(a.mirror_mon == KVM_MON_LEFT && a.hidden_mon == KVM_MON_RIGHT);

    a = feed(&pc, (in_t){KVM_IN_USBC, KVM_IN_DP, KVM_USB_PC, BOTH_ON, 0, 0});
    CHECK(a.set_topology && a.mirror_mon == KVM_MON_RIGHT && a.hidden_mon == KVM_MON_LEFT);

    a = feed(&pc, (in_t){KVM_IN_USBC, KVM_IN_USBC, KVM_USB_MAC, BOTH_ON, 0, 0});
    CHECK(!a.set_topology);                        /* everything to the Mac: leave alone */
    a = feed(&pc, (in_t){KVM_IN_USBC, KVM_IN_DP, KVM_USB_MAC, BOTH_ON, 0, 0});
    CHECK(!a.set_topology);                        /* back to the same mirror: already set */
    CHECK(a.wake_displays);                        /* a monitor came back to the PC */

    a = feed(&pc, (in_t){KVM_IN_KEEP, KVM_IN_DP, KVM_USB_MAC, BOTH_ON, 0, 0});
    CHECK(!a.set_topology);                        /* left monitor unplugged */

    /* A monitor in standby (no signal) doesn't count as mine, so the layout stays. */
    a = feed(&pc, (in_t){KVM_IN_OFF, KVM_IN_OFF, KVM_USB_PC, 0, 0, 0});
    CHECK(!a.set_topology);
}

static void test_split_follows_leader(void) {
    host_policy_t pc, mac;
    host_policy_init(&pc, KVM_IN_DP);
    host_policy_init(&mac, KVM_IN_USBC);
    /* Split, keyboard on the PC: the Mac follows and keeps its displays awake. */
    in_t s = {KVM_IN_DP, KVM_IN_USBC, KVM_USB_PC, BOTH_ON, 0, 0};
    host_actions_t a = feed(&pc, s), b = feed(&mac, s);
    CHECK(!a.keep_awake);
    CHECK(b.keep_awake);

    /* The PC's displays sleep (idle, lock, or its "displays off" key): the Mac follows. */
    s.hosts = KVM_HOST_SEEN(KVM_USB_PC) | KVM_HOST_SEEN(KVM_USB_MAC) | KVM_HOST_DISPLAYS_ON(KVM_USB_MAC);
    a = feed(&pc, s);
    b = feed(&mac, s);
    CHECK(!a.sleep_displays && !a.wake_displays);
    CHECK(b.sleep_displays && !b.keep_awake);

    /* The Mac's go off too, and both monitors drop to standby (read as off); heartbeats
     * repeat nothing, and the layout stays. */
    s.left = s.right = KVM_IN_OFF;
    s.hosts = KVM_HOST_SEEN(KVM_USB_PC) | KVM_HOST_SEEN(KVM_USB_MAC);
    a = feed(&pc, s);
    b = feed(&mac, s);
    CHECK(!b.sleep_displays && !b.wake_displays && !b.set_topology && !a.set_topology);

    /* The PC wakes (mouse): its monitor comes back; the Mac's is still in standby, but
     * the Mac wakes and holds its displays on again. */
    s.left = KVM_IN_DP;
    s.hosts = KVM_HOST_SEEN(KVM_USB_PC) | KVM_HOST_DISPLAYS_ON(KVM_USB_PC) | KVM_HOST_SEEN(KVM_USB_MAC);
    b = feed(&mac, s);
    CHECK(b.wake_displays && b.keep_awake);
    s.right = KVM_IN_USBC;

    /* Keyboard moves to the Mac: now the PC follows. */
    s.hid = KVM_USB_MAC;
    s.hosts = BOTH_ON;
    a = feed(&pc, s);
    b = feed(&mac, s);
    CHECK(a.keep_awake && !b.keep_awake);
}

static void test_no_leader_daemon(void) {
    host_policy_t mac;
    host_policy_init(&mac, KVM_IN_USBC);
    /* Keyboard on the PC but no STATUS from it: the Mac keeps its own idle timer. */
    in_t s = {KVM_IN_DP, KVM_IN_USBC, KVM_USB_PC, KVM_HOST_SEEN(KVM_USB_MAC) | KVM_HOST_DISPLAYS_ON(KVM_USB_MAC), 0, 0};
    host_actions_t b = feed(&mac, s);
    CHECK(!b.keep_awake);
    s.hid = KVM_USB_GUEST2;  /* keyboard on a guest: same */
    b = feed(&mac, s);
    CHECK(!b.keep_awake && !b.sleep_displays);
}

static void test_not_on_screen(void) {
    host_policy_t mac;
    host_policy_init(&mac, KVM_IN_USBC);
    /* Everything on the PC: the Mac isn't on a monitor, so it doesn't follow. */
    in_t s = {KVM_IN_DP, KVM_IN_DP, KVM_USB_PC, BOTH_ON, 0, 0};
    host_actions_t b = feed(&mac, s);
    CHECK(!b.keep_awake);
    s.hosts = KVM_HOST_SEEN(KVM_USB_PC) | KVM_HOST_SEEN(KVM_USB_MAC);
    b = feed(&mac, s);
    CHECK(!b.sleep_displays);
}

static void test_wake_and_sleep_requests(void) {
    host_policy_t pc, mac;
    host_policy_init(&pc, KVM_IN_DP);
    host_policy_init(&mac, KVM_IN_USBC);
    in_t s = {KVM_IN_DP, KVM_IN_DP, KVM_USB_PC, BOTH_ON, 0x35, 0x00};
    host_actions_t a = feed(&pc, s), b = feed(&mac, s);  /* first: just note them */
    CHECK(!a.wake_displays && !b.wake_displays);

    s.wake = 0x36;  /* PC asked for, already showing */
    a = feed(&pc, s);
    b = feed(&mac, s);
    CHECK(a.wake_displays && !a.set_topology && !b.wake_displays);

    s.wake = 0x46;  /* Mac asked for (e.g. the keyboard) */
    a = feed(&pc, s);
    b = feed(&mac, s);
    CHECK(!a.wake_displays && b.wake_displays);

    a = feed(&pc, s);  /* heartbeat: once only */
    CHECK(!a.wake_displays);

    s.wake = 0x40;  /* nibble wraps 0xF -> 0x0 */
    a = feed(&pc, s);
    CHECK(a.wake_displays);

    s.sleep = 0x01;  /* Screens off, sent to the PC (it has the keyboard) */
    a = feed(&pc, s);
    b = feed(&mac, s);
    CHECK(a.sleep_displays && !b.sleep_displays);
    a = feed(&pc, s);
    CHECK(!a.sleep_displays);
}

int main(void) {
    test_topology();
    test_split_follows_leader();
    test_no_leader_daemon();
    test_not_on_screen();
    test_wake_and_sleep_requests();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    printf("host_policy: all tests passed\n");
    return EXIT_SUCCESS;
}
