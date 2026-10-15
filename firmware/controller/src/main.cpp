#include <cstdio>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_random.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "network.h"
#include "led_strip.h"
#include "lvgl.h"

// Adafruit Feather ESP32-S3 TFT Pinout
#define PIN_POWER_TFT_I2C  GPIO_NUM_21
#define PIN_TFT_BACKLIGHT  GPIO_NUM_45
#define PIN_NEOPIXEL       GPIO_NUM_33
#define PIN_NEOPIXEL_POWER GPIO_NUM_34

// TFT SPI Pins
#define PIN_TFT_MOSI      35
#define PIN_TFT_CLK       36
#define PIN_TFT_CS        GPIO_NUM_7
#define PIN_TFT_DC        GPIO_NUM_39
#define PIN_TFT_RST       GPIO_NUM_40

#define PIN_NEOKEY_SWITCH_A GPIO_NUM_5
#define PIN_NEOKEY_SWITCH_B GPIO_NUM_6
#define PIN_NEOKEY_NEOPIXEL GPIO_NUM_9

static lv_disp_drv_t disp_drv;

struct KeyDebounceState {
    bool stable_pressed = false;
    bool raw_pressed = false;
    uint8_t stable_samples = 0;
};

static bool key_press_detected(gpio_num_t pin, KeyDebounceState &state) {
    const bool pressed = gpio_get_level(pin) == 0;
    if (pressed != state.raw_pressed) {
        state.raw_pressed = pressed;
        state.stable_samples = 0;
        return false;
    }

    if (pressed == state.stable_pressed) {
        return false;
    }

    if (++state.stable_samples < 3) {
        return false;
    }

    state.stable_pressed = pressed;
    state.stable_samples = 0;
    return pressed;
}

static void hue_to_rgb(uint8_t hue, uint8_t &red, uint8_t &green, uint8_t &blue) {
    if (hue < 85) {
        red = static_cast<uint8_t>(255 - hue * 3);
        green = static_cast<uint8_t>(hue * 3);
        blue = 0;
    } else if (hue < 170) {
        hue -= 85;
        red = 0;
        green = static_cast<uint8_t>(255 - hue * 3);
        blue = static_cast<uint8_t>(hue * 3);
    } else {
        hue -= 170;
        red = static_cast<uint8_t>(hue * 3);
        green = 0;
        blue = static_cast<uint8_t>(255 - hue * 3);
    }
}

static lv_obj_t *create_status_label(
    lv_obj_t *parent,
    const char *text,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t width,
    lv_coord_t height,
    const lv_font_t *font,
    lv_color_t color,
    lv_text_align_t align = LV_TEXT_ALIGN_LEFT
) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_align(label, align, 0);
    return label;
}

static lv_obj_t *create_status_card(lv_obj_t *parent, lv_coord_t y, lv_coord_t height) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, 8, y);
    lv_obj_set_size(card, 119, height);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x101C2B), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x24364A), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 7, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    return card;
}

