// Timestamped log lines to a file (and OutputDebugString). Safe from any thread.
#pragma once

#include <string>

namespace hostlog {

// Opens <dir>\<name>, started afresh once it passes 1 MB. dir is created if missing.
void open(const std::wstring &dir, const wchar_t *name);
const std::wstring &path();
void close();

}  // namespace hostlog

void trace(const char *fmt, ...);
