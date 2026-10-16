#include <cstdio>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
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

#define PIN_NEOKEY_SWITCH_MENU GPIO_NUM_5
#define PIN_NEOKEY_SWITCH_A GPIO_NUM_6
#define PIN_NEOKEY_SWITCH_FUNCTION GPIO_NUM_10
#define PIN_NEOKEY_SWITCH_B GPIO_NUM_11
#define PIN_NEOKEY_NEOPIXEL GPIO_NUM_9
#define NEOKEY_NEOPIXEL_COUNT 4

// Reserved for future KVM SWD and monitor DDC control; not initialized yet.
#define PIN_KVM1_SWDIO GPIO_NUM_18 // Feather A0
#define PIN_KVM1_SWCLK GPIO_NUM_17 // Feather A1
#define PIN_KVM2_SWDIO GPIO_NUM_16 // Feather A2
#define PIN_KVM2_SWCLK GPIO_NUM_15 // Feather A3
#define PIN_MONITOR1_DDC_SCL GPIO_NUM_14 // Feather A4
#define PIN_MONITOR1_DDC_SDA GPIO_NUM_8  // Feather A5
#define PIN_MONITOR2_DDC_SCL GPIO_NUM_2  // Feather RX
#define PIN_MONITOR2_DDC_SDA GPIO_NUM_1  // Feather TX

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
    DisplayPort,
    UsbC,
    Hdmi
};

struct KvmState {
    MonitorInput monitor1_input;
    MonitorInput monitor2_input;
    HostId hub1_host;
    HostId hub2_host;
};

enum class HubLayout : uint8_t {
    Joint,
    Split,
    SplitSwapped
};

static KvmState current_kvm_state = {
    MonitorInput::DisplayPort,
    MonitorInput::DisplayPort,
    HostId::Mac,
    HostId::Mac
};
static lv_obj_t *routing_diagram = nullptr;
static lv_obj_t *state_code_label = nullptr;

static const char *host_label(HostId host) {
    switch (host) {
        case HostId::Pc: return "PC";
        case HostId::Mac: return "MAC";
        case HostId::Guest1: return "3";
        case HostId::Guest2: return "4";
    }
    return "";
}

static HostId input_host(MonitorInput input, uint8_t monitor_index) {
    switch (input) {
        case MonitorInput::UsbC: return HostId::Mac;
        case MonitorInput::DisplayPort: return HostId::Pc;
        case MonitorInput::Hdmi:
            return monitor_index == 0 ? HostId::Guest1 : HostId::Guest2;
    }
    return HostId::Guest1;
}

static void advance_monitor_input(MonitorInput &input) {
    input = static_cast<MonitorInput>(
        (static_cast<uint8_t>(input) + 1) % 3
    );
}

static void advance_hub_host(HostId &host) {
    host = static_cast<HostId>(
        (static_cast<uint8_t>(host) + 1) % 4
    );
}

static void update_state_code(const KvmState &state) {
    char code[5] = {
        static_cast<char>('1' + static_cast<uint8_t>(state.monitor1_input)),
        static_cast<char>('1' + static_cast<uint8_t>(state.monitor2_input)),
        static_cast<char>('1' + static_cast<uint8_t>(state.hub1_host)),
        static_cast<char>('1' + static_cast<uint8_t>(state.hub2_host)),
        '\0'
    };
    lv_label_set_text(state_code_label, code);
}