static lv_obj_t *create_diagram_shape(
    lv_obj_t *parent,
    lv_coord_t x,
    lv_coord_t y,
    lv_coord_t width,
    lv_coord_t height,
    lv_color_t color,
    lv_coord_t radius = 0
) {
    lv_obj_t *shape = lv_obj_create(parent);
    lv_obj_set_pos(shape, x, y);
    lv_obj_set_size(shape, width, height);
    lv_obj_clear_flag(shape, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(shape, color, 0);
    lv_obj_set_style_bg_opa(shape, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(shape, 0, 0);
    lv_obj_set_style_radius(shape, radius, 0);
    lv_obj_set_style_pad_all(shape, 0, 0);
    return shape;
}

enum class HostId : uint8_t {
    Pc,
    Mac,
    Guest1,
    Guest2
};

enum class MonitorInput : uint8_t {
    UsbC,
    DisplayPort,
    Hdmi
};

struct KvmState {
    MonitorInput monitor1_input;
    MonitorInput monitor2_input;
    HostId hub1_host;
    HostId hub2_host;
};

struct HostRoute {
    HostId host;
    uint8_t monitor_mask;
    uint8_t hub_mask;
};

static KvmState current_kvm_state = {
    MonitorInput::DisplayPort,
    MonitorInput::DisplayPort,
    HostId::Mac,
    HostId::Mac
};
static lv_obj_t *routing_diagram = nullptr;

static const char *host_label(HostId host) {
    switch (host) {
        case HostId::Pc: return "PC";
        case HostId::Mac: return "MAC";
        case HostId::Guest1: return "3";
        case HostId::Guest2: return "4";
    }
    return "";
}

static HostId input_host(MonitorInput input) {
    switch (input) {
        case MonitorInput::UsbC: return HostId::Mac;
        case MonitorInput::DisplayPort: return HostId::Pc;
        case MonitorInput::Hdmi: return HostId::Guest1;
    }
    return HostId::Guest1;
}

static uint16_t scenario_index(const KvmState &state) {
    return static_cast<uint16_t>(state.monitor1_input)
        + 3 * static_cast<uint16_t>(state.monitor2_input)
        + 9 * static_cast<uint16_t>(state.hub1_host)
        + 36 * static_cast<uint16_t>(state.hub2_host);
}

static void set_scenario_index(KvmState &state, uint16_t index) {
    state.monitor1_input = static_cast<MonitorInput>(index % 3);
    index /= 3;
    state.monitor2_input = static_cast<MonitorInput>(index % 3);
    index /= 3;
    state.hub1_host = static_cast<HostId>(index % 4);
    index /= 4;
    state.hub2_host = static_cast<HostId>(index % 4);
}

static void randomize_scenario(KvmState &state) {
    const uint16_t next_index = static_cast<uint16_t>(
        (scenario_index(state) + 1 + esp_random() % 143) % 144
    );
    set_scenario_index(state, next_index);
}

static lv_color_t host_color(HostId host) {
    switch (host) {
        case HostId::Pc: return lv_color_hex(0x55E6D2);
        case HostId::Mac: return lv_color_hex(0xB99AFF);
        case HostId::Guest1: return lv_color_hex(0xFFC857);
        case HostId::Guest2: return lv_color_hex(0xFF8B7B);
    }
    return lv_color_hex(0x91A3B8);
}

static HostRoute *find_or_add_host_route(
    HostRoute *routes,
    uint8_t &route_count,
    HostId host
) {
    for (uint8_t i = 0; i < route_count; ++i) {
        if (routes[i].host == host) {
            return &routes[i];
        }
    }

    HostRoute *route = &routes[route_count++];
    route->host = host;
    route->monitor_mask = 0;
    route->hub_mask = 0;
    return route;
}

static void create_host_node(
    lv_obj_t *parent,
    lv_coord_t center_x,
    lv_coord_t y,
    HostId host
) {
    const lv_color_t color = host_color(host);
    lv_obj_t *node = create_diagram_shape(
        parent,
        center_x - 20,
        y,
        40,
        18,
        lv_color_hex(0x101C2B),
        5
    );
    lv_obj_set_style_border_color(node, color, 0);
    lv_obj_set_style_border_width(node, 1, 0);
    create_status_label(node, host_label(host), 0, 1, 40, 16, &lv_font_montserrat_14, color, LV_TEXT_ALIGN_CENTER);
}

static void create_keyboard_icon(lv_obj_t *parent, lv_coord_t center_x) {
    const lv_color_t icon_color = lv_color_hex(0xE6EDF5);
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_pos(body, center_x - 14, 135);
    lv_obj_set_size(body, 28, 15);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x101C2B), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(body, icon_color, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_pad_all(body, 0, 0);

    for (lv_coord_t row = 0; row < 2; ++row) {
        for (lv_coord_t column = 0; column < 6; ++column) {
            create_diagram_shape(
                body,
                3 + column * 3,
                3 + row * 4,
                2,
                2,
                icon_color,
                1
            );
        }
    }
    create_diagram_shape(body, 9, 11, 10, 1, icon_color);
}

static void create_headset_icon(lv_obj_t *parent, lv_coord_t center_x) {
    const lv_color_t icon_color = lv_color_hex(0xE6EDF5);
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_pos(body, center_x - 10, 135);
    lv_obj_set_size(body, 20, 15);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(body, lv_color_hex(0x101C2B), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(body, icon_color, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_radius(body, 4, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    create_diagram_shape(body, 2, 2, 3, 9, icon_color, 2);
    create_diagram_shape(body, 15, 2, 3, 9, icon_color, 2);
    create_diagram_shape(body, 6, 1, 8, 2, icon_color, 2);
    create_diagram_shape(body, 2, 9, 4, 5, icon_color, 2);
    create_diagram_shape(body, 14, 9, 4, 5, icon_color, 2);
}

static void create_monitor(lv_obj_t *parent, lv_coord_t x, MonitorInput input) {
    const HostId host = input_host(input);
    const lv_color_t primary = lv_color_hex(0xE6EDF5);
    const lv_color_t color = host_color(host);

    lv_obj_t *bezel = lv_obj_create(parent);
    lv_obj_set_pos(bezel, x, 8);
    lv_obj_set_size(bezel, 52, 48);
    lv_obj_clear_flag(bezel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bezel, lv_color_hex(0x101C2B), 0);
    lv_obj_set_style_bg_opa(bezel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bezel, color, 0);
    lv_obj_set_style_border_width(bezel, 2, 0);
    lv_obj_set_style_radius(bezel, 5, 0);
    lv_obj_set_style_pad_all(bezel, 0, 0);

    lv_obj_t *screen = lv_obj_create(bezel);
    lv_obj_set_pos(screen, 3, 3);
    lv_obj_set_size(screen, 44, 40);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x12283A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_radius(screen, 2, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    create_status_label(
        screen,
        host_label(host),
        0,
        11,
        44,
        18,
        &lv_font_montserrat_14,
        primary,
        LV_TEXT_ALIGN_CENTER
    );

    lv_obj_t *stand = lv_obj_create(parent);
    lv_obj_set_pos(stand, x + 22, 56);
    lv_obj_set_size(stand, 6, 5);
    lv_obj_set_style_bg_color(stand, lv_color_hex(0x34536A), 0);
    lv_obj_set_style_bg_opa(stand, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(stand, 0, 0);
    lv_obj_set_style_radius(stand, 0, 0);
    lv_obj_set_style_pad_all(stand, 0, 0);

    lv_obj_t *foot = lv_obj_create(parent);
    lv_obj_set_pos(foot, x + 15, 61);
    lv_obj_set_size(foot, 20, 2);
    lv_obj_set_style_bg_color(foot, lv_color_hex(0x34536A), 0);
    lv_obj_set_style_bg_opa(foot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(foot, 0, 0);
    lv_obj_set_style_radius(foot, 1, 0);
    lv_obj_set_style_pad_all(foot, 0, 0);
}

static void create_routing_diagram(
    lv_obj_t *parent,
    const KvmState &state
) {
    const lv_coord_t monitor_centers[2] = {40, 95};
    const HostId monitor_hosts[2] = {
        input_host(state.monitor1_input),
        input_host(state.monitor2_input)
    };
    const HostId hub_hosts[2] = {state.hub1_host, state.hub2_host};
    HostRoute routes[4];
    uint8_t route_count = 0;

    for (uint8_t monitor = 0; monitor < 2; ++monitor) {
        HostRoute *route = find_or_add_host_route(routes, route_count, monitor_hosts[monitor]);
        route->monitor_mask |= static_cast<uint8_t>(1U << monitor);
    }
    for (uint8_t hub = 0; hub < 2; ++hub) {
        HostRoute *route = find_or_add_host_route(routes, route_count, hub_hosts[hub]);
        route->hub_mask |= static_cast<uint8_t>(1U << hub);
    }

    create_monitor(parent, 14, state.monitor1_input);
    create_monitor(parent, 69, state.monitor2_input);

    for (uint8_t route_index = 0; route_index < route_count; ++route_index) {
        const HostRoute &route = routes[route_index];
        if (route.hub_mask == 0) {
            continue;
        }

        const lv_color_t color = host_color(route.host);
        lv_coord_t device_centers[2];
        uint8_t device_count = 0;
        for (uint8_t hub = 0; hub < 2; ++hub) {
            if ((route.hub_mask & (1U << hub)) == 0) {
                continue;
            }

            if (route.hub_mask == 3) {
                if (route.monitor_mask == 1) {
                    device_centers[device_count++] = hub == 0 ? 26 : 55;
                } else if (route.monitor_mask == 2) {
                    device_centers[device_count++] = hub == 0 ? 80 : 109;
                } else {
                    device_centers[device_count++] = hub == 0 ? 44 : 93;
                }
            } else if (route.monitor_mask == 1) {
                device_centers[device_count++] = monitor_centers[0];
            } else if (route.monitor_mask == 2) {
                device_centers[device_count++] = monitor_centers[1];
            } else if (route.monitor_mask == 3) {
                device_centers[device_count++] = 68;
            } else {
                device_centers[device_count++] = hub == 0 ? 44 : 93;
            }
        }

        lv_coord_t route_center;
        if (route.monitor_mask == 1) {
            route_center = monitor_centers[0];
        } else if (route.monitor_mask == 2) {
            route_center = monitor_centers[1];
        } else if (route.monitor_mask == 3 || route.hub_mask == 3) {
            route_center = 68;
        } else {
            route_center = device_centers[0];
        }

        if (route.monitor_mask == 3) {
            for (uint8_t monitor = 0; monitor < 2; ++monitor) {
                create_diagram_shape(parent, monitor_centers[monitor] - 1, 64, 2, 14, color);
            }
            create_diagram_shape(parent, monitor_centers[0], 77, 57, 2, color);
            create_diagram_shape(parent, route_center - 1, 78, 2, 41, color);
        } else if (route.monitor_mask != 0) {
            const uint8_t monitor = route.monitor_mask == 1 ? 0 : 1;
            create_diagram_shape(
                parent,
                monitor_centers[monitor] - 1,
                64,
                2,
                55,
                color
            );
        } else {
            create_host_node(parent, route_center, 78, route.host);
            create_diagram_shape(parent, route_center - 1, 96, 2, 23, color);
        }

        if (device_count == 2) {
            create_diagram_shape(
                parent,
                device_centers[0],
                118,
                device_centers[1] - device_centers[0] + 1,
                2,
                color
            );
            for (uint8_t device = 0; device < device_count; ++device) {
                create_diagram_shape(parent, device_centers[device] - 1, 119, 2, 16, color);
            }
        } else {
            if (route_center != device_centers[0]) {
                create_diagram_shape(
                    parent,
                    route_center < device_centers[0] ? route_center : device_centers[0],
                    118,
                    (route_center < device_centers[0]
                        ? device_centers[0] - route_center
                        : route_center - device_centers[0]) + 1,
                    2,
                    color
                );
            }
            create_diagram_shape(parent, device_centers[0] - 1, 119, 2, 16, color);
        }

        for (uint8_t hub = 0; hub < 2; ++hub) {
            if ((route.hub_mask & (1U << hub)) == 0) {
                continue;
            }

            const lv_coord_t center_x = route.hub_mask == 3
                ? (hub == 0 ? 44 : 93)
                : device_centers[0];
            if (hub == 0) {
                create_keyboard_icon(parent, center_x);
            } else {
                create_headset_icon(parent, center_x);
            }
        }
    }
}

static void create_status_screen(void) {
    const lv_color_t primary = lv_color_hex(0xE6EDF5);
    const lv_color_t muted = lv_color_hex(0x91A3B8);
    const lv_color_t accent = lv_color_hex(0x55E6D2);
    lv_obj_t *screen = lv_scr_act();

    lv_obj_set_style_bg_color(screen, lv_color_hex(0x08111D), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    routing_diagram = lv_obj_create(screen);
    lv_obj_set_pos(routing_diagram, 0, 0);
    lv_obj_set_size(routing_diagram, 135, 170);
    lv_obj_clear_flag(routing_diagram, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(routing_diagram, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(routing_diagram, 0, 0);
    lv_obj_set_style_pad_all(routing_diagram, 0, 0);
    create_routing_diagram(routing_diagram, current_kvm_state);

    create_status_label(screen, "VOLUME", 8, 178, 60, 16, &lv_font_montserrat_14, accent);
    create_status_label(screen, "APP", 82, 178, 45, 16, &lv_font_montserrat_14, muted, LV_TEXT_ALIGN_RIGHT);
    lv_obj_t *volume_card = create_status_card(screen, 193, 40);
    create_status_label(volume_card, "PC", 10, 5, 45, 16, &lv_font_montserrat_14, muted);
    create_status_label(volume_card, "--%", 10, 21, 45, 17, &lv_font_montserrat_14, primary);
    create_status_label(volume_card, "MAC", 65, 5, 43, 16, &lv_font_montserrat_14, muted);
    create_status_label(volume_card, "--%", 65, 21, 43, 17, &lv_font_montserrat_14, primary);
}

static void render_current_scenario(void) {
    lv_obj_clean(routing_diagram);
    create_routing_diagram(routing_diagram, current_kvm_state);
}

static void display_flush_cb(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_map) {
    auto panel_handle = static_cast<esp_lcd_panel_handle_t>(disp_drv->user_data);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
        panel_handle,
        area->x1,
        area->y1,
        area->x2 + 1,
        area->y2 + 1,
        color_map
    ));
}

static bool display_color_trans_done_cb(
    esp_lcd_panel_io_handle_t,
    esp_lcd_panel_io_event_data_t *,
    void *user_ctx
) {
    auto *display_driver = static_cast<lv_disp_drv_t *>(user_ctx);
    lv_disp_flush_ready(display_driver);
    return false;
}

// This will intentionally crash the compiler if LVGL isn't correctly configured for 16-bit
static_assert(sizeof(lv_color_t) == 2, "LVGL must be configured for 16-bit color!");

// extern "C" is mandatory so the FreeRTOS bootloader can find the entry point
extern "C" void app_main(void) {

    vTaskDelay(pdMS_TO_TICKS(3000));

    printf("Booting Custom KVM Controller (C++)...\n");

    // 1. Route Power to the TFT and STEMMA port
    gpio_set_direction(PIN_POWER_TFT_I2C, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_POWER_TFT_I2C, 1);

    // Give the display hardware time to wake up before sending SPI commands
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // 2. Turn on the TFT Backlight
    gpio_set_direction(PIN_TFT_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_TFT_BACKLIGHT, 1);

    // 3. Initialize the Built-in NeoPixel
    gpio_set_direction(PIN_NEOPIXEL_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NEOPIXEL_POWER, 1);

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = PIN_NEOPIXEL;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000;

    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    // Set NeoPixel to Green (R, G, B) to confirm boot
    led_strip_set_pixel(led_strip, 0, 0, 255, 0);
    led_strip_refresh(led_strip);

    // The NeoKey FeatherWing switches are active-low and its two NeoPixels share D9.
    gpio_config_t neokey_gpio_config = {};
    neokey_gpio_config.pin_bit_mask =
        (1ULL << PIN_NEOKEY_SWITCH_A) | (1ULL << PIN_NEOKEY_SWITCH_B);
    neokey_gpio_config.mode = GPIO_MODE_INPUT;
    neokey_gpio_config.pull_up_en = GPIO_PULLUP_ENABLE;
    neokey_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    neokey_gpio_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&neokey_gpio_config));

    led_strip_config_t neokey_strip_config = {};
    neokey_strip_config.strip_gpio_num = PIN_NEOKEY_NEOPIXEL;
    neokey_strip_config.max_leds = 2;
    neokey_strip_config.led_model = LED_MODEL_WS2812;

    led_strip_handle_t neokey_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&neokey_strip_config, &rmt_config, &neokey_strip));

    const esp_err_t network_err = network_start();
    if (network_err != ESP_OK) {
        printf("Wi-Fi not started: %s\n", esp_err_to_name(network_err));
    }

    // 4. Initialize the SPI Master Bus for the ST7789 TFT
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = PIN_TFT_MOSI;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = PIN_TFT_CLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 240 * 135 * sizeof(uint16_t);
    
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 5. Configure the LCD IO Device (ST7789 protocol)
    esp_lcd_panel_io_handle_t io_handle = nullptr;
    
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = PIN_TFT_CS;
    io_config.dc_gpio_num = PIN_TFT_DC;
    io_config.spi_mode = 0;
    io_config.pclk_hz = 40 * 1000 * 1000;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.on_color_trans_done = display_color_trans_done_cb;
    io_config.user_ctx = &disp_drv;
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config, &io_handle));

    printf("Hardware initialized. Ready for LVGL integration.\n");
    
    // 6. Initialize the ST7789 Panel
    esp_lcd_panel_handle_t panel_handle = nullptr;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = PIN_TFT_RST;

    // LVGL stores RGB565 pixels little-endian on ESP32.
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
    panel_config.bits_per_pixel = 16;
        
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    // --- ADD THESE TWO LINES BACK ---
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // --------------------------------

    // Use the TFT's native portrait orientation (135 x 240).
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));

    // This unmirrored orientation uses the 52-pixel X offset for the odd-width panel.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 52, 40));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // --- 7. Initialize LVGL ---
    lv_init();
    
    // Allocate a draw buffer (1/10th of the screen size is standard for microcontrollers)
    const uint16_t screen_width = 135;
    const uint16_t screen_height = 240;
    
    // Allocate the buffer in DMA-capable internal memory
    size_t buffer_size = (screen_width * screen_height / 10) * sizeof(lv_color_t);
    lv_color_t *draw_buf_pixels = static_cast<lv_color_t *>(heap_caps_malloc(buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, draw_buf_pixels, nullptr, screen_width * screen_height / 10);

    // Register our display and flush callback with LVGL
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screen_width;
    disp_drv.ver_res = screen_height;
    disp_drv.flush_cb = display_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle; // Pass the hardware handle into the callback
    lv_disp_drv_register(&disp_drv);

    // --- 8. Draw the portrait KVM status screen ---
    create_status_screen();

    printf("UI Rendered. Entering main loop.\n");
    
    // --- 9. The Main Loop ---
    uint16_t heartbeat_ticks = 0;
    uint8_t animation_hue = 0;
    uint8_t key_hue_offset = 0;
    uint8_t animation_ticks = 0;
    KeyDebounceState key_a_state;
    KeyDebounceState key_b_state;

    while (true) {
        // LVGL needs to know how much time has passed to handle animations and rendering
        lv_tick_inc(10);
        lv_timer_handler(); 

        bool scenario_changed = false;
        if (key_press_detected(PIN_NEOKEY_SWITCH_A, key_a_state)) {
            key_hue_offset += 48;
            randomize_scenario(current_kvm_state);
            scenario_changed = true;
        }
        if (key_press_detected(PIN_NEOKEY_SWITCH_B, key_b_state)) {
            key_hue_offset += 96;
            randomize_scenario(current_kvm_state);
            scenario_changed = true;
        }
        if (scenario_changed) {
            render_current_scenario();
        }

        if (++animation_ticks >= 3) {
            animation_ticks = 0;
            ++animation_hue;

            for (uint32_t pixel = 0; pixel < 2; ++pixel) {
                uint8_t red;
                uint8_t green;
                uint8_t blue;
                const uint8_t hue = static_cast<uint8_t>(
                    animation_hue + key_hue_offset + pixel * 128
                );
                hue_to_rgb(hue, red, green, blue);
                ESP_ERROR_CHECK(led_strip_set_pixel(neokey_strip, pixel, red, green, blue));
            }
            ESP_ERROR_CHECK(led_strip_refresh(neokey_strip));
        }

        if (++heartbeat_ticks >= 200) {
            heartbeat_ticks = 0;
            printf("Controller alive\n");
            fflush(stdout);
        }
        
        // Yield to FreeRTOS for 10ms
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}