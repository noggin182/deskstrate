#include "agent.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>
#include <string>

#include "displays.h"
#include "kvm_proto.h"
#include "log.h"

namespace agent {

// ---- agent side ------------------------------------------------------------

int run() {
    trace("agent: started in session %lu", [] {
        DWORD s = 0;
        ProcessIdToSessionId(GetCurrentProcessId(), &s);
        return s;
    }());
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    std::string pending;
    char buf[256];
    DWORD n = 0;
    while (ReadFile(in, buf, sizeof buf, &n, nullptr) && n > 0) {
        pending.append(buf, n);
        size_t eol;
        while ((eol = pending.find('\n')) != std::string::npos) {
            const std::string cmd = pending.substr(0, eol);
            pending.erase(0, eol + 1);
            trace("agent: %s", cmd.c_str());
            if (cmd == "sleep") {
                displays::sleep();
            } else if (cmd == "wake") {
                displays::wake();
            } else if (cmd == "extend") {
                displays::set_topology(KVM_TOPO_EXTEND);
            } else if (cmd == "mirror") {
                displays::set_topology(KVM_TOPO_MIRROR);
            }
        }
    }
    trace("agent: pipe closed, exiting");
    return 0;
}

// ---- service side ----------------------------------------------------------

namespace {

HANDLE g_proc = nullptr;
HANDLE g_pipe = nullptr;  // write end; the agent's stdin
DWORD g_session = 0xFFFFFFFF;

bool running_in(DWORD session) {
    return g_proc && g_session == session && WaitForSingleObject(g_proc, 0) == WAIT_TIMEOUT;
}

bool spawn(DWORD session) {
    stop();
    HANDLE self = nullptr, token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_QUERY, &self)) {
        trace("agent: OpenProcessToken failed: %lu", GetLastError());
        return false;
    }
    const BOOL dup = DuplicateTokenEx(self, MAXIMUM_ALLOWED, nullptr, SecurityIdentification, TokenPrimary, &token);
    CloseHandle(self);
    if (!dup) {
        trace("agent: DuplicateTokenEx failed: %lu", GetLastError());
        return false;
    }
    // Our own SYSTEM token, moved into the console session (needs SeTcbPrivilege, which
    // services running as LocalSystem have).
    if (!SetTokenInformation(token, TokenSessionId, &session, sizeof session)) {
        trace("agent: SetTokenInformation failed: %lu", GetLastError());
        CloseHandle(token);
        return false;
    }

    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE read_end = nullptr, write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
        CloseHandle(token);
        return false;
    }
    SetHandleInformation(write_end, HANDLE_FLAG_INHERIT, 0);  // only the read end goes across

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --agent";

    STARTUPINFOW si = {};
    si.cb = sizeof si;
    si.lpDesktop = const_cast<wchar_t *>(L"winsta0\\default");
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = read_end;
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessAsUserW(token, exe, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                         nullptr, nullptr, &si, &pi);
    const DWORD err = GetLastError();
    CloseHandle(read_end);
    CloseHandle(token);
    if (!ok) {
        trace("agent: CreateProcessAsUser in session %lu failed: %lu", session, err);
        CloseHandle(write_end);
        return false;
    }
    CloseHandle(pi.hThread);
    g_proc = pi.hProcess;
    g_pipe = write_end;
    g_session = session;
    trace("agent: started pid %lu in session %lu", pi.dwProcessId, session);
    return true;
}

bool send(const char *cmd) {
    const DWORD session = WTSGetActiveConsoleSessionId();
    if (session == 0xFFFFFFFF) {
        trace("agent: no console session, dropping %s", cmd);
        return false;
    }
    for (int attempt = 0; attempt < 2; attempt++) {
        if (!running_in(session) && !spawn(session)) return false;
        const std::string line = std::string(cmd) + "\n";
        DWORD written = 0;
        if (WriteFile(g_pipe, line.data(), (DWORD)line.size(), &written, nullptr) && written == line.size()) {
            return true;
        }
        trace("agent: write failed (%lu), restarting it", GetLastError());
        stop();
    }
    return false;
}

}  // namespace

void sleep_displays() { send("sleep"); }
void wake_displays() { send("wake"); }
bool set_topology(uint8_t topo) { return send(topo == KVM_TOPO_MIRROR ? "mirror" : "extend"); }

void stop() {
    if (g_pipe) CloseHandle(g_pipe);  // the agent sees EOF and exits
    g_pipe = nullptr;
    if (g_proc) {
        if (WaitForSingleObject(g_proc, 2000) == WAIT_TIMEOUT) TerminateProcess(g_proc, 1);
        CloseHandle(g_proc);
    }
    g_proc = nullptr;
    g_session = 0xFFFFFFFF;
}

}  // namespace agent
