#include "displays.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#include <vector>

#include "host_policy.h"
#include "kvm_proto.h"
#include "log.h"

namespace displays {

namespace {

// Runs fn on a fresh thread attached to the input desktop. A thread that already owns
// windows can't change desktop, and the desktop showing changes (Default, Winlogon) as
// the user signs in and locks, so every action gets its own thread.
template <typename Fn>
void on_input_desktop(Fn fn) {
    struct Ctx {
        Fn *fn;
    } ctx{&fn};
    HANDLE t = CreateThread(
        nullptr, 0,
        [](LPVOID p) -> DWORD {
            HDESK desk = OpenInputDesktop(0, FALSE, GENERIC_ALL);
            if (desk) {
                if (!SetThreadDesktop(desk)) trace("SetThreadDesktop failed: %lu", GetLastError());
            } else {
                trace("OpenInputDesktop failed: %lu (acting on this thread's desktop)", GetLastError());
            }
            (*static_cast<Ctx *>(p)->fn)();
            if (desk) CloseDesktop(desk);
            return 0;
        },
        &ctx, 0, nullptr);
    if (!t) {
        fn();
        return;
    }
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

constexpr BYTE kVcpInput = 0x60;
constexpr BYTE kVcpPower = 0xD6;  // 1 on, 4 standby

// The Dells' input codes (VCP 0x60) that are this PC: DisplayPort.
bool is_pc_input(DWORD v) {
    v &= 0xFF;
    return v == 0x0F || v == 0x10;
}

// Sets DDC/CI power on every monitor whose input is this PC's. A monitor showing another
// host is left alone: that host decides, and a Dell ignores standby from an input it isn't
// showing anyway.
void ddc_power_here(bool on) {
    std::vector<HMONITOR> mons;
    EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL {
            reinterpret_cast<std::vector<HMONITOR> *>(p)->push_back(m);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&mons));
    const DWORD want = on ? 1 : 4;
    for (HMONITOR m : mons) {
        DWORD n = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(m, &n) || n == 0) continue;
        std::vector<PHYSICAL_MONITOR> pms(n);
        if (!GetPhysicalMonitorsFromHMONITOR(m, n, pms.data())) continue;
        for (PHYSICAL_MONITOR &pm : pms) {
            DWORD input = 0, power = 0, max = 0;
            if (!GetVCPFeatureAndVCPFeatureReply(pm.hPhysicalMonitor, kVcpInput, nullptr, &input, &max)) {
                trace("ddc: %ls: no reply (%lu)", pm.szPhysicalMonitorDescription, GetLastError());
                continue;
            }
            if (!is_pc_input(input)) continue;
            if (GetVCPFeatureAndVCPFeatureReply(pm.hPhysicalMonitor, kVcpPower, nullptr, &power, &max) &&
                (power & 0xFF) == want) {
                continue;
            }
            if (SetVCPFeature(pm.hPhysicalMonitor, kVcpPower, want)) {
                trace("ddc: %ls %s", pm.szPhysicalMonitorDescription, on ? "on" : "standby");
            } else {
                trace("ddc: %ls: set power failed (%lu)", pm.szPhysicalMonitorDescription, GetLastError());
            }
        }
        DestroyPhysicalMonitors(n, pms.data());
    }
}

}  // namespace

// DDC standby only: the signal stays on, so the monitors go dark at once with no
// "No signal", and Windows doesn't treat it as display sleep. The keyboard wakes them
// (KD wake -> wake()).
void sleep() {
    on_input_desktop([] { ddc_power_here(false); });
}

void volume(uint8_t kvm_volume) {
    WORD vk = 0;
    switch (kvm_volume) {
        case KVM_VOL_UP: vk = VK_VOLUME_UP; break;
        case KVM_VOL_DOWN: vk = VK_VOLUME_DOWN; break;
        case KVM_VOL_MUTE: vk = VK_VOLUME_MUTE; break;
        default: return;
    }
    on_input_desktop([vk] {
        INPUT in[2] = {};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = vk;
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        if (SendInput(2, in, sizeof(INPUT)) != 2) trace("volume: SendInput failed: %lu", GetLastError());
    });
}

void wake() {
    on_input_desktop([] {
        INPUT in[2] = {};
        in[0].type = in[1].type = INPUT_MOUSE;
        in[0].mi.dwFlags = in[1].mi.dwFlags = MOUSEEVENTF_MOVE;
        in[0].mi.dx = 1;
        in[1].mi.dx = -1;
        if (SendInput(2, in, sizeof in[0]) != 2) trace("SendInput failed: %lu", GetLastError());
        SetThreadExecutionState(ES_DISPLAY_REQUIRED);
        // A monitor in DDC standby doesn't wake on input or signal: switch it on.
        ddc_power_here(true);
    });
}

// Extend or duplicate every display. Windows keeps a monitor switched to another input,
// so duplicate leaves the visible one showing everything.
bool set_topology(uint8_t topo) {
    bool ok = false;
    on_input_desktop([&] {
        const char *name = topo == HOST_TOPO_MIRROR ? "duplicate" : "extend";
        UINT32 paths = 0, modes = 0;
        DISPLAYCONFIG_TOPOLOGY_ID current = {};
        if (GetDisplayConfigBufferSizes(QDC_DATABASE_CURRENT, &paths, &modes) == ERROR_SUCCESS) {
            std::vector<DISPLAYCONFIG_PATH_INFO> p(paths);
            std::vector<DISPLAYCONFIG_MODE_INFO> m(modes);
            if (QueryDisplayConfig(QDC_DATABASE_CURRENT, &paths, p.data(), &modes, m.data(), &current) != ERROR_SUCCESS) {
                current = {};
            }
        }
        const DISPLAYCONFIG_TOPOLOGY_ID want = topo == HOST_TOPO_MIRROR ? DISPLAYCONFIG_TOPOLOGY_CLONE : DISPLAYCONFIG_TOPOLOGY_EXTEND;
        if (current == want) {
            trace("topology: already %s", name);
            ok = true;
            return;
        }
        const UINT32 flags = SDC_APPLY | (topo == HOST_TOPO_MIRROR ? SDC_TOPOLOGY_CLONE : SDC_TOPOLOGY_EXTEND);
        const LONG err = SetDisplayConfig(0, nullptr, 0, nullptr, flags);
        trace("topology: %s -> %ld", name, err);
        ok = err == ERROR_SUCCESS;
    });
    return ok;
}

}  // namespace displays
