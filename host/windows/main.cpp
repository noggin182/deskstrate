// deskstrate-host for Windows: the PC's daemon.
//
// Listens for the core's STATE broadcasts on KVM_PORT and follows host/common/host_policy.h:
// extend or duplicate the displays, and wake them when a monitor comes back to the PC or
// the keyboard moves to it. A KV VOLUME message for the PC presses its volume keys (the
// keyboard's Fn+knob while the keyboard is on the Mac), and a KZ SLEEP (the keyboard's
// Sleep key) puts the displays to sleep. It only ever sends the core an empty REQUEST, to get a STATE.
//
// One exe, three ways to run:
//   --service    the normal install: a LocalSystem service started at boot, so it works
//                before anyone signs in and while the PC is locked. It owns the socket and
//                the protocol, and gets suspend/shutdown and display on/off from the SCM.
//                Session 0 can't touch the displays, so it hands each display action to...
//   --agent      ...itself, started as SYSTEM in the console session (agent.h). The agent
//                acts on whichever desktop is showing: sign-in, lock screen or the user's.
//   (neither)    standalone: everything in one process with a tray icon, in the user's
//                session (the Startup-shortcut install, and for debugging). Can't act on
//                the sign-in or lock screen.
//
// The protocol runs on one thread with a hidden window: the socket reports through
// WSAAsyncSelect and a 1 s timer asks for STATE while the core is silent.
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
#include <wtsapi32.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "agent.h"
#include "displays.h"
#include "host_policy.h"
#include "kvm_proto.h"
#include "log.h"

namespace {

constexpr UINT WM_APP_SOCKET = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
constexpr UINT WM_APP_DISPLAY = WM_APP + 3;  // wParam: GUID_CONSOLE_DISPLAY_STATE value
constexpr UINT WM_APP_POWER = WM_APP + 4;    // wParam: PBT_APMSUSPEND / PBT_APMRESUMEAUTOMATIC
constexpr UINT WM_APP_SESSION = WM_APP + 5;  // wParam: WTS_* session change
constexpr UINT WM_APP_STOP = WM_APP + 6;     // wParam: 1 = the PC is shutting down
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerMs = 1000;
constexpr UINT kMenuOpenLog = 1;
constexpr UINT kMenuExit = 2;
const wchar_t kServiceName[] = L"deskstrate-host";

enum class Mode { Standalone, Service, Agent };
Mode g_mode = Mode::Standalone;

HWND g_hwnd = nullptr;
SOCKET g_sock = INVALID_SOCKET;
NOTIFYICONDATAW g_tray = {};

bool g_topology_enabled = true;
in_addr g_core_fixed = {};        // --core, 0 = broadcast until the core answers
in_addr g_core = {};              // where the last STATE came from
ULONGLONG g_last_state_ms = 0;    // GetTickCount64() of the last STATE, 0 = never

host_policy_t g_policy;
uint8_t g_state[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};
bool g_displays_on = true;

SERVICE_STATUS_HANDLE g_svc = nullptr;
SERVICE_STATUS g_svc_status = {};

void report(DWORD state, DWORD exit_code = 0);

std::wstring known_folder(REFKNOWNFOLDERID id) {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SHGetKnownFolderPath(id, 0, nullptr, &base) == S_OK) dir = std::wstring(base) + L"\\Deskstrate";
    CoTaskMemFree(base);
    return dir;
}

// ---- tray (standalone only) ------------------------------------------------

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
    if (cmd == kMenuOpenLog && !hostlog::path().empty()) {
        ShellExecuteW(nullptr, L"open", hostlog::path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (cmd == kMenuExit) {
        DestroyWindow(g_hwnd);
    }
}

// ---- display actions: here (standalone) or through the agent (service) -----

void wake_displays() {
    trace("displays: wake");
    if (g_mode == Mode::Service) agent::wake_displays(); else displays::wake();
}

void set_topology(uint8_t topo) {
    if (!(g_mode == Mode::Service ? agent::set_topology(topo) : displays::set_topology(topo))) {
        trace("topology: not applied");
    }
}

// ---- protocol --------------------------------------------------------------

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

// An empty REQUEST (all 0xFF) changes nothing: the core just answers with its STATE.
void ask_for_state() {
    const uint8_t none[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};
    const kvm_msg_t req = kvm_msg(KVM_REQUEST, none);
    send_to_core(&req, sizeof req);
}

const char *mon_name(uint8_t b) {
    if (b == KVM_UNKNOWN) return "--";
    if (kvm_mon_standby(b)) return "standby";
    switch (kvm_mon_host(b)) {
        case KVM_HOST_PC: return "PC";
        case KVM_HOST_MAC: return "Mac";
        case KVM_HOST_GUEST: return "HDMI";
        default: return "?";
    }
}

void on_state(const uint8_t b[KVM_SLOTS], const sockaddr_in &from) {
    const bool first = !g_last_state_ms;
    g_core = from.sin_addr;
    g_last_state_ms = GetTickCount64();
    if (first || memcmp(b, g_state, KVM_SLOTS) != 0) {
        char ip[INET_ADDRSTRLEN] = "";
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip);
        trace("STATE %02X %02X %02X %02X from %s: left %s, right %s", b[0], b[1], b[2], b[3], ip,
              mon_name(b[KVM_MON_LEFT]), mon_name(b[KVM_MON_RIGHT]));
    }
    memcpy(g_state, b, KVM_SLOTS);

    const host_actions_t act = host_policy_on_state(&g_policy, b);
    if (act.set_topology && g_topology_enabled) set_topology(act.topology);
    if (act.wake_displays) wake_displays();

    wchar_t tip[64];
    swprintf(tip, 64, L"left %hs, right %hs", mon_name(b[KVM_MON_LEFT]), mon_name(b[KVM_MON_RIGHT]));
    set_tip(tip);
}

