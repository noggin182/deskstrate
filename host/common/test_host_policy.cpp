/* Tests for host_policy.h. Run: c++ -std=c++17 -I components/kvm_proto/include -I host/common host/common/test_host_policy.cpp && ./a.out */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_policy.h"
#include "level_report.h"

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
#define NC KVM_UNKNOWN   /* hub not connected; leave alone in a REQUEST */
#define MNC KVM_MON_NC   /* monitor not connected (STATE) */

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

    a = feed(&pc, MNC, PC, MAC);                   /* left monitor unplugged */
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
    const uint8_t one_left[KVM_SLOTS] = {MNC, MAC | SB, PC, PC};
    CHECK(kvm_asleep(asleep) && !kvm_asleep(awake) && kvm_asleep(one_left));
    /* With neither monitor connected, '~' still carries the desk's sleep. */
    const uint8_t none_asleep[KVM_SLOTS] = {MNC | SB, MNC | SB, PC, NC}, none_awake[KVM_SLOTS] = {MNC, MNC, PC, NC};
    CHECK(kvm_asleep(none_asleep) && !kvm_asleep(none_awake));
    CHECK(kvm_shows(none_asleep, sleep) && !kvm_shows(none_awake, sleep) && kvm_shows(none_awake, wake));
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

static int encodes_to(const kvm_msg_t &m, const char *want) {
    uint8_t raw[KVM_MAX_LEN];
    const unsigned n = kvm_encode(&m, raw);
    return n == strlen(want) && memcmp(raw, want, n) == 0;
}

static void test_wire(void) {
    const uint8_t want[KVM_SLOTS] = {PC, NC, NC, NC};
    kvm_msg_t m = kvm_msg(KVM_REQUEST, want);
    CHECK(encodes_to(m, "KR P--- --- ---"));
    kvm_msg_t out;
    CHECK(kvm_decode("KR P--- --- ---", KVM_STATE_LEN, KVM_REQUEST, &out));
    CHECK(memcmp(out.b, want, KVM_SLOTS) == 0 && kvm_level_get(out.level[KVM_LEVEL_PC]) < 0);
    CHECK(!kvm_decode("KR P--- --- ---", KVM_STATE_LEN, KVM_STATE, &out));
    CHECK(!kvm_decode("KR P--- --- ---", KVM_STATE_LEN - 1, KVM_REQUEST, &out));
    CHECK(!kvm_decode("KRP---", 6, KVM_REQUEST, &out));   /* the old 6-byte format */
    CHECK(!kvm_decode("KSPPPP", 6, KVM_STATE, &out));

    /* Volume fields round trip. */
    kvm_level_set(m.level[KVM_LEVEL_PC], 42, 0);
    kvm_level_set(m.level[KVM_LEVEL_MAC], 100, 1);
    m.type = KVM_STATE;
    m.b[1] = MAC; m.b[2] = MAC; m.b[3] = MAC;
    CHECK(encodes_to(m, "KS PMMM 42% **/"));
    CHECK(kvm_decode("KS PMMM 42% **/", KVM_STATE_LEN, KVM_STATE, &out));
    CHECK(kvm_level_get(out.level[KVM_LEVEL_PC]) == 42 && !kvm_level_muted(out.level[KVM_LEVEL_PC]));
    CHECK(kvm_level_get(out.level[KVM_LEVEL_MAC]) == 100 && kvm_level_muted(out.level[KVM_LEVEL_MAC]));
    kvm_level_set(m.level[KVM_LEVEL_PC], 0, 0);
    kvm_level_set(m.level[KVM_LEVEL_MAC], -1, 1);
    CHECK(encodes_to(m, "KS PMMM 00% ---"));
    kvm_level_set(m.level[KVM_LEVEL_PC], 7, 1);
    kvm_level_set(m.level[KVM_LEVEL_MAC], 250, 0);
    CHECK(encodes_to(m, "KS PMMM 07/ **%"));
    CHECK(kvm_level_from_scalar(0.0f) == 0 && kvm_level_from_scalar(0.424f) == 42 && kvm_level_from_scalar(0.425f) == 43);
    CHECK(kvm_level_from_scalar(0.994f) == 99 && kvm_level_from_scalar(0.995f) == 100 && kvm_level_from_scalar(1.0f) == 100);
    CHECK(kvm_level_from_scalar(-0.1f) == 0 && kvm_level_from_scalar(1.5f) == 100 && kvm_level_from_scalar(0.0625f) == 6);

    /* VOLUME. */
    CHECK(encodes_to(kvm_volume_msg(PC, KVM_VOL_UP), "KV P+"));
    CHECK(kvm_decode("KV M/", KVM_VOLUME_LEN, KVM_VOLUME, &out) && out.host == MAC && out.action == KVM_VOL_MUTE);

    /* A volume-only report neither sleeps nor wakes the desk. */
    CHECK(kvm_decode("KR ---- --- 42%", KVM_STATE_LEN, KVM_REQUEST, &out));
    CHECK(kvm_request_sleep(out.b) < 0);

    CHECK(kvm_mon_host('m') == MAC && kvm_mon_standby('m'));
    CHECK(kvm_mon_host('X') == KVM_HOST_KEEP && !kvm_mon_standby('X'));
    CHECK(kvm_mon_host(NC) == KVM_HOST_KEEP && !kvm_mon_standby(NC) && !kvm_mon_connected(NC));
    CHECK(KVM_MON_NC_ASLEEP == (KVM_MON_NC | KVM_STANDBY));
    CHECK(kvm_mon_host('^') == KVM_HOST_KEEP && !kvm_mon_standby('^') && !kvm_mon_connected('^'));
    CHECK(kvm_mon_host('~') == KVM_HOST_KEEP && kvm_mon_standby('~') && !kvm_mon_connected('~'));
    CHECK(kvm_mon_connected('P') && kvm_mon_connected('x'));
    CHECK(kvm_connected(KVM_MON_LEFT, 'p') && !kvm_connected(KVM_MON_RIGHT, '~') && !kvm_connected(KVM_HUB_HID, NC));
    CHECK(kvm_hub_port('P') == 0 && kvm_hub_port('M') == 1 && kvm_hub_port('1') == 2 && kvm_hub_port('2') == 3);
    CHECK(kvm_hub_port(NC) > 3 && kvm_hub_byte(2) == '1' && kvm_hub_byte(0xFF) == NC);
}

