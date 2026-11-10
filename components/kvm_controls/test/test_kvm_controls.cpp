// Tests for kvm_controls. Run from the repo root (CI does, in host.yml):
//   c++ -std=c++17 -Wall -I components/kvm_proto/include -I components/kvm_controls/include
//       components/kvm_controls/kvm_controls.cpp components/kvm_controls/test/test_kvm_controls.cpp && ./a.out
#include <cstdio>
#include <cstring>

#include "kvm_controls.h"

using namespace kvm_controls;

static int failures = 0;

static View v(uint8_t l, uint8_t r, uint8_t hid, uint8_t media) {
    View x;
    x.b[0] = l;
    x.b[1] = r;
    x.b[2] = hid;
    x.b[3] = media;
    return x;
}

static void expect(const Target &got, const View &want, const char *what, int line) {
    if (memcmp(got.b, want.b, KVM_SLOTS) != 0) {
        fprintf(stderr, "line %d %s: got %.4s, want %.4s\n", line, what, reinterpret_cast<const char *>(got.b),
                reinterpret_cast<const char *>(want.b));
        failures++;
    }
}
#define EXPECT(got, ...) expect(got, v(__VA_ARGS__), #got, __LINE__)
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            fprintf(stderr, "line %d: CHECK(%s) failed\n", __LINE__, #cond);        \
            failures++;                                                             \
        }                                                                           \
    } while (0)

constexpr uint8_t PC = KVM_HOST_PC, MAC = KVM_HOST_MAC, GUEST = KVM_HOST_GUEST, SB = KVM_STANDBY, X = KVM_UNKNOWN;
// A monitor the core can't see, in a STATE ('^', or '~' while the desk is asleep).
constexpr uint8_t NC = KVM_MON_NC, NCS = KVM_MON_NC_ASLEEP;
// Hub bytes.
constexpr uint8_t HP = kPc, HM = kMac, G1 = kGuest1, G2 = kGuest2;

static void test_side() {
    // Not split: everything to PC / Mac.
    EXPECT(plan_side(v(MAC, MAC, HM, HM), true), PC, PC, HP, HP);
    EXPECT(plan_side(v(PC, PC, HP, HP), false), MAC, MAC, HM, HM);
    // No state yet: still the not-split meaning.
    EXPECT(plan_side(View{}, true), PC, PC, HP, HP);
    // Split: only the HID hub, to that screen's host.
    EXPECT(plan_side(v(PC, MAC, HP, HP), false), X, X, HM, X);
    EXPECT(plan_side(v(MAC, PC, HP, HM), false), X, X, HP, X);
    EXPECT(plan_side(v(MAC, PC, HP, HM), true), X, X, HM, X);
    EXPECT(plan_side(v(GUEST, PC, HP, HP), true), X, X, G1, X);
    EXPECT(plan_side(v(PC, GUEST, HP, HP), false), X, X, G2, X);
    // Standby (the desk asleep) doesn't hide a monitor's host.
    EXPECT(plan_side(v(PC | SB, MAC | SB, HP, HP), false), X, X, HM, X);
}

static void test_toggle_split() {
    // Enter: left stays with the media hub; right and the HID hub get the other host.
    EXPECT(plan_toggle_split(v(PC, PC, HP, HP)), X, MAC, HM, HP);
    EXPECT(plan_toggle_split(v(MAC, MAC, HM, HM)), X, PC, HP, HM);
    EXPECT(plan_toggle_split(v(MAC, MAC, HP, HP)), X, PC, HP, HM);
    EXPECT(plan_toggle_split(v(GUEST, NC, G1, G1)), X, PC, HP, G1);
    EXPECT(plan_toggle_split(v(GUEST | SB, NCS, G1, G1)), X, PC, HP, G1);
    EXPECT(plan_toggle_split(v(PC | SB, PC | SB, HP, HP)), X, MAC, HM, HP);
    // Deck chords.
    EXPECT(split(kPc, kMac), PC, MAC, HM, HP);
    EXPECT(split(kMac, kPc), MAC, PC, HP, HM);
    // Nothing known: nothing.
    CHECK(is_empty(plan_toggle_split(View{})));
    // Leave: everything follows the media hub, else the HID hub, else the PC.
    EXPECT(plan_toggle_split(v(PC, MAC, HM, HP)), PC, PC, HP, HP);
    EXPECT(plan_toggle_split(v(MAC, PC, HP, HM)), MAC, MAC, HM, HM);
    EXPECT(plan_toggle_split(v(GUEST, MAC, G1, HM)), MAC, MAC, HM, HM);
    EXPECT(plan_toggle_split(v(GUEST, MAC, HM, G1)), MAC, MAC, HM, HM);
    EXPECT(plan_toggle_split(v(GUEST, GUEST, G1, G2)), PC, PC, HP, HP);
    EXPECT(plan_leave_split(v(PC, MAC, HM, HP)), PC, PC, HP, HP);
    // Fn+Up twice comes back to where it started.
    EXPECT(plan_toggle_split(v(PC, MAC, HM, HP)), PC, PC, HP, HP);
    CHECK(is_empty(plan_leave_split(v(PC, PC, HP, HP))));
}

static void test_swap() {
    EXPECT(plan_swap(v(PC, MAC, HP, HP)), MAC, PC, X, X);
    EXPECT(plan_swap(v(PC | SB, MAC | SB, HP, HP)), MAC, PC, X, X);
    CHECK(is_empty(plan_swap(View{})));
    // The media hub stays with its screen.
    EXPECT(plan_swap_media(v(PC, MAC, HP, HP)), MAC, PC, X, HM);  // was on the left (PC): left is now Mac
    EXPECT(plan_swap_media(v(PC, MAC, HP, HM)), MAC, PC, X, HP);  // was on the right (Mac): right is now PC
    EXPECT(plan_swap_media(v(PC, MAC, HM, G1)), MAC, PC, X, X);  // on a guest no screen shows: stays
    EXPECT(plan_swap_media(v(GUEST, PC, HP, HP)), PC, GUEST, X, G2);  // right now shows guest 2
    EXPECT(plan_swap_media(v(GUEST, PC, HP, G1)), PC, GUEST, X, HP);  // left now shows the PC
    EXPECT(plan_swap_media(v(PC, PC, HP, HP)), PC, PC, X, X);  // not split: nothing moves
    CHECK(is_empty(plan_swap_media(v(PC, NC, HP, HP))));
    CHECK(is_empty(plan_swap_media(v(PC | SB, NCS, HP, HP))));
    CHECK(!is_split(v(PC, NC, HP, HP)) && monitor_host(v(NCS, PC, HP, HP), KVM_MON_LEFT) == kNoHost);
}

static void test_other_host() {
    CHECK(other_host(v(PC, PC, HP, HP)) == kMac);
    CHECK(other_host(v(PC, PC, HM, HP)) == kPc);
    CHECK(other_host(v(PC, PC, G1, HP)) == kNoHost);
    CHECK(other_host(View{}) == kNoHost);
}

int main() {
    test_side();
    test_toggle_split();
    test_swap();
    test_other_host();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("kvm_controls: all tests passed\n");
    return 0;
}
