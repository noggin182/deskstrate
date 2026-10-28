// deskstrate-host for Windows: the PC's daemon. A tray app started at logon (it has to run
// in the user's session to sleep and wake the displays and change the display topology).
//
// Listens for the core's STATE broadcasts on KVM_PORT and follows host/common/host_policy.h:
// extend or duplicate the displays, sleep them for Screens off, wake them when a monitor
// comes back to the PC, and if they wake locally during Screens off, ask the core to put
// those monitors back on DisplayPort. Replies to each STATE with a STATUS.
//
// Everything runs on the one UI thread: the socket reports through WSAAsyncSelect and a
// 1 s timer asks for STATE while the core is silent.
//
// Options: --core <ipv4>  send to the core directly instead of broadcasting until it answers
//          --no-topology  never change extend/duplicate

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "host_policy.h"
#include "kvm_proto.h"

namespace {

constexpr UINT WM_APP_SOCKET = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 1000;
constexpr UINT kMenuOpenLog = 1;
constexpr UINT kMenuExit = 2;
constexpr ULONGLONG kLocalWakeGuardMs = 3000;  // ignore a display wake this soon after sleeping it

HWND g_hwnd = nullptr;
SOCKET g_sock = INVALID_SOCKET;
NOTIFYICONDATAW g_tray = {};
FILE *g_log = nullptr;
std::wstring g_log_path;

bool g_topology_enabled = true;
in_addr g_core_fixed = {};        // --core, 0 = broadcast until the core answers
in_addr g_core = {};              // where the last STATE came from
uint64_t g_core_counter = 0;      // last accepted counter from the core
ULONGLONG g_last_state_ms = 0;    // GetTickCount64() of the last STATE, 0 = never
uint32_t g_req_id = 0;
uint64_t g_last_counter = 0;

host_policy_t g_policy;
kvm_state_t g_state = {};
bool g_displays_on = true;
uint8_t g_topology = KVM_TOPO_NONE;
ULONGLONG g_slept_ms = 0;

// ---- logging ---------------------------------------------------------------

void trace(const char *fmt, ...) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[512];
    int n = snprintf(line, sizeof line, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", t.wYear, t.wMonth,
                     t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - n, fmt, ap);
    va_end(ap);
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
    if (g_log) {
        fprintf(g_log, "%s\n", line);
        fflush(g_log);
    }
}

// %LOCALAPPDATA%\Deskstrate\host.log, started afresh once it passes 1 MB.
void open_log() {
    PWSTR base = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base) != S_OK) return;
    std::wstring dir = std::wstring(base) + L"\\Deskstrate";
    CoTaskMemFree(base);
    CreateDirectoryW(dir.c_str(), nullptr);
    g_log_path = dir + L"\\host.log";
    WIN32_FILE_ATTRIBUTE_DATA info;
    const bool big = GetFileAttributesExW(g_log_path.c_str(), GetFileExInfoStandard, &info) &&
                     (info.nFileSizeHigh || info.nFileSizeLow > 1024 * 1024);
    g_log = _wfopen(g_log_path.c_str(), big ? L"w" : L"a");
}

// ---- tray ------------------------------------------------------------------

void set_tip(const wchar_t *status) {
    if (!g_tray.cbSize) return;
    swprintf(g_tray.szTip, sizeof g_tray.szTip / sizeof g_tray.szTip[0], L"Deskstrate: %s", status);
    g_tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
}

void add_tray() {
    g_tray.cbSize = sizeof g_tray;
    g_tray.hWnd = g_hwnd;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = WM_APP_TRAY;
    g_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_tray.szTip, L"Deskstrate: looking for the core");
    Shell_NotifyIconW(NIM_ADD, &g_tray);
}

