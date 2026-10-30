#include "hubs.h"

#include <cstring>

#include "board_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hub_swd.h"
#include "sdkconfig.h"

namespace hubs {

namespace {

const char *TAG = "hubs";
const char *kNames[kCount] = {"HID", "MEDIA"};

constexpr uint32_t kPollMs = 1000;
constexpr uint32_t kOfflineRetryMs = 15000;  // don't keep line-resetting a hub that isn't there

struct Hub {
    hub_swd::Hub swd;
    bool init_ok = false;
    bool connected = false;
    State state;
    TickType_t next_poll = 0;
};

Hub hubs_[kCount];
SemaphoreHandle_t op_lock;  // one poll or request at a time on the SWD lines
portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;

bool reached(TickType_t deadline) { return (int32_t)(xTaskGetTickCount() - deadline) >= 0; }

void publish(int i, const State &s) {
    taskENTER_CRITICAL(&state_mux);
    hubs_[i].state = s;
    taskEXIT_CRITICAL(&state_mux);
}

// Plain-words reason for an SWD failure, from the stage and ACK that failed.
const char *failure(const hub_swd::Hub &swd) {
    const uint8_t ack = swd.last_ack();
    if (ack == 7) {
        return strcmp(swd.last_stage(), "idcode") == 0
            ? "no SWD reply (check wiring, hub power and common ground)"
            : "SWD reply lost";
    }
    if (ack == 2) return "SWD WAIT timeout";
    if (ack == 4) return "SWD FAULT";
    if (ack == 8) return "SWD parity error (noisy or slow lines)";
    return "SWD out of step (SWCLK/SWDIO swapped?)";
}

void poll(int i) {
    Hub &h = hubs_[i];
    if (!h.init_ok || !reached(h.next_poll)) return;

    const State was = get(i);
    State s;
    s.error = nullptr;
    if (!h.connected) {
        uint32_t idcode = 0;
        h.connected = h.swd.connect(&idcode) == ESP_OK;
        if (h.connected) ESP_LOGI(TAG, "%s hub: SWD connected, idcode %08x", kNames[i], (unsigned)idcode);
    }
    hub_swd::Status st = {};
    if (h.connected && h.swd.read_status(st) == ESP_OK) {
        s.online = true;
        s.port = st.current <= 3 ? st.current : kUnknown;
        s.raw = (uint32_t)st.held_lockout | (uint32_t)st.requested << 8 |
                (uint32_t)st.current << 16 | (uint32_t)st.applied << 24;
        h.next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(kPollMs);
    } else {
        s.error = failure(h.swd);
        h.connected = false;
        h.next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(kOfflineRetryMs);
    }

    if (was.online != s.online) {
        ESP_LOGI(TAG, "%s hub: %s", kNames[i], s.online ? "responding" : "not responding");
    }
    // Logged once per change, so a hub that never answers still says why.
    if (!s.online && s.error != was.error) {
        ESP_LOGW(TAG, "%s hub: %s (stage %s, ack %u)", kNames[i], s.error, h.swd.last_stage(),
                 h.swd.last_ack());
    }
    if (s.online && was.port != s.port) {
        if (s.port == kUnknown) {
            // The status word isn't where this build keeps it (see hub_swd.h kStatusAddr).
            ESP_LOGW(TAG, "%s hub: status word %08x has no valid port", kNames[i], (unsigned)s.raw);
        } else {
            ESP_LOGI(TAG, "%s hub: port %u", kNames[i], s.port);
        }
    }
    publish(i, s);
}

void poll_task(void *) {
    for (;;) {
        for (int i = 0; i < kCount; i++) {
            xSemaphoreTake(op_lock, portMAX_DELAY);
            poll(i);
            xSemaphoreGive(op_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace

void start() {
    op_lock = xSemaphoreCreateMutex();

    const hub_swd::Pins pins[kCount] = {{board::kHidSwclk, board::kHidSwdio},
                                        {board::kMediaSwclk, board::kMediaSwdio}};
    bool any = false;
    for (int i = 0; i < kCount; i++) {
        const esp_err_t err = hubs_[i].swd.init(pins[i]);
        hubs_[i].init_ok = err == ESP_OK;
        any |= hubs_[i].init_ok;
        if (err != ESP_OK) ESP_LOGE(TAG, "%s hub init failed: %s", kNames[i], esp_err_to_name(err));
    }
    if (!any) return;  // nothing to poll
    xTaskCreate(poll_task, "hubs", 4096, nullptr, 3, nullptr);
}

State get(int hub) {
    if (hub < 0 || hub >= kCount) return {};
    taskENTER_CRITICAL(&state_mux);
    const State s = hubs_[hub].state;
    taskEXIT_CRITICAL(&state_mux);
    return s;
}

esp_err_t set_port(int hub, uint8_t port) {
    if (hub < 0 || hub >= kCount || port > 3) return ESP_ERR_INVALID_ARG;
    const State now = get(hub);
    if (!now.online) return ESP_ERR_NOT_FOUND;
    if (now.port == port) return ESP_OK;

    xSemaphoreTake(op_lock, portMAX_DELAY);
    Hub &h = hubs_[hub];
    const esp_err_t err = h.swd.request(port);
    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
        // Lost the hub (unplugged or power-cycled): reconnect on the next poll.
        h.connected = false;
        h.next_poll = xTaskGetTickCount();
    }
    xSemaphoreGive(op_lock);
    if (err == ESP_OK) {
        // The hub switches on its next main-loop pass; show the target until the poll confirms.
        publish(hub, State{.online = true, .port = port, .raw = now.raw, .error = nullptr});
        ESP_LOGI(TAG, "%s hub: requested port %u", kNames[hub], port);
    }
    return err;
}

}  // namespace hubs
