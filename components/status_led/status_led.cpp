#include "status_led.h"

#include <atomic>
#include <new>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

namespace status_led {

namespace {

const char *TAG = "status_led";

constexpr uint32_t kTickMs = 50;
constexpr uint32_t kFlashMs = 100;     // on, then off: 5 Hz
constexpr uint32_t kBlinkMs = 500;     // single LED: on, then off: 1 Hz
constexpr uint32_t kHeartbeatMs = 2000;  // single LED, all good: one tick lit per period

// What the board is doing, in the order the header lists it (first that applies wins).
enum class State { kRestarting, kUpdating, kDoorbell, kFault, kBooting, kCoreSilent, kOk };

struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};
constexpr Rgb kOff = {0, 0, 0};
constexpr Rgb kBlueDim = {0, 0, 16};
constexpr Rgb kGreen = {0, 80, 0};
constexpr Rgb kPurple = {100, 0, 140};
constexpr Rgb kRed = {120, 0, 0};
constexpr Rgb kOrange = {255, 80, 0};

// Brightness in 1/256ths: full, or 1/kDimDivisor while dimmed, stepped each tick so a
// change fades over kDimFadeMs.
constexpr uint32_t kFullLevel = 256;
constexpr uint32_t kDimLevel = kFullLevel / kDimDivisor;
constexpr uint32_t kDimStep = (kFullLevel - kDimLevel + kDimFadeMs / kTickMs - 1) / (kDimFadeMs / kTickMs);

Rgb scaled(Rgb c, uint32_t level) {
    auto ch = [level](uint8_t v) { return static_cast<uint8_t>((v * level + kFullLevel / 2) / kFullLevel); };
    return {ch(c.r), ch(c.g), ch(c.b)};
}

std::atomic<bool> ready{false};
std::atomic<uint32_t> faults{0};
std::atomic<uint32_t> activity{0};
std::atomic<bool> core_silent{false};
std::atomic<bool> dimmed{false};
std::atomic<bool> started{false};
std::atomic<bool> going_dark{false};
std::atomic<bool> dark{false};

struct Led {
    Kind kind;
    gpio_num_t gpio;
    led_strip_handle_t strip;  // kWs2812 only
};

bool has_ip() {
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip = {};
    return netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0;
}

Rgb colour(State s, TickType_t now) {
    switch (s) {
        case State::kRestarting: return kGreen;
        case State::kUpdating: return (now / pdMS_TO_TICKS(kFlashMs)) % 2 ? kOff : kPurple;
        case State::kDoorbell: return kPurple;
        case State::kFault: return kRed;
        case State::kBooting: return kGreen;
        case State::kCoreSilent: return kOrange;
        case State::kOk: break;
    }
    return kBlueDim;
}

bool lit(State s, TickType_t now) {
    switch (s) {
        case State::kUpdating: return (now / pdMS_TO_TICKS(kFlashMs)) % 2 == 0;
        case State::kRestarting:
        case State::kDoorbell:
        case State::kBooting: return (now / pdMS_TO_TICKS(kBlinkMs)) % 2 == 0;
        case State::kFault:
        case State::kCoreSilent: return true;
        case State::kOk: break;
    }
    return now % pdMS_TO_TICKS(kHeartbeatMs) < pdMS_TO_TICKS(kTickMs * 2);
}

void show_dark(const Led &led) {
    if (led.kind == Kind::kWs2812) {
        led_strip_clear(led.strip);
    } else {
        gpio_set_level(led.gpio, off_level(led.kind));
    }
}

void task(void *arg) {
    const Led led = *static_cast<Led *>(arg);
    delete static_cast<Led *>(arg);
    TickType_t ready_at = 0;
    bool joined = false;
    bool shown = false;
    Rgb last = kOff;
    uint32_t level = kFullLevel;
    for (;;) {
        if (going_dark) {
            // Dark, then stop for good: nothing changes it again before the board sleeps.
            show_dark(led);
            dark = true;
            vTaskSuspend(nullptr);
        }
        const TickType_t now = xTaskGetTickCount();
        // Netif is only looked at once ready without faults, i.e. after Wi-Fi started.
        const bool up = ready && !faults && has_ip();
        joined = joined || up;
        if (ready && !ready_at) ready_at = now;

        State s = State::kOk;
        const uint32_t act = activity;
        if (act & kRestarting) {
            s = State::kRestarting;
        } else if (act & kUpdating) {
            s = State::kUpdating;
        } else if (act & kDoorbell) {
            s = State::kDoorbell;
        } else if (faults) {
            s = State::kFault;
        } else if (!ready) {
            s = State::kBooting;
        } else if (!up) {
            s = !joined && now - ready_at < pdMS_TO_TICKS(kJoinTimeoutMs) ? State::kBooting : State::kFault;
        } else if (core_silent) {
            s = State::kCoreSilent;
        }

        if (led.kind == Kind::kWs2812) {
            const uint32_t target = dimmed ? kDimLevel : kFullLevel;
            if (level > target) {
                level = level - target > kDimStep ? level - kDimStep : target;
            } else if (level < target) {
                level = target - level > kDimStep ? level + kDimStep : target;
            }
            const Rgb c = scaled(colour(s, now), level);
            if (!shown || !(c == last)) {
                if (led_strip_set_pixel(led.strip, 0, c.r, c.g, c.b) == ESP_OK &&
                    led_strip_refresh(led.strip) == ESP_OK) {
                    last = c;
                    shown = true;
                }
            }
        } else {
            const bool on = lit(s, now);
            gpio_set_level(led.gpio, on ? !off_level(led.kind) : off_level(led.kind));
        }
        vTaskDelay(pdMS_TO_TICKS(kTickMs));
    }
}

}  // namespace

