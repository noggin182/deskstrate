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
        fprintf(stderr, "line %d %s: got %02X %02X %02X %02X, want %02X %02X %02X %02X\n", line, what, got.b[0],
                got.b[1], got.b[2], got.b[3], want.b[0], want.b[1], want.b[2], want.b[3]);
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

static void test_side() {
    // Not split: everything to PC / Mac.
    EXPECT(plan_side(v(MAC, MAC, 1, 1), true), PC, PC, 0, 0);
    EXPECT(plan_side(v(PC, PC, 0, 0), false), MAC, MAC, 1, 1);
    // No state yet: still the not-split meaning.
    EXPECT(plan_side(View{}, true), PC, PC, 0, 0);
    // Split: only the HID hub, to that screen's host.
    EXPECT(plan_side(v(PC, MAC, 0, 0), false), X, X, 1, X);
    EXPECT(plan_side(v(MAC, PC, 0, 1), false), X, X, 0, X);
    EXPECT(plan_side(v(MAC, PC, 0, 1), true), X, X, 1, X);
    EXPECT(plan_side(v(GUEST, PC, 0, 0), true), X, X, 2, X);
    EXPECT(plan_side(v(PC, GUEST, 0, 0), false), X, X, 3, X);
    // A monitor in standby isn't split.
    EXPECT(plan_side(v(PC, MAC | SB, 0, 0), false), MAC, MAC, 1, 1);
}

static void test_toggle_split() {
    // Enter: left and hubs stay, right gets the other host.
    EXPECT(plan_toggle_split(v(PC, PC, 0, 0)), X, MAC, X, X);
    EXPECT(plan_toggle_split(v(MAC, MAC, 1, 1)), X, PC, X, X);
    EXPECT(plan_toggle_split(v(MAC, MAC, 0, 1)), X, PC, X, X);
    EXPECT(plan_toggle_split(v(GUEST, X, 2, 2)), X, PC, X, X);
    // Nothing known: nothing.
    CHECK(is_empty(plan_toggle_split(View{})));
    // Leave: everything follows the HID hub, else the media hub, else the PC.
    EXPECT(plan_toggle_split(v(PC, MAC, 1, 0)), MAC, MAC, 1, 1);
    EXPECT(plan_toggle_split(v(MAC, PC, 0, 1)), PC, PC, 0, 0);
    EXPECT(plan_toggle_split(v(GUEST, MAC, 2, 1)), MAC, MAC, 1, 1);
    EXPECT(plan_toggle_split(v(GUEST, GUEST, 2, 3)), PC, PC, 0, 0);
    EXPECT(plan_leave_split(v(PC, MAC, 1, 0)), MAC, MAC, 1, 1);
    CHECK(is_empty(plan_leave_split(v(PC, PC, 0, 0))));
}

static void test_swap() {
    EXPECT(plan_swap(v(PC, MAC, 0, 0)), MAC, PC, X, X);
    CHECK(is_empty(plan_swap(v(PC, MAC | SB, 0, 0))));
    CHECK(is_empty(plan_swap(View{})));
    // The media hub stays with its screen.
    EXPECT(plan_swap_media(v(PC, MAC, 0, 0)), MAC, PC, X, 1);  // was on the left (PC): left is now Mac
    EXPECT(plan_swap_media(v(PC, MAC, 0, 1)), MAC, PC, X, 0);  // was on the right (Mac): right is now PC
    EXPECT(plan_swap_media(v(PC, MAC, 1, 2)), MAC, PC, X, X);  // on a guest no screen shows: stays
    EXPECT(plan_swap_media(v(GUEST, PC, 0, 0)), PC, GUEST, X, 3);  // right now shows guest 2
    EXPECT(plan_swap_media(v(GUEST, PC, 0, 2)), PC, GUEST, X, 0);  // left now shows the PC
    EXPECT(plan_swap_media(v(PC, PC, 0, 0)), PC, PC, X, X);  // not split: nothing moves
    CHECK(is_empty(plan_swap_media(v(PC, X, 0, 0))));
}

static void test_other_host() {
    CHECK(other_host(v(PC, PC, 0, 0)) == kMac);
    CHECK(other_host(v(PC, PC, 1, 0)) == kPc);
    CHECK(other_host(v(PC, PC, 2, 0)) == kNoHost);
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
