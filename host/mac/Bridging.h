/* C++ side of the Mac daemon (Swift C++ interop): the wire format and the shared host policy. */
#include "kvm_proto.h"
#include "host_policy.h"
#include "level_report.h"

/* level_report_due() for the Mac's field of a STATE (NULL: no STATE), so Swift doesn't
 * have to take a pointer into the message's nested arrays. */
static inline int mac_level_due(const level_report_t *r, uint64_t now_ms, const kvm_msg_t *state) {
    return level_report_due(r, now_ms, state ? state->level[KVM_LEVEL_MAC] : NULL);
}