esp_err_t start(gpio_num_t gpio, Kind kind) {
    if (gpio == GPIO_NUM_NC) return ESP_OK;
    auto *led = new (std::nothrow) Led{kind, gpio, nullptr};
    if (!led) return ESP_ERR_NO_MEM;
    if (kind == Kind::kWs2812) {
        led_strip_config_t strip_config = {};
        strip_config.strip_gpio_num = gpio;
        strip_config.max_leds = 1;
        strip_config.led_model = LED_MODEL_WS2812;
        led_strip_rmt_config_t rmt_config = {};
        rmt_config.resolution_hz = 10 * 1000 * 1000;
        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led->strip);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "no LED on GPIO %d: %s", (int)gpio, esp_err_to_name(err));
            delete led;
            return err;
        }
    } else {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << gpio;
        io.mode = GPIO_MODE_OUTPUT;
        esp_err_t err = gpio_config(&io);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "no LED on GPIO %d: %s", (int)gpio, esp_err_to_name(err));
            delete led;
            return err;
        }
        gpio_set_level(gpio, off_level(kind));
    }
    const led_strip_handle_t strip = led->strip;
    if (xTaskCreate(task, "status_led", 3072, led, 2, nullptr) != pdPASS) {
        if (strip) led_strip_del(strip);
        delete led;
        return ESP_ERR_NO_MEM;
    }
    started = true;
    return ESP_OK;
}

void set_ready() { ready = true; }

namespace {
void set_bits(std::atomic<uint32_t> &bits, uint32_t b, bool on) {
    if (on) {
        bits |= b;
    } else {
        bits &= ~b;
    }
}
}  // namespace

void set_fault(uint32_t f, bool on) { set_bits(faults, f, on); }

void set_activity(uint32_t a, bool on) { set_bits(activity, a, on); }

void set_core_silent(bool silent) { core_silent = silent; }

void set_dim(bool dim) { dimmed = dim; }

bool turn_off(uint32_t timeout_ms) {
    if (!started) return true;
    going_dark = true;
    for (uint32_t waited = 0; !dark && waited < timeout_ms; waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return dark;
}

}  // namespace status_led
