// Keeps the last few KB of log output in RAM so the web UI can show it (/api/log)
// without a serial cable. Lines still go to the console as before.
#pragma once

#include <cstddef>

namespace log_ring {

// Hooks esp_log; call first thing in app_main so start-up lines are kept too.
void start();

// Copies the buffered log, oldest first, into buf (NUL-terminated). Returns the length.
size_t read(char *buf, size_t len);

}  // namespace log_ring