// The keyboard's Fn+knob, broadcast by the keyboard's ESP for whichever host doesn't
// have the keyboard: [host, enum kvm_volume, FF, FF].
void on_volume(const uint8_t b[KVM_SLOTS]) {
    if (b[0] != KVM_HOST_PC) return;
    trace("volume %u", b[1]);
    if (g_mode == Mode::Service) agent::volume(b[1]); else displays::volume(b[1]);
}

// The keyboard's Sleep key, broadcast by the keyboard's ESP to both hosts. It sends each
// one three times in case one is lost: act on the first only.
void on_sleep() {
    static ULONGLONG last = 0;
    const ULONGLONG now = GetTickCount64();
    if (last && now - last < 1000) return;
    last = now;
    trace("displays: sleep (keyboard)");
    if (g_mode == Mode::Service) agent::sleep_displays(); else displays::sleep();
}

void on_datagram(const uint8_t *buf, int n, const sockaddr_in &from) {
    kvm_msg_t msg;
    if (kvm_decode(buf, n, KVM_STATE, &msg)) on_state(msg.b, from);
    else if (kvm_decode(buf, n, KVM_VOLUME, &msg)) on_volume(msg.b);
    else if (kvm_decode(buf, n, KVM_SLEEP, &msg)) on_sleep();
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
    if (g_last_state_ms && now - g_last_state_ms > KVM_TIMEOUT_MS) {
        trace("core silent for %u ms: looking for it again", KVM_TIMEOUT_MS);
        g_last_state_ms = 0;
        set_tip(L"looking for the core");
    }
    if (!g_last_state_ms) ask_for_state();
}

void on_display_state(DWORD value) {  // GUID_CONSOLE_DISPLAY_STATE: 0 off, 1 on, 2 dimmed
    const bool on = value != 0;
    if (on == g_displays_on) return;
    g_displays_on = on;
    trace("displays: %s", on ? "on" : "off");
}