static lv_color_t host_color(HostId host) {
    switch (host) {
        case HostId::Pc: return lv_color_hex(0xFFC857);
        case HostId::Mac: return lv_color_hex(0x4F8FEF);
        case HostId::Guest1: return lv_color_hex(0xB99AFF);
        case HostId::Guest2: return lv_color_hex(0xFF8B7B);
    }
    return lv_color_hex(0x91A3B8);
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

static void create_keyboard_icon(lv_obj_t *parent, lv_coord_t center_x, lv_coord_t y) {
    const lv_color_t icon_color = lv_color_hex(0xE6EDF5);
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_pos(body, center_x - 14, y);
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

static void create_camera_icon(lv_obj_t *parent, lv_coord_t center_x, lv_coord_t y) {
    const lv_color_t icon_color = lv_color_hex(0xE6EDF5);
    lv_obj_t *camera = lv_obj_create(parent);
    lv_obj_set_pos(camera, center_x - 10, y + 2);
    lv_obj_set_size(camera, 16, 12);
    lv_obj_clear_flag(camera, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(camera, lv_color_hex(0x101C2B), 0);
    lv_obj_set_style_bg_opa(camera, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(camera, icon_color, 0);
    lv_obj_set_style_border_width(camera, 1, 0);
    lv_obj_set_style_radius(camera, 3, 0);
    lv_obj_set_style_pad_all(camera, 0, 0);
    create_diagram_shape(camera, 5, 3, 6, 6, icon_color, 3);
    create_diagram_shape(parent, center_x + 6, y + 4, 4, 8, icon_color, 1);
}

static void create_monitor_input_icon(lv_obj_t *screen, MonitorInput input) {
    const lv_color_t icon_color = lv_color_hex(0xDCEBFF);
    const lv_color_t screen_color = lv_color_hex(0x12283A);

    if (input == MonitorInput::DisplayPort) {
        const lv_color_t window_color = lv_color_hex(0x58B9FF);
        create_diagram_shape(screen, 11, 10, 9, 8, window_color);
        create_diagram_shape(screen, 22, 9, 10, 8, window_color);
        create_diagram_shape(screen, 11, 20, 9, 9, window_color);
        create_diagram_shape(screen, 22, 19, 10, 9, window_color);
    } else if (input == MonitorInput::UsbC) {
        create_diagram_shape(screen, 14, 13, 10, 11, icon_color, 5);
        create_diagram_shape(screen, 21, 13, 10, 11, icon_color, 5);
        create_diagram_shape(screen, 16, 19, 13, 9, icon_color, 5);
        create_diagram_shape(screen, 26, 13, 5, 5, screen_color, 2);
        create_diagram_shape(screen, 20, 11, 2, 5, icon_color, 1);
        create_diagram_shape(screen, 23, 9, 5, 3, lv_color_hex(0x78C8FF), 2);
    } else {
        lv_obj_t *connector = lv_obj_create(screen);
        lv_obj_set_pos(connector, 12, 12);
        lv_obj_set_size(connector, 20, 13);
        lv_obj_clear_flag(connector, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_opa(connector, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(connector, icon_color, 0);
        lv_obj_set_style_border_width(connector, 2, 0);
        lv_obj_set_style_radius(connector, 2, 0);
        lv_obj_set_style_pad_all(connector, 0, 0);

        for (lv_coord_t pin = 0; pin < 5; ++pin) {
            create_diagram_shape(
                connector,
                3 + pin * 3,
                5,
                2,
                3,
                icon_color,
                1
            );
        }
        create_diagram_shape(screen, 18, 25, 8, 3, icon_color, 1);
        create_diagram_shape(screen, 20, 28, 4, 4, icon_color, 1);
    }
}

static void create_monitor(lv_obj_t *parent, lv_coord_t x, MonitorInput input, uint8_t monitor_index) {
    const HostId host = input_host(input, monitor_index);
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
    create_monitor_input_icon(screen, input);

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

static HubLayout hub_layout(
    const KvmState &state,
    HostId monitor1_host,
    HostId monitor2_host
) {
    if (state.hub1_host == state.hub2_host) {
        return HubLayout::Joint;
    }
    const bool monitor1_matches_a_hub =
        monitor1_host == state.hub1_host || monitor1_host == state.hub2_host;
    const bool camera_matches_monitor1 = state.hub2_host == monitor1_host;
    const bool keyboard_matches_monitor2 = state.hub1_host == monitor2_host;
    return camera_matches_monitor1
        || (!monitor1_matches_a_hub && keyboard_matches_monitor2)
        ? HubLayout::SplitSwapped
        : HubLayout::Split;
}

static void create_routing_diagram(lv_obj_t *parent, const KvmState &state) {
    const lv_coord_t monitor_centers[2] = {40, 95};
    const lv_coord_t monitor_bottom_y = 64;
    const lv_coord_t connector_gap = 4;
    const lv_coord_t monitor_connector_y = monitor_bottom_y + connector_gap;
    const lv_coord_t split_route_bus_y = 72;
    const lv_coord_t single_monitor_bus_y = 74;
    const lv_coord_t shared_monitor_bus_y = 70;
    const lv_coord_t hub_icon_y = 91;
    const lv_coord_t hub_connector_end_y = hub_icon_y - connector_gap;
    const lv_coord_t hub_connector_start_y = hub_icon_y + 15 + connector_gap;
    const lv_coord_t host_node_y = 128;
    const HostId monitor_hosts[2] = {
        input_host(state.monitor1_input, 0),
        input_host(state.monitor2_input, 1)
    };
    const HostId hub_hosts[2] = {state.hub1_host, state.hub2_host};
    const bool joint = state.hub1_host == state.hub2_host;
    const HubLayout layout = hub_layout(state, monitor_hosts[0], monitor_hosts[1]);
    const lv_coord_t hub_centers[2] = {
        layout == HubLayout::Joint ? static_cast<lv_coord_t>(51) :
            (layout == HubLayout::Split ? monitor_centers[0] : monitor_centers[1]),
        layout == HubLayout::Joint ? static_cast<lv_coord_t>(89) :
            (layout == HubLayout::Split ? monitor_centers[1] : monitor_centers[0])
    };
    const lv_coord_t group_center = 68;

    create_monitor(parent, 14, state.monitor1_input, 0);
    create_monitor(parent, 69, state.monitor2_input, 1);

    if (joint) {
        uint8_t matching_monitors = 0;
        for (uint8_t monitor = 0; monitor < 2; ++monitor) {
            if (monitor_hosts[monitor] == hub_hosts[0]) {
                ++matching_monitors;
            }
        }
        const lv_color_t color = host_color(hub_hosts[0]);
        if (matching_monitors == 2) {
            for (uint8_t monitor = 0; monitor < 2; ++monitor) {
                create_diagram_shape(
                    parent,
                    monitor_centers[monitor] - 1,
                    monitor_connector_y,
                    2,
                    shared_monitor_bus_y - monitor_connector_y,
                    color
                );
            }
            create_diagram_shape(
                parent,
                monitor_centers[0],
                shared_monitor_bus_y,
                monitor_centers[1] - monitor_centers[0] + 1,
                2,
                color
            );
            create_diagram_shape(
                parent,
                group_center - 1,
                shared_monitor_bus_y + 2,
                2,
                hub_connector_end_y - shared_monitor_bus_y - 2,
                color
            );
        } else if (matching_monitors == 1) {
            const uint8_t monitor = monitor_hosts[0] == hub_hosts[0] ? 0 : 1;
            create_diagram_shape(
                parent,
                monitor_centers[monitor] - 1,
                monitor_connector_y,
                2,
                single_monitor_bus_y - monitor_connector_y,
                color
            );
            const lv_coord_t left = monitor_centers[monitor] < group_center
                ? monitor_centers[monitor]
                : group_center;
            create_diagram_shape(
                parent,
                left,
                single_monitor_bus_y,
                (monitor_centers[monitor] > group_center
                    ? monitor_centers[monitor] - group_center
                    : group_center - monitor_centers[monitor]) + 1,
                2,
                color
            );
            create_diagram_shape(
                parent,
                group_center - 1,
                single_monitor_bus_y + 2,
                2,
                hub_connector_end_y - single_monitor_bus_y - 2,
                color
            );
        }
    } else {
        for (uint8_t monitor = 0; monitor < 2; ++monitor) {
            uint8_t hub = 0;
            if (monitor_hosts[monitor] == hub_hosts[1]) {
                hub = 1;
            } else if (monitor_hosts[monitor] != hub_hosts[0]) {
                continue;
            }

            const lv_color_t color = host_color(hub_hosts[hub]);
            if (monitor_centers[monitor] == hub_centers[hub]) {
                create_diagram_shape(
                    parent,
                    monitor_centers[monitor] - 1,
                    monitor_connector_y,
                    2,
                    hub_connector_end_y - monitor_connector_y,
                    color
                );
            } else {
                const lv_coord_t left = monitor_centers[monitor] < hub_centers[hub]
                    ? monitor_centers[monitor]
                    : hub_centers[hub];
                create_diagram_shape(
                    parent,
                    monitor_centers[monitor] - 1,
                    monitor_connector_y,
                    2,
                    split_route_bus_y - monitor_connector_y,
                    color
                );
                create_diagram_shape(
                    parent,
                    left,
                    split_route_bus_y,
                    (monitor_centers[monitor] > hub_centers[hub]
                        ? monitor_centers[monitor] - hub_centers[hub]
                        : hub_centers[hub] - monitor_centers[monitor]) + 1,
                    2,
                    color
                );
                create_diagram_shape(
                    parent,
                    hub_centers[hub] - 1,
                    split_route_bus_y + 2,
                    2,
                    hub_connector_end_y - split_route_bus_y - 2,
                    color
                );
            }
        }
    }

    if (layout == HubLayout::Joint) {
        const lv_color_t color = host_color(hub_hosts[0]);
        create_diagram_shape(
            parent,
            group_center - 1,
            hub_connector_start_y,
            2,
            host_node_y - hub_connector_start_y,
            color
        );
        create_keyboard_icon(parent, hub_centers[0], hub_icon_y);
        create_status_label(
            parent,
            "/",
            67,
            hub_icon_y - 1,
            10,
            17,
            &lv_font_montserrat_14,
            lv_color_hex(0x91A3B8),
            LV_TEXT_ALIGN_CENTER
        );
        create_camera_icon(parent, hub_centers[1], hub_icon_y);
        create_host_node(parent, group_center, host_node_y, hub_hosts[0]);
    } else {
        for (uint8_t hub = 0; hub < 2; ++hub) {
            const lv_color_t color = host_color(hub_hosts[hub]);
            create_diagram_shape(
                parent,
                hub_centers[hub] - 1,
                hub_connector_start_y,
                2,
                host_node_y - hub_connector_start_y,
                color
            );
            if (hub == 0) {
                create_keyboard_icon(parent, hub_centers[hub], hub_icon_y);
            } else {
                create_camera_icon(parent, hub_centers[hub], hub_icon_y);
            }
            create_host_node(parent, hub_centers[hub], host_node_y, hub_hosts[hub]);
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

    lv_obj_t *state_code_card = create_status_card(screen, 178, 55);
    create_status_label(
        state_code_card,
        "M1  M2  H1  H2",
        0,
        4,
        117,
        16,
        &lv_font_montserrat_14,
        accent,
        LV_TEXT_ALIGN_CENTER
    );
    state_code_label = create_status_label(
        state_code_card,
        "",
        0,
        23,
        117,
        28,
        &lv_font_montserrat_22,
        primary,
        LV_TEXT_ALIGN_CENTER
    );
    update_state_code(current_kvm_state);
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

    // NeoKey switches are active-low; their NeoPixels share D9.
    gpio_config_t neokey_gpio_config = {};
    neokey_gpio_config.pin_bit_mask =
        (1ULL << PIN_NEOKEY_SWITCH_A)
        | (1ULL << PIN_NEOKEY_SWITCH_B)
        | (1ULL << PIN_NEOKEY_SWITCH_MENU)
        | (1ULL << PIN_NEOKEY_SWITCH_FUNCTION);
    neokey_gpio_config.mode = GPIO_MODE_INPUT;
    neokey_gpio_config.pull_up_en = GPIO_PULLUP_ENABLE;
    neokey_gpio_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    neokey_gpio_config.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&neokey_gpio_config));

    led_strip_config_t neokey_strip_config = {};
    neokey_strip_config.strip_gpio_num = PIN_NEOKEY_NEOPIXEL;
    neokey_strip_config.max_leds = NEOKEY_NEOPIXEL_COUNT;
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
    KeyDebounceState key_menu_state;
    KeyDebounceState key_function_state;

    while (true) {
        // LVGL needs to know how much time has passed to handle animations and rendering
        lv_tick_inc(10);
        lv_timer_handler(); 

        bool scenario_changed = false;
        if (key_press_detected(PIN_NEOKEY_SWITCH_A, key_a_state)) {
            key_hue_offset += 48;
            advance_hub_host(current_kvm_state.hub1_host);
            scenario_changed = true;
        }
        if (key_press_detected(PIN_NEOKEY_SWITCH_B, key_b_state)) {
            key_hue_offset += 96;
            advance_hub_host(current_kvm_state.hub2_host);
            scenario_changed = true;
        }
        if (key_press_detected(PIN_NEOKEY_SWITCH_MENU, key_menu_state)) {
            key_hue_offset += 144;
            advance_monitor_input(current_kvm_state.monitor1_input);
            scenario_changed = true;
        }
        if (key_press_detected(PIN_NEOKEY_SWITCH_FUNCTION, key_function_state)) {
            key_hue_offset += 192;
            advance_monitor_input(current_kvm_state.monitor2_input);
            scenario_changed = true;
        }
        if (scenario_changed) {
            update_state_code(current_kvm_state);
            render_current_scenario();
        }

        if (++animation_ticks >= 3) {
            animation_ticks = 0;
            ++animation_hue;

            for (uint32_t pixel = 0; pixel < NEOKEY_NEOPIXEL_COUNT; ++pixel) {
                uint8_t red;
                uint8_t green;
                uint8_t blue;
                const uint8_t hue = static_cast<uint8_t>(
                    animation_hue + key_hue_offset
                    + pixel * (256 / NEOKEY_NEOPIXEL_COUNT)
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