/*
 * level_report.h - when a host daemon reports its own volume to the core. Shared by the
 * Windows and Mac daemons, like host_policy.h. No I/O: test with test_host_policy.cpp.
 *
 * The host owns its volume; the core only passes on the last report (kvm_proto.h). A
 * daemon feeds every reading of its output device in with level_report_set(), asks
 * level_report_due() on each reading, each STATE and a timer of LEVEL_REPORT_TICK_MS,
 * and sends "KR ---- <its field>" whenever it says so (then level_report_sent()):
 *   - a change, at most one every LEVEL_REPORT_MIN_MS while a knob turns (the last
 *     reading always goes out),
 *   - every KVM_LEVEL_REPORT_MS as a keepalive, so the core's KVM_LEVEL_EXPIRE_MS shows
 *     a host that has gone,
 *   - a STATE showing this host's field different from the reading (the core rebooted,
 *     or a report was lost), at most one every LEVEL_REPORT_FIX_MS.
 * Nothing is sent while the reading is unknown (no output device).
 */
#ifndef LEVEL_REPORT_H
#define LEVEL_REPORT_H

#include <stdint.h>
#include <string.h>

#include "kvm_proto.h"

#define LEVEL_REPORT_MIN_MS   100u
#define LEVEL_REPORT_FIX_MS   1000u
#define LEVEL_REPORT_TICK_MS  100u

typedef struct {
    uint8_t mine[KVM_LEVEL_LEN];  /* the latest reading, "---" if unknown */
    uint8_t sent[KVM_LEVEL_LEN];  /* the last one sent */
    uint64_t sent_ms;             /* when, 0 = never */
} level_report_t;

static inline void level_report_init(level_report_t *r) {
    memset(r, 0, sizeof *r);
    memset(r->mine, KVM_UNKNOWN, KVM_LEVEL_LEN);
    memset(r->sent, KVM_UNKNOWN, KVM_LEVEL_LEN);
}

/* A reading: level 0-100, or < 0 when there's no output device. */
static inline void level_report_set(level_report_t *r, int level, int muted) {
    kvm_level_set(r->mine, level, muted);
}

/* 1 if a report should go out now. state_field: this host's field in the latest STATE,
 * or NULL (none since the last call, or no core yet). now_ms must not be 0. */
static inline int level_report_due(const level_report_t *r, uint64_t now_ms, const uint8_t *state_field) {
    if (r->mine[0] == KVM_UNKNOWN) return 0;
    if (!r->sent_ms) return 1;
    const uint64_t since = now_ms - r->sent_ms;
    if (memcmp(r->mine, r->sent, KVM_LEVEL_LEN) != 0) return since >= LEVEL_REPORT_MIN_MS;
    if (since >= KVM_LEVEL_REPORT_MS) return 1;
    if (state_field && memcmp(state_field, r->mine, KVM_LEVEL_LEN) != 0) return since >= LEVEL_REPORT_FIX_MS;
    return 0;
}

static inline void level_report_sent(level_report_t *r, uint64_t now_ms) {
    memcpy(r->sent, r->mine, KVM_LEVEL_LEN);
    r->sent_ms = now_ms;
}

/* The REQUEST to send: no slots, only this host's field (KVM_LEVEL_PC / KVM_LEVEL_MAC). */
static inline kvm_msg_t level_report_msg(const level_report_t *r, int host_field) {
    const uint8_t none[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};
    kvm_msg_t m = kvm_msg(KVM_REQUEST, none);
    memcpy(m.level[host_field], r->mine, KVM_LEVEL_LEN);
    return m;
}

#endif /* LEVEL_REPORT_H */
