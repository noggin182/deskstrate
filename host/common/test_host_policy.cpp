/* Tests for host_policy.h. Run: c++ -std=c++17 -I components/kvm_proto/include -I host/common host/common/test_host_policy.cpp && ./a.out */
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

#define PC KVM_HOST_PC
#define MAC KVM_HOST_MAC
#define GUEST KVM_HOST_GUEST
#define SB KVM_STANDBY
#define NC KVM_UNKNOWN

static host_actions_t feed(host_policy_t *p, uint8_t left, uint8_t right, uint8_t hid) {
    const uint8_t b[KVM_SLOTS] = {left, right, hid, hid};
    return host_policy_on_state(p, b);
}

static void test_topology(void) {
    host_policy_t pc;
    host_policy_init(&pc, PC);
    host_actions_t a = feed(&pc, PC, PC, PC);
    CHECK(a.set_topology && a.topology == HOST_TOPO_EXTEND);
    CHECK(!a.wake_displays);                       /* first STATE: no display actions */

    a = feed(&pc, PC, PC, PC);
    CHECK(!a.set_topology);                        /* unchanged: nothing to do */

    a = feed(&pc, PC, MAC, PC);                    /* split, PC left */
    CHECK(a.set_topology && a.topology == HOST_TOPO_MIRROR);
    CHECK(a.mirror_mon == KVM_MON_LEFT && a.hidden_mon == KVM_MON_RIGHT);

    a = feed(&pc, MAC, PC, PC);                    /* swapped */
    CHECK(a.set_topology && a.mirror_mon == KVM_MON_RIGHT && a.hidden_mon == KVM_MON_LEFT);

    a = feed(&pc, MAC, MAC, MAC);                  /* everything to the Mac: leave alone */
    CHECK(!a.set_topology);
    a = feed(&pc, MAC, PC, MAC);                   /* back to the same mirror: already set */
    CHECK(!a.set_topology);
    CHECK(a.wake_displays);                        /* a monitor came back to the PC */

    a = feed(&pc, NC, PC, MAC);                    /* left monitor unplugged */
    CHECK(!a.set_topology);
    a = feed(&pc, GUEST, PC, MAC);                 /* guest on the left */
    CHECK(!a.set_topology && !a.wake_displays);
}

static void test_standby(void) {
    host_policy_t pc;
    host_policy_init(&pc, PC);
    feed(&pc, PC, PC, PC);
    /* Monitors asleep on my input keep the layout. */
    host_actions_t a = feed(&pc, PC | SB, PC | SB, PC);
    CHECK(!a.set_topology && !a.wake_displays);
    a = feed(&pc, PC | SB, PC, PC);
    CHECK(!a.set_topology && a.wake_displays);     /* one woke on my input */
    a = feed(&pc, PC, PC, PC);
    CHECK(a.wake_displays);
    a = feed(&pc, PC, PC, PC);                     /* heartbeat: once only */
    CHECK(!a.wake_displays);
}

static void test_power(void) {
    host_policy_t pc;
    host_policy_init(&pc, PC);
    host_actions_t a = feed(&pc, PC, MAC, PC);
    CHECK(a.power == HOST_POWER_ON);               /* awake: make sure mine are on */
    a = feed(&pc, PC, MAC, PC);
    CHECK(a.power == HOST_POWER_NONE);             /* not every STATE... */
    feed(&pc, PC, MAC, PC);
    a = feed(&pc, PC, MAC, PC);
    CHECK(a.power == HOST_POWER_ON);               /* ...but every HOST_POWER_EVERY */
    a = feed(&pc, PC | SB, MAC | SB, PC);
    CHECK(a.power == HOST_POWER_STANDBY);          /* asleep: straight away */
    a = feed(&pc, PC | SB, MAC | SB, PC);
    CHECK(a.power == HOST_POWER_NONE);
    a = feed(&pc, PC, MAC, PC);
    CHECK(a.power == HOST_POWER_ON && a.wake_displays);  /* woken */
    a = feed(&pc, MAC | SB, MAC | SB, MAC);
    CHECK(a.power == HOST_POWER_NONE);             /* nothing on my input */
}