void show_menu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, g_tray.szTip);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuOpenLog, L"Open log");
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);  // so the menu closes when clicking elsewhere
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(menu);
    if (cmd == kMenuOpenLog && !g_log_path.empty()) {
        ShellExecuteW(nullptr, L"open", g_log_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (cmd == kMenuExit) {
        DestroyWindow(g_hwnd);
    }
}

// ---- displays --------------------------------------------------------------

void sleep_displays() {
    trace("displays: sleep (Screens off)");
    g_slept_ms = GetTickCount64();
    // Handled by DefWindowProc; sent to our own window so no other app can stall it.
    SendMessageW(g_hwnd, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
}

void wake_displays() {
    trace("displays: wake");
    // SC_MONITORPOWER -1 doesn't wake Windows 10/11 reliably; input does. Move 1 px and back.
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = in[1].mi.dwFlags = MOUSEEVENTF_MOVE;
    in[0].mi.dx = 1;
    in[1].mi.dx = -1;
    SendInput(2, in, sizeof in[0]);
    SetThreadExecutionState(ES_DISPLAY_REQUIRED);
}

// Extend or duplicate every display. Windows keeps a monitor switched to another input,
// so duplicate leaves the visible one showing everything.
void set_topology(uint8_t topo) {
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
        trace("topology: already %s", topo == KVM_TOPO_MIRROR ? "duplicate" : "extend");
        g_topology = topo;
        return;
    }
    const UINT32 flags = SDC_APPLY | (topo == KVM_TOPO_MIRROR ? SDC_TOPOLOGY_CLONE : SDC_TOPOLOGY_EXTEND);
    const LONG err = SetDisplayConfig(0, nullptr, 0, nullptr, flags);
    trace("topology: %s -> %ld", topo == KVM_TOPO_MIRROR ? "duplicate" : "extend", err);
    if (err == ERROR_SUCCESS) g_topology = topo;
}

// ---- protocol --------------------------------------------------------------

uint64_t next_counter() {
    // KVM_COUNTER for hosts: Unix time in ms, kept strictly increasing.
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const uint64_t t100ns = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    uint64_t ms = t100ns / 10000 - 11644473600000ull;
    if (ms <= g_last_counter) ms = g_last_counter + 1;
    g_last_counter = ms;
    return ms;
}

void fill_hdr(kvm_hdr_t &hdr, uint8_t type, uint8_t flags) {
    hdr.magic = KVM_MAGIC;
    hdr.version = KVM_VERSION;
    hdr.type = type;
    hdr.sender = KVM_NODE_PC;
    hdr.flags = flags;
    hdr.counter = next_counter();
}

void send_to_core(const void *buf, int len) {
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(KVM_PORT);
    if (g_last_state_ms) {
        to.sin_addr = g_core;
    } else if (g_core_fixed.s_addr) {
        to.sin_addr = g_core_fixed;
    } else {
        to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }
    sendto(g_sock, static_cast<const char *>(buf), len, 0, reinterpret_cast<sockaddr *>(&to), sizeof to);
}

void send_request(uint8_t action, uint8_t a, uint8_t b) {
    kvm_request_t req = {};
    fill_hdr(req.hdr, KVM_REQUEST, 0);
    req.req_id = ++g_req_id;
    req.action = action;
    req.a = a;
    req.b = b;
    send_to_core(&req, sizeof req);
}

void send_status(bool going_down) {
    kvm_status_t st = {};
    fill_hdr(st.hdr, KVM_STATUS, going_down ? KVM_F_GOING_DOWN : 0);
    st.ack_seq = g_state.seq;
    st.awake = going_down ? 0 : 1;
    st.displays = g_displays_on ? KVM_DISP_ON : KVM_DISP_ASLEEP;
    st.topology = g_topology;
    st.mon_count = 0;  // the core reads the monitors itself
    send_to_core(&st, sizeof st);
}

const char *input_name(uint8_t in) {
    switch (in) {
        case KVM_IN_KEEP: return "--";
        case KVM_IN_DP: return "PC";
        case KVM_IN_HDMI: return "HDMI";
        case KVM_IN_USBC: return "Mac";
        case KVM_IN_OFF: return "off";
        default: return "?";
    }
}

void on_state(const kvm_state_t &st, const sockaddr_in &from) {
    const bool first = !g_last_state_ms;
    g_core = from.sin_addr;
    g_last_state_ms = GetTickCount64();
    const bool changed = first || st.seq != g_state.seq || !(st.hdr.flags & KVM_F_HEARTBEAT);
    g_state = st;
    if (changed) {
        char ip[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        trace("STATE seq %u from %s: left %s, right %s, off 0x%x", st.seq, ip, input_name(st.mon[0]),
            input_name(st.mon[1]), st.off);
    }

    const host_actions_t act = host_policy_on_state(&g_policy, &st);
    if (act.set_topology && g_topology_enabled) set_topology(act.topology);
    if (act.sleep_displays) sleep_displays();
    if (act.wake_displays) wake_displays();

    wchar_t tip[64];
    swprintf(tip, 64, L"left %hs, right %hs", input_name(st.mon[0]), input_name(st.mon[1]));
    set_tip(tip);
    send_status(false);
}

void on_datagram(const uint8_t *buf, int n, const sockaddr_in &from) {
    if (n < (int)sizeof(kvm_hdr_t)) return;
    kvm_hdr_t hdr;
    memcpy(&hdr, buf, sizeof hdr);
    if (hdr.magic != KVM_MAGIC || hdr.version != KVM_VERSION || hdr.sender != KVM_NODE_CORE) return;
    if (hdr.counter <= g_core_counter) return;  // replayed or reordered
    g_core_counter = hdr.counter;
    if (hdr.type == KVM_STATE && n == (int)sizeof(kvm_state_t)) {
        kvm_state_t st;
        memcpy(&st, buf, sizeof st);  // decode by copy, never by casting the buffer
        on_state(st, from);
    } else if (hdr.type == KVM_RESULT && n == (int)sizeof(kvm_result_t)) {
        kvm_result_t res;
        memcpy(&res, buf, sizeof res);
        if (res.code != KVM_RES_OK) trace("RESULT %u: code %u", res.req_id, res.code);
    }
}

void on_socket() {
    for (;;) {
        uint8_t buf[256];
        sockaddr_in from = {};
        int from_len = sizeof from;
        const int n = recvfrom(g_sock, reinterpret_cast<char *>(buf), sizeof buf, 0,
                               reinterpret_cast<sockaddr *>(&from), &from_len);
        if (n == SOCKET_ERROR) return;  // WSAEWOULDBLOCK: drained
        on_datagram(buf, n, from);
    }
}

void on_timer() {
    const ULONGLONG now = GetTickCount64();
    if (g_last_state_ms && now - g_last_state_ms > KVM_HOST_TIMEOUT_MS) {
        trace("core silent for %u ms: looking for it again", KVM_HOST_TIMEOUT_MS);
        g_last_state_ms = 0;
        g_core_counter = 0;  // the core may have rebooted with a fresh NVS
        set_tip(L"looking for the core");
    }
    if (!g_last_state_ms) send_request(KVM_ACT_GET_STATE, 0, 0);
}

void on_display_state(DWORD value) {  // GUID_CONSOLE_DISPLAY_STATE: 0 off, 1 on, 2 dimmed
    const bool on = value != 0;
    if (on == g_displays_on) return;
    g_displays_on = on;
    trace("displays: %s", on ? "on" : "off");
    if (on && GetTickCount64() - g_slept_ms > kLocalWakeGuardMs) {
        // Woken here (mouse, keyboard) while Screens off holds our monitors: take them back.
        const uint8_t reclaim = host_policy_reclaim(&g_policy);
        for (uint8_t m = 0; m < 2; m++) {
            if (reclaim & (1u << m)) {
                trace("woken locally: asking for monitor %u back", m);
                send_request(KVM_ACT_MON, m, g_policy.my_input);
            }
        }
    }
    if (g_last_state_ms) send_status(false);
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_SOCKET:
            if (WSAGETSELECTEVENT(lp) == FD_READ) on_socket();
            return 0;
        case WM_TIMER:
            if (wp == kTimerId) on_timer();
            return 0;
        case WM_APP_TRAY:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP) show_menu();
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_POWERSETTINGCHANGE) {
                const auto *s = reinterpret_cast<const POWERBROADCAST_SETTING *>(lp);
                if (IsEqualGUID(s->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) && s->DataLength >= sizeof(DWORD)) {
                    DWORD v;
                    memcpy(&v, s->Data, sizeof v);
                    on_display_state(v);
                }
            } else if (wp == PBT_APMSUSPEND) {
                trace("system: suspending");
                if (g_last_state_ms) send_status(true);
            } else if (wp == PBT_APMRESUMEAUTOMATIC) {
                trace("system: resumed");
                g_last_state_ms = 0;
                send_request(KVM_ACT_GET_STATE, 0, 0);
            }
            return TRUE;
        case WM_QUERYENDSESSION:
            if (g_last_state_ms) send_status(true);
            return TRUE;
        case WM_DESTROY:
            Shell_NotifyIconW(NIM_DELETE, &g_tray);
            PostQuitMessage(0);
            return 0;
    }
    static const UINT taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    if (g_tray.cbSize && msg == taskbar_created) {
        Shell_NotifyIconW(NIM_ADD, &g_tray);  // Explorer restarted
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool open_socket() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == INVALID_SOCKET) return false;
    BOOL yes = TRUE;
    setsockopt(g_sock, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char *>(&yes), sizeof yes);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(KVM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(g_sock, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == SOCKET_ERROR) {
        trace("bind UDP %u failed: %d", KVM_PORT, WSAGetLastError());
        return false;
    }
    WSAAsyncSelect(g_sock, g_hwnd, WM_APP_SOCKET, FD_READ);
    return true;
}