/* Any byte outside the table, or out of place, drops the whole datagram. */
static void test_valid(void) {
    kvm_msg_t out;
    const char *good[] = {"KS PPPP --- ---", "KS MMMM 42% **%", "KS pmPM 00/ 99%", "KS P^12 --- ---",
                          "KS xX-- --- ---", "KS gG-M --- ---", "KS ^^-- --- ---", "KS ~~P- **/ 07%",
                          "KS ~mPM --- ---", "KR ---- --- ---", "KR xx-- --- ---", "KR XX-- --- ---",
                          "KR P--- --- ---", "KR ---- 42% ---", "KR ---- --- **/", "KV P+", "KV M-", "KV M/"};
    for (const char *g : good) {
        if (!kvm_decode(g, (int)strlen(g), (uint8_t)g[1], &out)) {
            fprintf(stderr, "rejected %s\n", g);
            failures++;
        }
    }
    const char *bad[] = {"KS APPP --- ---", "KS PP3P --- ---", "KS PPpP --- ---", "KS P?PP --- ---",
                         "KS \xFFPPP --- ---", "KR 0--- --- ---", "KR --G- --- ---", "KR ---x --- ---",
                         "KX PPPP --- ---", "kS PPPP --- ---", "KS 1PPP --- ---", "KS -PPP --- ---",
                         "KS P-PP --- ---", "KR ^--- --- ---", "KR -~-- --- ---", "KS PP^P --- ---",
                         "KS PP~- --- ---", "KS `PPP --- ---", "KS @PPP --- ---",
                         /* volume fields */
                         "KS PPPP 100 ---", "KS PPPP 4% ---", "KS PPPP 42 ---", "KS PPPP 42x ---",
                         "KS PPPP *2% ---", "KS PPPP 4*% ---", "KS PPPP --% ---", "KS PPPP -- ---",
                         "KS PPPP --- 4a%", "KS PPPP ---  42", "KS PPPP 42%-42%",
                         /* spaces */
                         "KSPPPP 42% 42% ", "KS PPPP_42% 42%", "KS PPPP 42%_42%", "KS_PPPP --- ---",
                         /* VOLUME */
                         "KV G+", "KV Px", "KVP+ ", "KV P+-", "KV P"};
    for (const char *b : bad) {
        if (kvm_decode(b, (int)strlen(b), (uint8_t)b[1], &out)) {
            fprintf(stderr, "accepted %s\n", b);
            failures++;
        }
    }
    /* A type the receiver didn't ask for is not a match either. */
    CHECK(!kvm_decode("KV P+", KVM_VOLUME_LEN, KVM_STATE, &out));
}

static void test_level_report(void) {
    level_report_t r;
    level_report_init(&r);
    CHECK(!level_report_due(&r, 1000, nullptr));            /* no reading yet */
    level_report_set(&r, 42, 0);
    CHECK(level_report_due(&r, 1000, nullptr));             /* first reading */
    level_report_sent(&r, 1000);
    CHECK(!level_report_due(&r, 1050, nullptr));
    uint8_t raw[KVM_MAX_LEN];
    kvm_msg_t m = level_report_msg(&r, KVM_LEVEL_MAC);
    CHECK(kvm_encode(&m, raw) == KVM_STATE_LEN && memcmp(raw, "KR ---- --- 42%", KVM_STATE_LEN) == 0);
    m = level_report_msg(&r, KVM_LEVEL_PC);
    CHECK(kvm_encode(&m, raw) == KVM_STATE_LEN && memcmp(raw, "KR ---- 42% ---", KVM_STATE_LEN) == 0);
    /* A knob turning: held back to one per LEVEL_REPORT_MIN_MS, the last one still goes. */
    level_report_set(&r, 44, 0);
    CHECK(!level_report_due(&r, 1050, nullptr));
    CHECK(level_report_due(&r, 1100, nullptr));
    level_report_sent(&r, 1100);
    /* Same reading: nothing until the keepalive. */
    const uint8_t shown[KVM_LEVEL_LEN] = {'4', '4', '%'};
    CHECK(!level_report_due(&r, 5000, shown));
    CHECK(level_report_due(&r, 1100 + KVM_LEVEL_REPORT_MS, shown));
    /* A STATE that shows it wrong (the core rebooted): fixed, at most once a second. */
    const uint8_t lost[KVM_LEVEL_LEN] = {'-', '-', '-'};
    CHECK(!level_report_due(&r, 1500, lost));
    CHECK(level_report_due(&r, 2100, lost));
    /* Mute is a change. */
    level_report_set(&r, 44, 1);
    CHECK(level_report_due(&r, 1300, nullptr));
    /* No output device: silent. */
    level_report_set(&r, -1, 0);
    CHECK(!level_report_due(&r, 99999, lost));
}

int main(void) {
    test_level_report();
    test_topology();
    test_standby();
    test_power();
    test_requests();
    test_keyboard();
    test_wire();
    test_valid();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    printf("host_policy: all tests passed\n");
    return EXIT_SUCCESS;
}
