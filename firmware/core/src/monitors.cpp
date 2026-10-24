#include "monitors.h"

#include "board_config.h"
#include "ddc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace monitors {

namespace {

const char *TAG = "monitors";

constexpr uint32_t kPollMs = 2000;
constexpr uint32_t kOfflineRetryMs = 15000;  // don't hammer a bus with nothing on it

struct Monitor {
    ddc::Bus bus;
    bool bus_ok = false;
    State state;
    TickType_t quiet_until = 0;  // skip polls while the monitor settles after a write
    TickType_t next_poll = 0;
};

Monitor mons[kCount];
SemaphoreHandle_t op_lock;  // one poll or write at a time, so a poll never undoes a write
portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;

bool reached(TickType_t deadline) { return (int32_t)(xTaskGetTickCount() - deadline) >= 0; }

void publish(int i, const State &s) {
    taskENTER_CRITICAL(&state_mux);
    mons[i].state = s;
    taskEXIT_CRITICAL(&state_mux);
}

void log_changes(int i, const State &was, const State &now) {
    if (was.online != now.online) {
        ESP_LOGI(TAG, "DDC%d: %s", i + 1, now.online ? "responding" : "not responding");
    }
    if (now.serial && was.serial != now.serial) {
        const char *s = side(now.serial);
        ESP_LOGI(TAG, "DDC%d: EDID serial %lu (%s)", i + 1, (unsigned long)now.serial,
                 s ? s : "unknown");
    }
    if (now.online && (was.input != now.input || was.power != now.power)) {
        ESP_LOGI(TAG, "DDC%d: input 0x%02X = %s, power %u", i + 1, now.input,
                 ddc::input_name(now.input), now.power);
    }
}

void poll(int i) {
    Monitor &m = mons[i];
    if (!m.bus_ok || !reached(m.quiet_until) || !reached(m.next_poll)) return;

    const State was = get(i);
    State s = was;
    // A line idling low means no pull-ups (nothing wired): probing would only time out.
    const bool lines_up = m.bus.sda_level() && m.bus.scl_level();
    if (!lines_up || m.bus.probe_ddcci() != ESP_OK) {
        s = State{.serial = was.serial};
        m.next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(kOfflineRetryMs);
    } else {
        s.online = true;
        if (!s.serial && m.bus.probe_edid() == ESP_OK) {
            uint32_t serial = 0;
            if (m.bus.read_edid_serial(serial) == ESP_OK) s.serial = serial;
        }
        uint8_t input = 0;
        s.input = m.bus.get_input(input) == ESP_OK ? input : 0;
        ddc::VcpValue power{};
        s.power = m.bus.get_vcp(ddc::kVcpPower, power) == ESP_OK ? (power.current & 0xFF) : 0;
        m.next_poll = xTaskGetTickCount() + pdMS_TO_TICKS(kPollMs);
    }
    log_changes(i, was, s);
    publish(i, s);
}

void poll_task(void *) {
    for (;;) {
        for (int i = 0; i < kCount; i++) {
            xSemaphoreTake(op_lock, portMAX_DELAY);
            poll(i);
            xSemaphoreGive(op_lock);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// Shared by set_input/set_power: write one VCP code, then update the cache.
esp_err_t write(int i, uint8_t code, uint8_t value, void (*apply)(State &, uint8_t)) {
    Monitor &m = mons[i];
    if (!m.bus_ok || !get(i).online) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(op_lock, portMAX_DELAY);
    const esp_err_t err = m.bus.set_vcp(code, value);
    if (err == ESP_OK) {
        State s = get(i);
        s.online = true;
        apply(s, value);
        publish(i, s);
        m.quiet_until = xTaskGetTickCount() + pdMS_TO_TICKS(ddc::kInputSettleMs);
        ESP_LOGI(TAG, "DDC%d: set VCP 0x%02X = 0x%02X", i + 1, code, value);
    }
    xSemaphoreGive(op_lock);
    return err;
}

}  // namespace

void start() {
    op_lock = xSemaphoreCreateMutex();

    const ddc::Pins pins[kCount] = {{board::kDdc1Sda, board::kDdc1Scl},
                                    {board::kDdc2Sda, board::kDdc2Scl}};
    const i2c_port_num_t ports[kCount] = {I2C_NUM_0, I2C_NUM_1};

#if CONFIG_DESKSTRATE_DDC_TOGGLE_TEST
    ESP_LOGW(TAG, "toggle test: meter on HDMI pin 16 (SDA) / 15 (SCL) vs pin 17");
    ddc::line_toggle_test(pins[0]);
#endif

    for (int i = 0; i < kCount; i++) {
        const esp_err_t err = mons[i].bus.init(ports[i], pins[i]);
        mons[i].bus_ok = err == ESP_OK;
        if (!mons[i].bus_ok) ESP_LOGE(TAG, "DDC%d init failed: %s", i + 1, esp_err_to_name(err));
    }
    xTaskCreate(poll_task, "monitors", 4096, nullptr, 3, nullptr);
}

State get(int mon) {
    if (mon < 0 || mon >= kCount) return {};
    taskENTER_CRITICAL(&state_mux);
    const State s = mons[mon].state;
    taskEXIT_CRITICAL(&state_mux);
    return s;
}

const char *side(uint32_t serial) {
    switch (serial) {
        case 810562882: return "left";
        case 810959170: return "right";
        default: return nullptr;
    }
}

esp_err_t set_input(int mon, uint8_t input) {
    if (mon < 0 || mon >= kCount) return ESP_ERR_INVALID_ARG;
    if (input != ddc::kInputDp && input != ddc::kInputHdmi && input != ddc::kInputUsbc) {
        return ESP_ERR_INVALID_ARG;
    }
    return write(mon, ddc::kVcpInput, input, [](State &s, uint8_t v) { s.input = v; });
}

esp_err_t set_power(int mon, uint8_t power) {
    if (mon < 0 || mon >= kCount) return ESP_ERR_INVALID_ARG;
    if (power != ddc::kPowerOn && power != ddc::kPowerStandby) return ESP_ERR_INVALID_ARG;
    return write(mon, ddc::kVcpPower, power, [](State &s, uint8_t v) { s.power = v; });
}

}  // namespace monitors