void parse_args() {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        if (wcscmp(argv[i], L"--no-topology") == 0) {
            g_topology_enabled = false;
        } else if (wcscmp(argv[i], L"--core") == 0 && i + 1 < argc) {
            InetPtonW(AF_INET, argv[++i], &g_core_fixed);
        }
    }
    LocalFree(argv);
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\deskstrate-host");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // already running

    open_log();
    parse_args();
    host_policy_init(&g_policy, KVM_IN_DP);
    trace("deskstrate-host starting (topology %s)", g_topology_enabled ? "on" : "off");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"deskstrate-host";
    RegisterClassW(&wc);
    // A hidden top-level window: message-only windows don't get power broadcasts.
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Deskstrate", WS_OVERLAPPED, 0, 0, 0, 0,
                             nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;

    if (!open_socket()) {
        MessageBoxW(nullptr, L"Couldn't open UDP port 18200. Is another copy running?", L"Deskstrate", MB_ICONERROR);
        return 1;
    }
    add_tray();
    RegisterPowerSettingNotification(g_hwnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    SetTimer(g_hwnd, kTimerId, kTimerMs, nullptr);
    send_request(KVM_ACT_GET_STATE, 0, 0);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    closesocket(g_sock);
    WSACleanup();
    if (g_log) fclose(g_log);
    ReleaseMutex(single);
    return 0;
}