static void test_requests(void) {
    const uint8_t sleep[KVM_SLOTS] = KVM_SLEEP_REQUEST, wake[KVM_SLOTS] = KVM_WAKE_REQUEST;
    const uint8_t none[KVM_SLOTS] = {NC, NC, NC, NC}, hub[KVM_SLOTS] = {NC, NC, MAC, NC};
    CHECK(kvm_request_sleep(sleep) == 1 && kvm_request_sleep(wake) == 0);
    CHECK(kvm_request_sleep(none) == -1 && kvm_request_sleep(hub) == 0);  /* any request wakes */
    const uint8_t asleep[KVM_SLOTS] = {PC | SB, MAC | SB, PC, PC}, awake[KVM_SLOTS] = {PC, MAC, PC, PC};
    const uint8_t one_left[KVM_SLOTS] = {NC, MAC | SB, PC, PC};
    CHECK(kvm_asleep(asleep) && !kvm_asleep(awake) && kvm_asleep(one_left));
    CHECK(kvm_shows(asleep, sleep) && !kvm_shows(awake, sleep));
    CHECK(kvm_shows(awake, wake) && !kvm_shows(asleep, wake));
    CHECK(kvm_shows(awake, none) && kvm_shows(asleep, none));
    CHECK(!kvm_shows(awake, hub));
    const uint8_t to_pc[KVM_SLOTS] = {PC, PC, PC, PC};
    CHECK(!kvm_shows(awake, to_pc) && !kvm_shows(asleep, to_pc));
}

static void test_keyboard(void) {
    host_policy_t pc, mac;
    host_policy_init(&pc, PC);
    host_policy_init(&mac, MAC);
    feed(&pc, PC, MAC, PC);
    feed(&mac, PC, MAC, PC);
    /* Split: the keyboard moves to the Mac, monitors stay. */
    host_actions_t a = feed(&pc, PC, MAC, MAC);
    host_actions_t b = feed(&mac, PC, MAC, MAC);
    CHECK(!a.wake_displays && b.wake_displays);
    CHECK(!a.set_topology && !b.set_topology);
    b = feed(&mac, PC, MAC, MAC);                  /* heartbeat: once only */
    CHECK(!b.wake_displays);
    /* Everything to the PC: the PC wakes, the Mac doesn't. */
    a = feed(&pc, PC, PC, PC);
    b = feed(&mac, PC, PC, PC);
    CHECK(a.wake_displays && a.set_topology && a.topology == HOST_TOPO_EXTEND);
    CHECK(!b.wake_displays && !b.set_topology);
}

static void test_wire(void) {
    const uint8_t want[KVM_SLOTS] = {0x00, 0xFF, 0xFF, 0xFF};
    const kvm_msg_t m = kvm_msg(KVM_REQUEST, want);
    uint8_t raw[KVM_MSG_LEN];
    memcpy(raw, &m, sizeof raw);
    CHECK(raw[0] == 'K' && raw[1] == 'R' && raw[2] == 0x00 && raw[5] == 0xFF);
    kvm_msg_t out;
    CHECK(kvm_decode(raw, KVM_MSG_LEN, KVM_REQUEST, &out));
    CHECK(!kvm_decode(raw, KVM_MSG_LEN, KVM_STATE, &out));
    CHECK(!kvm_decode(raw, KVM_MSG_LEN - 1, KVM_REQUEST, &out));
    CHECK(kvm_mon_host(0x81) == MAC && kvm_mon_standby(0x81));
    CHECK(kvm_mon_host(NC) == KVM_HOST_KEEP && !kvm_mon_standby(NC));
}

int main(void) {
    test_topology();
    test_standby();
    test_power();
    test_requests();
    test_keyboard();
    test_wire();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    printf("host_policy: all tests passed\n");
    return EXIT_SUCCESS;
}
