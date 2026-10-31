// deskstrate-host for Windows: the PC's daemon.
//
// Listens for the core's STATE broadcasts on KVM_PORT and follows host/common/host_policy.h:
// extend or duplicate the displays; in split mode without the keyboard, keep them awake,
// sleep them and wake them with the keyboard's host; sleep them for the deck's Screens off;
// wake them when a deck key asks for the PC. Replies to each STATE with a STATUS, which
// carries this PC's display state to the core (and so to the other host).
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
// Options: --core <ipv4>   send to the core directly instead of broadcasting until it answers
//          --no-topology   never change extend/duplicate
//          --displays-off  just sleep the displays and exit (bind a key to it)

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

enum class Mode { Standalone, Service, Agent, DisplaysOff };
Mode g_mode = Mode::Standalone;

HWND g_hwnd = nullptr;
SOCKET g_sock = INVALID_SOCKET;
NOTIFYICONDATAW g_tray = {};

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
HANDLE g_keep_awake_req = nullptr;  // power request: display required while following
bool g_keep_awake = false;

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

void sleep_displays() {
    trace("displays: sleep");
    if (g_mode == Mode::Service) agent::sleep_displays(); else displays::sleep();
}

void wake_displays() {
    trace("displays: wake");
    if (g_mode == Mode::Service) agent::wake_displays(); else displays::wake();
}

// While following the keyboard's host in split mode: no idle display sleep here. A power
// request is per process, so it works from the service as well as standalone.
void set_keep_awake(bool on) {
    if (on == g_keep_awake) return;
    if (!g_keep_awake_req) {
        REASON_CONTEXT why = {};
        why.Version = POWER_REQUEST_CONTEXT_VERSION;
        why.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
        why.Reason.SimpleReasonString = const_cast<LPWSTR>(L"Deskstrate: following the host with the keyboard");
        g_keep_awake_req = PowerCreateRequest(&why);
        if (g_keep_awake_req == INVALID_HANDLE_VALUE) g_keep_awake_req = nullptr;
    }
    if (!g_keep_awake_req) return;
    const BOOL ok = on ? PowerSetRequest(g_keep_awake_req, PowerRequestDisplayRequired)
                       : PowerClearRequest(g_keep_awake_req, PowerRequestDisplayRequired);
    trace("displays: keep awake %s%s", on ? "on" : "off", ok ? "" : " (failed)");
    if (ok) g_keep_awake = on;
}

void set_topology(uint8_t topo) {
    const bool ok = g_mode == Mode::Service ? agent::set_topology(topo) : displays::set_topology(topo);
    if (ok) g_topology = topo;
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
        trace("STATE seq %u from %s: left %s, right %s, keyboard %u, hosts 0x%x", st.seq, ip,
              input_name(st.mon[0]), input_name(st.mon[1]), st.usb[KVM_HUB_HID], st.hosts);
    }

    const host_actions_t act = host_policy_on_state(&g_policy, &st);
    if (act.set_topology && g_topology_enabled) set_topology(act.topology);
    set_keep_awake(act.keep_awake);  // first: released before sleeping
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
    // The core relays it in STATE.hosts: in split mode the other host follows.
    if (g_last_state_ms) send_status(false);
}

void on_power(WPARAM event) {
    if (event == PBT_APMSUSPEND) {
        trace("system: suspending");
        if (g_last_state_ms) send_status(true);
    } else if (event == PBT_APMRESUMEAUTOMATIC) {
        trace("system: resumed");
        g_last_state_ms = 0;
        send_request(KVM_ACT_GET_STATE, 0, 0);
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
            if (wp && g_last_state_ms) send_status(true);
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
            if (g_last_state_ms) send_status(true);
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
    host_policy_init(&g_policy, KVM_IN_DP);
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
    send_request(KVM_ACT_GET_STATE, 0, 0);
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
        } else if (wcscmp(argv[i], L"--displays-off") == 0) {
            g_mode = Mode::DisplaysOff;
        }
    }
    LocalFree(argv);
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int) {
    parse_args();

    if (g_mode == Mode::DisplaysOff) {
        // For a key binding: sleep this PC's displays and exit. The daemon sees them go
        // off and the core tells the other host in split mode to follow.
        displays::sleep();
        return 0;
    }

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
