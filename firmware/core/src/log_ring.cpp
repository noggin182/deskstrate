#include "log_ring.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace log_ring {

namespace {

constexpr size_t kSize = 8 * 1024;

char ring[kSize];
size_t head = 0;     // next write position
bool wrapped = false;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
vprintf_like_t console = nullptr;

// Colour escapes (ESC [ ... m) are for terminals; drop them from the copy.
size_t strip_colour(char *s, size_t n) {
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\x1b') {
            while (i < n && s[i] != 'm') i++;
            continue;
        }
        s[out++] = s[i];
    }
    return out;
}

int hook(const char *fmt, va_list args) {
    va_list copy;
    va_copy(copy, args);
    char line[192];
    int n = vsnprintf(line, sizeof line, fmt, copy);
    va_end(copy);
    if (n > 0) {
        size_t len = strip_colour(line, n < (int)sizeof line ? (size_t)n : sizeof line - 1);
        taskENTER_CRITICAL(&mux);
        for (size_t i = 0; i < len; i++) {
            ring[head++] = line[i];
            if (head == kSize) {
                head = 0;
                wrapped = true;
            }
        }
        taskEXIT_CRITICAL(&mux);
    }
    return console ? console(fmt, args) : n;
}

}  // namespace

void start() {
    if (!console) console = esp_log_set_vprintf(hook);
}

size_t read(char *buf, size_t len) {
    if (len == 0) return 0;
    size_t n = 0;
    taskENTER_CRITICAL(&mux);
    const size_t used = wrapped ? kSize : head;
    const size_t start = wrapped ? head : 0;
    // Skip the oldest bytes if buf is smaller, so the newest lines are kept.
    const size_t skip = used > len - 1 ? used - (len - 1) : 0;
    for (size_t i = skip; i < used; i++) buf[n++] = ring[(start + i) % kSize];
    taskEXIT_CRITICAL(&mux);
    buf[n] = '\0';
    // Start at a line boundary when the oldest line was cut.
    if (skip || wrapped) {
        const char *nl = static_cast<const char *>(memchr(buf, '\n', n));
        if (nl) {
            const size_t cut = nl + 1 - buf;
            memmove(buf, nl + 1, n - cut + 1);
            n -= cut;
        }
    }
    return n;
}

}  // namespace log_ring