void on_power(WPARAM event) {
    if (event == PBT_APMSUSPEND) {
        trace("system: suspending");
    } else if (event == PBT_APMRESUMEAUTOMATIC) {
        trace("system: resumed");
        g_last_state_ms = 0;
        ask_for_state();
    }
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
        case WM_APP_DISPLAY:
            on_display_state((DWORD)wp);
            return 0;
        case WM_APP_POWER:
            on_power(wp);
            return 0;
        case WM_APP_SESSION:
            if (wp == WTS_CONSOLE_CONNECT || wp == WTS_CONSOLE_DISCONNECT) {
                // Another session took the console (user switch): the next action starts an
                // agent there.
                trace("session: console %s", wp == WTS_CONSOLE_CONNECT ? "connected" : "disconnected");
                agent::stop();
            }
            return 0;
        case WM_APP_STOP:
            DestroyWindow(hwnd);
            return 0;
        case WM_POWERBROADCAST:  // standalone; the service gets these from the SCM
            if (wp == PBT_POWERSETTINGCHANGE) {
                const auto *s = reinterpret_cast<const POWERBROADCAST_SETTING *>(lp);
                if (IsEqualGUID(s->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) && s->DataLength >= sizeof(DWORD)) {
                    DWORD v;
                    memcpy(&v, s->Data, sizeof v);
                    on_display_state(v);
                }
            } else {
                on_power(wp);
            }
            return TRUE;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_DESTROY:
            if (g_tray.cbSize) Shell_NotifyIconW(NIM_DELETE, &g_tray);
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
    // Not inherited by the agent the service starts.
    SetHandleInformation(reinterpret_cast<HANDLE>(g_sock), HANDLE_FLAG_INHERIT, 0);
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

// Window, socket, timer, then the message loop until the window goes. 0 on a clean exit.
int run_protocol(HINSTANCE inst) {
    host_policy_init(&g_policy, KVM_HOST_PC);
    trace("deskstrate-host starting (%s, topology %s)", g_mode == Mode::Service ? "service" : "standalone",
          g_topology_enabled ? "on" : "off");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = L"deskstrate-host";
    RegisterClassW(&wc);
    // A hidden top-level window: message-only windows don't get power broadcasts.
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Deskstrate", WS_OVERLAPPED, 0, 0, 0, 0,
                             nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;
    if (!open_socket()) return 2;
    if (g_mode == Mode::Standalone) {
        add_tray();
        RegisterPowerSettingNotification(g_hwnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    }
    SetTimer(g_hwnd, kTimerId, kTimerMs, nullptr);
    ask_for_state();
    // Only now: the SCM's handler posts controls to g_hwnd.
    if (g_mode == Mode::Service) report(SERVICE_RUNNING);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    closesocket(g_sock);
    WSACleanup();
    return 0;
}

// ---- service ---------------------------------------------------------------

void report(DWORD state, DWORD exit_code) {
    g_svc_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_svc_status.dwCurrentState = state;
    g_svc_status.dwWin32ExitCode = exit_code;
    g_svc_status.dwControlsAccepted =
        state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN |
                                       SERVICE_ACCEPT_POWEREVENT | SERVICE_ACCEPT_SESSIONCHANGE
                                 : 0;
    g_svc_status.dwWaitHint = state == SERVICE_START_PENDING || state == SERVICE_STOP_PENDING ? 5000 : 0;
    SetServiceStatus(g_svc, &g_svc_status);
}

// Runs on the dispatcher thread: hand everything to the protocol thread's window.
DWORD WINAPI service_handler(DWORD control, DWORD event, LPVOID data, LPVOID) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_PRESHUTDOWN:
            report(SERVICE_STOP_PENDING);
            PostMessageW(g_hwnd, WM_APP_STOP, control == SERVICE_CONTROL_PRESHUTDOWN, 0);
            return NO_ERROR;
        case SERVICE_CONTROL_POWEREVENT:
            if (event == PBT_POWERSETTINGCHANGE) {
                const auto *s = static_cast<const POWERBROADCAST_SETTING *>(data);
                if (IsEqualGUID(s->PowerSetting, GUID_CONSOLE_DISPLAY_STATE) && s->DataLength >= sizeof(DWORD)) {
                    DWORD v;
                    memcpy(&v, s->Data, sizeof v);
                    PostMessageW(g_hwnd, WM_APP_DISPLAY, v, 0);
                }
            } else {
                PostMessageW(g_hwnd, WM_APP_POWER, event, 0);
            }
            return NO_ERROR;
        case SERVICE_CONTROL_SESSIONCHANGE:
            PostMessageW(g_hwnd, WM_APP_SESSION, event, 0);
            return NO_ERROR;
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
    }
    return ERROR_CALL_NOT_IMPLEMENTED;
}

void WINAPI service_main(DWORD, LPWSTR *) {
    g_svc = RegisterServiceCtrlHandlerExW(kServiceName, service_handler, nullptr);
    if (!g_svc) return;
    report(SERVICE_START_PENDING);
    RegisterPowerSettingNotification(g_svc, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_SERVICE_HANDLE);
    const int rc = run_protocol(GetModuleHandleW(nullptr));
    agent::stop();
    trace("service stopped (%d)", rc);
    report(SERVICE_STOPPED, rc ? ERROR_SERVICE_SPECIFIC_ERROR : NO_ERROR);
}

void parse_args() {
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++) {
        if (wcscmp(argv[i], L"--no-topology") == 0) {
            g_topology_enabled = false;
        } else if (wcscmp(argv[i], L"--core") == 0 && i + 1 < argc) {
            InetPtonW(AF_INET, argv[++i], &g_core_fixed);
        } else if (wcscmp(argv[i], L"--service") == 0) {
            g_mode = Mode::Service;
        } else if (wcscmp(argv[i], L"--agent") == 0) {
            g_mode = Mode::Agent;
        }
    }
    LocalFree(argv);
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    parse_args();

    if (g_mode == Mode::Agent) {
        hostlog::open(known_folder(FOLDERID_ProgramData), L"agent.log");
        return agent::run();
    }

    if (g_mode == Mode::Service) {
        hostlog::open(known_folder(FOLDERID_ProgramData), L"service.log");
        wchar_t name[] = L"deskstrate-host";
        const SERVICE_TABLE_ENTRYW table[] = {{name, service_main}, {nullptr, nullptr}};
        if (!StartServiceCtrlDispatcherW(table)) {
            trace("not started by the service manager (%lu): install with install.ps1", GetLastError());
            return 1;
        }
        return 0;
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\deskstrate-host");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;  // already running
    hostlog::open(known_folder(FOLDERID_LocalAppData), L"host.log");
    const int rc = run_protocol(inst);
    if (rc == 2) {
        MessageBoxW(nullptr, L"Couldn't open UDP port 18200. Is the Deskstrate service or another copy running?",
                    L"Deskstrate", MB_ICONERROR);
    }
    hostlog::close();
    ReleaseMutex(single);
    return rc;
}
