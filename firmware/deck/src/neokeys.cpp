#include "neokeys.h"

#include <cmath>

#include "led_strip.h"

namespace neokeys {

namespace {

using controls::Key;

// NeoPixel index for each controls::Key. The chain runs through the left wing (D5
// top, D6 bottom) and then the right wing (D10 top, D11 bottom).
// TODO: unverified on the hardware; swap entries here if a key lights the wrong colour.
constexpr uint8_t kPixelForKey[controls::kKeyCount] = {0, 2, 1, 3};

constexpr uint32_t kFlashMs = 180;

struct Rgb {
    float r, g, b;
};

led_strip_handle_t strip = nullptr;
uint32_t flash_at[controls::kKeyCount] = {};
bool flashing[controls::kKeyCount] = {};

Rgb hex(uint32_t c) {
    return {((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f};
}

Rgb scale(Rgb c, float k) { return {c.r * k, c.g * k, c.b * k}; }

Rgb mix(Rgb a, Rgb b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// 0..1 over each period, starting at the bottom of a sine.
float wave(uint32_t now_ms, uint32_t period_ms, uint32_t phase_ms = 0) {
    const float x = static_cast<float>((now_ms + phase_ms) % period_ms) / period_ms;
    return 0.5f - 0.5f * cosf(x * 2.0f * static_cast<float>(M_PI));
}

// Same colours as the screen (host_color in main.cpp).
Rgb host_rgb(uint8_t host) {
    switch (host) {
        case KVM_USB_PC: return hex(0xFFA020);
        case KVM_USB_MAC: return hex(0x2060FF);
        case KVM_USB_GUEST1: return hex(0xA060FF);
        case KVM_USB_GUEST2: return hex(0xFF5040);
    }
    return hex(0x404040);
}

// One host's animation at full strength.
Rgb host_pattern(uint8_t host, uint32_t now_ms, uint32_t phase_ms) {
    const Rgb base = host_rgb(host);
    switch (host) {
        case KVM_USB_PC:  // slow breath
            return scale(base, 0.25f + 0.75f * wave(now_ms, 3200, phase_ms));
        case KVM_USB_MAC:  // ripple between blue and cyan
            return mix(base, hex(0x20D0FF), wave(now_ms, 2400, phase_ms));
        case KVM_USB_GUEST1:
        case KVM_USB_GUEST2: {  // heartbeat: two quick beats, then rest
            const uint32_t t = (now_ms + phase_ms) % 1600;
            float k = 0.2f;
            if (t < 120) k = 1.0f;
            else if (t >= 240 && t < 360) k = 0.8f;
            return scale(base, k);
        }
    }
    return scale(base, 0.15f);
}

// The host a bottom key sends the keyboard to.
uint8_t bottom_key_host(const controls::View &view, bool left) {
    if (controls::is_split(view)) {
        return controls::monitor_host(view, left ? KVM_MON_LEFT : KVM_MON_RIGHT);
    }
    return left ? KVM_USB_PC : KVM_USB_MAC;
}

Rgb key_colour(Key key, const Scene &scene, uint32_t now_ms) {
    if (!scene.core_seen) {
        return scale(hex(0xFF8000), 0.05f + 0.25f * wave(now_ms, 2000, key * 250));
    }
    if (scene.menu) {
        switch (key) {
            case controls::kTopLeft: return scale(hex(0xFF2010), 0.6f);
            case controls::kTopRight: return scale(hex(0x20FF40), 0.3f + 0.5f * wave(now_ms, 1500));
            default: return scale(hex(0xFFFFFF), 0.25f);
        }
    }

    // A held top key points at the bottom key it chords with.
    const bool blink = (now_ms / 150) % 2 == 0;
    if (key == controls::kBottomLeft && (scene.held & (1u << controls::kTopRight))) {
        return blink ? hex(0xFFFFFF) : hex(0x000000);
    }
    if (key == controls::kBottomRight && (scene.held & (1u << controls::kTopLeft))) {
        return blink ? hex(0xFFFFFF) : hex(0x000000);
    }

    const uint8_t hid = scene.view.usb[KVM_HUB_HID];
    switch (key) {
        case controls::kTopLeft:
        case controls::kTopRight: {
            const uint8_t mon = key == controls::kTopLeft ? KVM_MON_LEFT : KVM_MON_RIGHT;
            if (scene.view.mon[mon] == KVM_IN_OFF) return scale(hex(0xFFFFFF), 0.02f);
            const uint8_t host = controls::monitor_host(scene.view, mon);
            return scale(host_pattern(host, now_ms, mon * 400), 0.6f);
        }
        case controls::kBottomLeft:
        case controls::kBottomRight: {
            const uint8_t host = bottom_key_host(scene.view, key == controls::kBottomLeft);
            if (host == hid) return host_pattern(host, now_ms, 0);
            return scale(host_rgb(host), 0.08f);
        }
        default:
            return hex(0x000000);
    }
}

uint8_t to_byte(float v, uint8_t brightness) {
    if (v <= 0.0f) return 0;
    if (v > 1.0f) v = 1.0f;
    // Square for a rough perceptual curve, so the breath fades smoothly.
    return static_cast<uint8_t>(v * v * brightness + 0.5f);
}

}  // namespace

esp_err_t start(gpio_num_t pin) {
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000;

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = pin;
    strip_config.max_leds = controls::kKeyCount;
    strip_config.led_model = LED_MODEL_WS2812;
    return led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
}

void flash(Key key, uint32_t now_ms) {
    flash_at[key] = now_ms;
    flashing[key] = true;
}

void render(const Scene &scene, uint32_t now_ms) {
    if (strip == nullptr) return;
    for (uint8_t k = 0; k < controls::kKeyCount; ++k) {
        const Key key = static_cast<Key>(k);
        Rgb c = key_colour(key, scene, now_ms);
        if (flashing[k]) {
            const uint32_t age = now_ms - flash_at[k];
            if (age >= kFlashMs) {
                flashing[k] = false;
            } else {
                c = mix(c, hex(0xFFFFFF), 1.0f - static_cast<float>(age) / kFlashMs);
            }
        }
        led_strip_set_pixel(strip, kPixelForKey[k], to_byte(c.r, scene.brightness),
                            to_byte(c.g, scene.brightness), to_byte(c.b, scene.brightness));
    }
    led_strip_refresh(strip);
}

}  // namespace neokeys
