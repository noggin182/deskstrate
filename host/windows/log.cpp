#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <share.h>

namespace {

FILE *g_file = nullptr;
std::wstring g_path;
CRITICAL_SECTION g_lock;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK init_lock(PINIT_ONCE, PVOID, PVOID *) {
    InitializeCriticalSection(&g_lock);
    return TRUE;
}

}  // namespace

namespace hostlog {

void open(const std::wstring &dir, const wchar_t *name) {
    InitOnceExecuteOnce(&g_once, init_lock, nullptr, nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    g_path = dir + L"\\" + name;
    WIN32_FILE_ATTRIBUTE_DATA info;
    const bool big = GetFileAttributesExW(g_path.c_str(), GetFileExInfoStandard, &info) &&
                     (info.nFileSizeHigh || info.nFileSizeLow > 1024 * 1024);
    g_file = _wfsopen(g_path.c_str(), big ? L"w" : L"a", _SH_DENYWR);
}

const std::wstring &path() { return g_path; }

void close() {
    if (g_file) fclose(g_file);
    g_file = nullptr;
}

}  // namespace hostlog

void trace(const char *fmt, ...) {
    InitOnceExecuteOnce(&g_once, init_lock, nullptr, nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[512];
    const int n = snprintf(line, sizeof line, "%04u-%02u-%02u %02u:%02u:%02u.%03u ", t.wYear, t.wMonth,
                           t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - n, fmt, ap);
    va_end(ap);
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
    EnterCriticalSection(&g_lock);
    if (g_file) {
        fprintf(g_file, "%s\n", line);
        fflush(g_file);
    }
    LeaveCriticalSection(&g_lock);
}
