#include "displays.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

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

}  // namespace

void sleep() {
    on_input_desktop([] {
        // DefWindowProc carries out SC_MONITORPOWER; a throwaway window on this desktop
        // means no other app's window can stall it.
        HWND w = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
        DefWindowProcW(w, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
        if (w) DestroyWindow(w);
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
    });
}

// Extend or duplicate every display. Windows keeps a monitor switched to another input,
// so duplicate leaves the visible one showing everything.
bool set_topology(uint8_t topo) {
    bool ok = false;
    on_input_desktop([&] {
        const char *name = topo == KVM_TOPO_MIRROR ? "duplicate" : "extend";
        UINT32 paths = 0, modes = 0;
        DISPLAYCONFIG_TOPOLOGY_ID current = {};
        if (GetDisplayConfigBufferSizes(QDC_DATABASE_CURRENT, &paths, &modes) == ERROR_SUCCESS) {
            std::vector<DISPLAYCONFIG_PATH_INFO> p(paths);
            std::vector<DISPLAYCONFIG_MODE_INFO> m(modes);
            if (QueryDisplayConfig(QDC_DATABASE_CURRENT, &paths, p.data(), &modes, m.data(), &current) != ERROR_SUCCESS) {
                current = {};
            }
        }
        const DISPLAYCONFIG_TOPOLOGY_ID want = topo == KVM_TOPO_MIRROR ? DISPLAYCONFIG_TOPOLOGY_CLONE : DISPLAYCONFIG_TOPOLOGY_EXTEND;
        if (current == want) {
            trace("topology: already %s", name);
            ok = true;
            return;
        }
        const UINT32 flags = SDC_APPLY | (topo == KVM_TOPO_MIRROR ? SDC_TOPOLOGY_CLONE : SDC_TOPOLOGY_EXTEND);
        const LONG err = SetDisplayConfig(0, nullptr, 0, nullptr, flags);
        trace("topology: %s -> %ld", name, err);
        ok = err == ERROR_SUCCESS;
    });
    return ok;
}

}  // namespace displays
