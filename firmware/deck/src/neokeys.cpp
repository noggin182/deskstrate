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
constexpr uint32_t kFadeMs = 350;

struct Rgb {
    float r, g, b;
};

led_strip_handle_t strip = nullptr;
uint32_t flash_at[controls::kKeyCount] = {};
bool flashing[controls::kKeyCount] = {};

// Crossfade per key, from the colour it last showed when its subject changed.
uint16_t shown_subject[controls::kKeyCount] = {};
Rgb shown[controls::kKeyCount] = {};
Rgb fade_from[controls::kKeyCount] = {};
uint32_t fade_at[controls::kKeyCount] = {};
bool fading[controls::kKeyCount] = {};

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

// Fully saturated colour at hue degrees (HSV value 1).
Rgb hue_rgb(float hue) {
    hue = fmodf(hue + 360.0f, 360.0f) / 60.0f;
    const int sector = static_cast<int>(hue);
    const float f = hue - sector;
    switch (sector) {
        case 0: return {1.0f, f, 0.0f};
        case 1: return {1.0f - f, 1.0f, 0.0f};
        case 2: return {0.0f, 1.0f, f};
        case 3: return {0.0f, 1.0f - f, 1.0f};
        case 4: return {f, 0.0f, 1.0f};
        default: return {1.0f, 0.0f, 1.0f - f};
    }
}

// Light the LED gives out, roughly: to_byte() squares each channel.
float power(Rgb c) { return c.r * c.r + c.g * c.g + c.b * c.b; }

// Each host cycles through a few hues close to its screen colour (host_color in
// main.cpp), e.g. the PC's orange drifts to yellow, back through orange to a hint of
// red. Every step is scaled to the same light output, so only the hue moves.
struct Palette {
    float hues[4];
    float saturation;  // 1 = pure hue; lower mixes in white (coral, lavender)
    uint32_t period_ms;
};

constexpr Palette kPalettes[4] = {
    {{32.0f, 50.0f, 36.0f, 12.0f}, 1.0f, 4000},     // PC: orange, yellow, orange, red
    {{222.0f, 202.0f, 188.0f, 240.0f}, 1.0f, 4400}, // Mac: blue, azure, cyan, deep blue
    {{272.0f, 292.0f, 256.0f, 306.0f}, 0.8f, 4200}, // Guest 3: purple, violet, indigo, magenta
    {{10.0f, 24.0f, 0.0f, 348.0f}, 0.65f, 4200},    // Guest 4: coral, salmon, red, pink
};

Rgb palette_rgb(const Palette &palette, float hue) {
    return mix(hex(0xFFFFFF), hue_rgb(hue), palette.saturation);
}

Rgb host_pattern(uint8_t host, uint32_t now_ms, uint32_t phase_ms) {
    if (host > KVM_USB_GUEST2) return scale(hex(0xFFFFFF), 0.1f);
    const Palette &palette = kPalettes[host];

    // Ease between the hues (taking the short way round the colour wheel).
    const float position = static_cast<float>((now_ms + phase_ms) % palette.period_ms) / palette.period_ms * 4.0f;
    const int i = static_cast<int>(position);
    float t = position - i;
    t = t * t * (3.0f - 2.0f * t);
    const float from = palette.hues[i];
    float delta = palette.hues[(i + 1) % 4] - from;
    if (delta > 180.0f) delta -= 360.0f;
    if (delta < -180.0f) delta += 360.0f;
    const Rgb c = palette_rgb(palette, from + delta * t);

    // Match the dimmest of the palette's hues, so no step is brighter than another.
    float target = power(palette_rgb(palette, palette.hues[0]));
    for (int h = 1; h < 4; ++h) target = fminf(target, power(palette_rgb(palette, palette.hues[h])));
    float k = sqrtf(target / power(c));
    const float peak = fmaxf(c.r, fmaxf(c.g, c.b));
    if (k * peak > 1.0f) k = 1.0f / peak;
    return scale(c, k);
}

// What a key is showing, ignoring the animation: when this changes the key fades
// to its new colour instead of jumping.
uint16_t key_subject(Key key, const Scene &scene) {
    if (!scene.core_seen) return 0x100;
    if (scene.menu) return 0x200;
    const bool chord_hint =
        (key == controls::kBottomLeft && (scene.held & (1u << controls::kTopRight)))
        || (key == controls::kBottomRight && (scene.held & (1u << controls::kTopLeft)));
    if (chord_hint) return 0x300;
    switch (key) {
        case controls::kTopLeft: return 0x400 | scene.view.mon[KVM_MON_LEFT];
        case controls::kTopRight: return 0x400 | scene.view.mon[KVM_MON_RIGHT];
        case controls::kBottomLeft: return 0x500 | scene.view.usb[KVM_HUB_HID];
        default: return 0x500 | scene.view.usb[KVM_HUB_MEDIA];
    }
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

    // A held top key points at the bottom key it chords with: a soft white pulse.
    const bool chord_hint =
        (key == controls::kBottomLeft && (scene.held & (1u << controls::kTopRight)))
        || (key == controls::kBottomRight && (scene.held & (1u << controls::kTopLeft)));
    if (chord_hint) return scale(hex(0xFFFFFF), 0.3f + 0.5f * wave(now_ms, 900));

    switch (key) {
        case controls::kTopLeft:
        case controls::kTopRight: {
            const uint8_t mon = key == controls::kTopLeft ? KVM_MON_LEFT : KVM_MON_RIGHT;
            if (scene.view.mon[mon] == KVM_IN_OFF) return scale(hex(0xFFFFFF), 0.05f);
            const uint8_t host = controls::monitor_host(scene.view, mon);
            // Monitor not connected (or its input unreadable): a dim red glow.
            if (host == KVM_USB_UNKNOWN) return scale(hex(0xFF0000), 0.3f);
            return scale(host_pattern(host, now_ms, mon * 400), 0.6f);
        }
        case controls::kBottomLeft:
        case controls::kBottomRight: {
            // Bottom left = the HID hub's host, bottom right = the media hub's.
            const uint8_t hub = key == controls::kBottomLeft ? KVM_HUB_HID : KVM_HUB_MEDIA;
            const uint8_t host = scene.view.usb[hub];
            // Hub not connected: a dim red glow, like a missing monitor.
            if (host > KVM_USB_GUEST2) return scale(hex(0xFF0000), 0.3f);
            return scale(host_pattern(host, now_ms, 200 + hub * 400), 0.6f);
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
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = pin;
    strip_config.max_leds = controls::kKeyCount;
    strip_config.led_model = LED_MODEL_WS2812;

    // Four LEDs are 96 RMT symbols, more than one 48-symbol block, so without help
    // the driver refills the block from an interrupt mid-frame. Wi-Fi interrupts can
    // delay that refill, corrupting bits (flashes of the wrong colour). DMA streams
    // the whole frame; failing that, three blocks hold it without any refill.
    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000;
    rmt_config.mem_block_symbols = 1024;
    rmt_config.flags.with_dma = true;
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    if (err != ESP_OK) {
        rmt_config.mem_block_symbols = 144;
        rmt_config.flags.with_dma = false;
        err = led_strip_new_rmt_device(&strip_config, &rmt_config, &strip);
    }
    return err;
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
        const uint16_t subject = key_subject(key, scene);
        if (subject != shown_subject[k]) {
            shown_subject[k] = subject;
            fade_from[k] = shown[k];
            fade_at[k] = now_ms;
            fading[k] = true;
        }
        if (fading[k]) {
            const uint32_t age = now_ms - fade_at[k];
            if (age >= kFadeMs) {
                fading[k] = false;
            } else {
                float t = static_cast<float>(age) / kFadeMs;
                t = t * t * (3.0f - 2.0f * t);
                c = mix(fade_from[k], c, t);
            }
        }
        shown[k] = c;  // before the press flash, which is short and fades on its own
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
