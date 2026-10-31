#include <cstdio>
#include <cstdint>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "controls.h"
#include "icons.h"
#include "menu.h"
#include "neokeys.h"
#include "network.h"
#include "nvs.h"
#include "ota_pull.h"
#include "status_led.h"
#include "lvgl.h"

// Adafruit Feather ESP32-S3 TFT pinout
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

// Orange on the status LED after this long without a STATE from the core
// (KVM_HEARTBEAT_MS apart when it's up).
#define CORE_SILENT_MS 10000
// Until a STATE arrives (at boot, or after the core went silent), ask for one this often.
#define STATE_PING_MS 1000
// The menu closes itself after this long without a key press.
#define MENU_IDLE_MS 20000
// NeoKey frame period.
#define NEOKEY_FRAME_MS 30

static lv_disp_drv_t disp_drv;

struct KeyDebounceState {
    bool stable_pressed = false;
    bool raw_pressed = false;
    uint8_t stable_samples = 0;
};

// Returns the debounced level (true = pressed): it follows the pin once it has read
// the same for three samples in a row.
static bool key_debounced(gpio_num_t pin, KeyDebounceState &state) {
    const bool pressed = gpio_get_level(pin) == 0;
    if (pressed != state.raw_pressed) {
        state.raw_pressed = pressed;
        state.stable_samples = 0;
    } else if (pressed != state.stable_pressed && ++state.stable_samples >= 3) {
        state.stable_pressed = pressed;
        state.stable_samples = 0;
    }
    return state.stable_pressed;
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
    Guest2,
    None     // hub not connected, or a monitor with no host (standby, not connected)
};

enum class MonitorInput : uint8_t {
    DisplayPort,
    UsbC,
    Hdmi,
    Standby,
    Disconnected  // not connected to the core, or its input couldn't be read
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
static lv_obj_t *state_code_card = nullptr;
static lv_obj_t *waiting_panel = nullptr;
static lv_obj_t *waiting_label = nullptr;

static const char *host_label(HostId host) {
    switch (host) {
        case HostId::Pc: return "PC";
        case HostId::Mac: return "MAC";
        case HostId::Guest1: return "3";
        case HostId::Guest2: return "4";
        case HostId::None: return "--";
    }
    return "";
}

static HostId input_host(MonitorInput input, uint8_t monitor_index) {
    switch (input) {
        case MonitorInput::UsbC: return HostId::Mac;
        case MonitorInput::DisplayPort: return HostId::Pc;
        case MonitorInput::Hdmi:
            return monitor_index == 0 ? HostId::Guest1 : HostId::Guest2;
        case MonitorInput::Standby:
        case MonitorInput::Disconnected:
            return HostId::None;
    }
    return HostId::None;
}

// A route joins a monitor and a hub only when both are on the same real host.
static bool same_host(HostId a, HostId b) {
    return a == b && a != HostId::None;
}

// kvm_proto <-> deck view. HostId is already in kvm_usb_port order (PC, Mac, Guest1, Guest2).
static MonitorInput from_kvm_input(uint8_t input) {
    switch (input) {
        case KVM_IN_DP: return MonitorInput::DisplayPort;
        case KVM_IN_USBC: return MonitorInput::UsbC;
        case KVM_IN_HDMI: return MonitorInput::Hdmi;
        case KVM_IN_OFF: return MonitorInput::Standby;
    }
    return MonitorInput::Disconnected;  // KEEP: the core can't see it
}

static HostId from_kvm_port(uint8_t port) {
    return port <= KVM_USB_GUEST2 ? static_cast<HostId>(port) : HostId::None;
}

// Copies the core's STATE into the view. Returns true if anything visible changed.
static bool apply_core_state(const kvm_state_t &core, KvmState &view) {
    KvmState next;
    next.monitor1_input = from_kvm_input(core.mon_count > KVM_MON_LEFT ? core.mon[KVM_MON_LEFT] : KVM_IN_KEEP);
    next.monitor2_input = from_kvm_input(core.mon_count > KVM_MON_RIGHT ? core.mon[KVM_MON_RIGHT] : KVM_IN_KEEP);
    next.hub1_host = from_kvm_port(core.usb[KVM_HUB_HID]);
    next.hub2_host = from_kvm_port(core.usb[KVM_HUB_MEDIA]);

    const bool changed = next.monitor1_input != view.monitor1_input
        || next.monitor2_input != view.monitor2_input
        || next.hub1_host != view.hub1_host
        || next.hub2_host != view.hub2_host;
    view = next;
    return changed;
}

// Keys ask the core; the screen only changes when the core's STATE comes back.
static void request_change(uint8_t action, uint8_t a, uint8_t b) {
    const esp_err_t err = network_send_kvm_request(action, a, b);
    if (err != ESP_OK) {
        printf("Request not sent: %s\n", esp_err_to_name(err));
    }
}

// Asks for every part of target, even parts the core already reports: the core skips
// those switches but still wakes the host asked for (STATE.wake).
static void request_target(const controls::Target &target, const controls::View &view) {
    for (uint8_t mon = 0; mon < 2; ++mon) {
        // A monitor or hub that isn't connected is skipped: the core would refuse it.
        if (view.mon[mon] == KVM_IN_KEEP) continue;
        if (target.mon[mon] != KVM_IN_KEEP) request_change(KVM_ACT_MON, mon, target.mon[mon]);
    }
    for (uint8_t hub = 0; hub < KVM_HUB_COUNT; ++hub) {
        if (view.usb[hub] == KVM_USB_UNKNOWN) continue;
        if (target.usb[hub] != KVM_USB_UNKNOWN) request_change(KVM_ACT_USB, hub, target.usb[hub]);
    }
}

// The core's STATE as controls/neokeys/menu see it (keeps standby and unknown).
static void update_view(const kvm_state_t &core, controls::View &view) {
    for (uint8_t mon = 0; mon < 2; ++mon) {
        view.mon[mon] = core.mon_count > mon ? core.mon[mon] : KVM_IN_KEEP;
    }
    for (uint8_t hub = 0; hub < KVM_HUB_COUNT; ++hub) {
        view.usb[hub] = core.usb[hub];
    }
}

// Menu settings (menu.h) are kept in NVS so they survive updates.
static menu::Settings load_settings(void) {
    menu::Settings settings;
    nvs_handle_t nvs;
    if (nvs_open("deck", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t value;
        if (nvs_get_u8(nvs, "lights", &value) == ESP_OK && value < menu::kLightLevels) {
            settings.light_level = value;
        }
        if (nvs_get_u8(nvs, "debug_code", &value) == ESP_OK) settings.debug_code = value != 0;
        nvs_close(nvs);
    }
    return settings;
}

static void save_settings(const menu::Settings &settings) {
    nvs_handle_t nvs;
    if (nvs_open("deck", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_set_u8(nvs, "lights", settings.light_level);
    nvs_set_u8(nvs, "debug_code", settings.debug_code ? 1 : 0);
    nvs_commit(nvs);
    nvs_close(nvs);
}

static char monitor_digit(MonitorInput input) {
    switch (input) {
        case MonitorInput::DisplayPort: return '1';
        case MonitorInput::UsbC: return '2';
        case MonitorInput::Hdmi: return '3';
        case MonitorInput::Standby: return '0';
        case MonitorInput::Disconnected: return '-';
    }
    return '?';
}

static char hub_digit(HostId host) {
    return host == HostId::None ? '-' : static_cast<char>('1' + static_cast<uint8_t>(host));
}

static void update_state_code(const KvmState &state) {
    char code[5] = {
        monitor_digit(state.monitor1_input),
        monitor_digit(state.monitor2_input),
        hub_digit(state.hub1_host),
        hub_digit(state.hub2_host),
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
        case HostId::None: break;
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

// Hub icons are drawn dim when the hub isn't connected.
static lv_color_t hub_icon_color(bool online) {
    return online ? lv_color_hex(0xE6EDF5) : lv_color_hex(0x34536A);
}

static void create_keyboard_icon(lv_obj_t *parent, lv_coord_t center_x, lv_coord_t y, bool online) {
    const lv_color_t icon_color = hub_icon_color(online);
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

static void create_camera_icon(lv_obj_t *parent, lv_coord_t center_x, lv_coord_t y, bool online) {
    const lv_color_t icon_color = hub_icon_color(online);
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

// An alpha-only logo (icons.h) tinted with color: LVGL draws ALPHA_8BIT images in the
// img_recolor colour, so the anti-aliased edges blend into the screen.
static void create_logo_icon(lv_obj_t *parent, const lv_img_dsc_t *icon, lv_coord_t x, lv_coord_t y, lv_color_t color) {
    lv_obj_t *img = lv_img_create(parent);
    lv_img_set_src(img, icon);
    lv_obj_set_pos(img, x, y);
    lv_obj_set_style_img_recolor(img, color, 0);
    lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
}

static void create_monitor_input_icon(lv_obj_t *screen, MonitorInput input) {
    const lv_color_t icon_color = lv_color_hex(0xDCEBFF);

    if (input == MonitorInput::DisplayPort) {
        create_logo_icon(screen, &icon_windows, 13, 10, lv_color_hex(0x58B9FF));
    } else if (input == MonitorInput::UsbC) {
        create_logo_icon(screen, &icon_apple, 13, 8, icon_color);
    } else if (input == MonitorInput::Standby || input == MonitorInput::Disconnected) {
        create_status_label(
            screen,
            input == MonitorInput::Standby ? LV_SYMBOL_POWER : LV_SYMBOL_CLOSE,
            0,
            12,
            44,
            16,
            &lv_font_montserrat_14,
            lv_color_hex(0x5B738A),
            LV_TEXT_ALIGN_CENTER
        );
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
    // Standby keeps a grey bezel; a monitor the core can't see is dimmer still.
    const lv_color_t color = input == MonitorInput::Disconnected ? lv_color_hex(0x34536A) : host_color(host);

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
        same_host(monitor1_host, state.hub1_host) || same_host(monitor1_host, state.hub2_host);
    const bool camera_matches_monitor1 = same_host(state.hub2_host, monitor1_host);
    const bool keyboard_matches_monitor2 = same_host(state.hub1_host, monitor2_host);
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
            if (same_host(monitor_hosts[monitor], hub_hosts[0])) {
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
            const uint8_t monitor = same_host(monitor_hosts[0], hub_hosts[0]) ? 0 : 1;
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
            if (same_host(monitor_hosts[monitor], hub_hosts[1])) {
                hub = 1;
            } else if (!same_host(monitor_hosts[monitor], hub_hosts[0])) {
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

    // A hub that isn't connected has no line down to its host node ("--").
    const bool hub_online[2] = {hub_hosts[0] != HostId::None, hub_hosts[1] != HostId::None};
    if (layout == HubLayout::Joint) {
        const lv_color_t color = host_color(hub_hosts[0]);
        if (hub_online[0]) {
            create_diagram_shape(
                parent,
                group_center - 1,
                hub_connector_start_y,
                2,
                host_node_y - hub_connector_start_y,
                color
            );
        }
        create_keyboard_icon(parent, hub_centers[0], hub_icon_y, hub_online[0]);
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
        create_camera_icon(parent, hub_centers[1], hub_icon_y, hub_online[1]);
        create_host_node(parent, group_center, host_node_y, hub_hosts[0]);
    } else {
        for (uint8_t hub = 0; hub < 2; ++hub) {
            const lv_color_t color = host_color(hub_hosts[hub]);
            if (hub_online[hub]) {
                create_diagram_shape(
                    parent,
                    hub_centers[hub] - 1,
                    hub_connector_start_y,
                    2,
                    host_node_y - hub_connector_start_y,
                    color
                );
            }
            if (hub == 0) {
                create_keyboard_icon(parent, hub_centers[hub], hub_icon_y, hub_online[hub]);
            } else {
                create_camera_icon(parent, hub_centers[hub], hub_icon_y, hub_online[hub]);
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

    state_code_card = create_status_card(screen, 178, 55);
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

// The state code card covers the volume panel while it is shown.
// Covers the status screen until the core's first STATE, and again whenever it goes
// silent, so the deck never shows a state it hasn't been told.
static void create_waiting_panel(void) {
    waiting_panel = lv_obj_create(lv_scr_act());
    lv_obj_set_pos(waiting_panel, 0, 0);
    lv_obj_set_size(waiting_panel, 135, 240);
    lv_obj_clear_flag(waiting_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(waiting_panel, lv_color_hex(0x08111D), 0);
    lv_obj_set_style_bg_opa(waiting_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(waiting_panel, 0, 0);
    lv_obj_set_style_radius(waiting_panel, 0, 0);
    lv_obj_set_style_pad_all(waiting_panel, 0, 0);

    lv_obj_t *spinner = lv_spinner_create(waiting_panel, 1000, 60);
    lv_obj_set_size(spinner, 56, 56);
    lv_obj_set_pos(spinner, 40, 72);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x24364A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x55E6D2), LV_PART_INDICATOR);

    waiting_label = create_status_label(
        waiting_panel,
        "",
        0,
        142,
        135,
        40,
        &lv_font_montserrat_14,
        lv_color_hex(0x91A3B8),
        LV_TEXT_ALIGN_CENTER
    );
}

enum class Waiting : uint8_t { Hidden, Starting, ForCore, LostCore };

static void show_waiting(Waiting wanted) {
    // Called every loop: only touch LVGL when something changes.
    static Waiting shown = Waiting::Hidden;
    static bool drawn = false;
    if (drawn && wanted == shown) return;
    drawn = true;
    shown = wanted;
    switch (wanted) {
        case Waiting::Hidden: lv_obj_add_flag(waiting_panel, LV_OBJ_FLAG_HIDDEN); return;
        case Waiting::Starting: lv_label_set_text(waiting_label, "Starting up..."); break;
        case Waiting::ForCore: lv_label_set_text(waiting_label, "Waiting for\nthe core..."); break;
        case Waiting::LostCore: lv_label_set_text(waiting_label, "Lost the core\nreconnecting..."); break;
    }
    lv_obj_clear_flag(waiting_panel, LV_OBJ_FLAG_HIDDEN);
}

// Full-screen overlay while the deck updates itself (ota_pull::progress()): a download
// icon and progress bar, then a power symbol in a spinning ring while it restarts.
// Drawn on the system layer, above the status screen and the menu.
static lv_obj_t *ota_panel = nullptr;
static lv_obj_t *ota_bar = nullptr;
static lv_obj_t *ota_detail = nullptr;

static lv_obj_t *create_ota_panel(void) {
    lv_obj_t *panel = lv_obj_create(lv_layer_sys());
    lv_obj_set_pos(panel, 0, 0);
    lv_obj_set_size(panel, 135, 240);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x08111D), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    return panel;
}

static void draw_ota_downloading(const char *version) {
    const lv_color_t accent = lv_color_hex(0x55E6D2);
    lv_obj_t *ring = lv_obj_create(ota_panel);
    lv_obj_set_pos(ring, 38, 40);
    lv_obj_set_size(ring, 60, 60);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ring, lv_color_hex(0x101C2B), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ring, accent, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    create_status_label(ring, LV_SYMBOL_DOWNLOAD, 0, 15, 54, 26, &lv_font_montserrat_22, accent, LV_TEXT_ALIGN_CENTER);

    create_status_label(ota_panel, "Updating", 0, 112, 135, 26, &lv_font_montserrat_22, lv_color_hex(0xE6EDF5), LV_TEXT_ALIGN_CENTER);
    lv_obj_t *to = create_status_label(ota_panel, "", 0, 140, 135, 16, &lv_font_montserrat_14, lv_color_hex(0x91A3B8), LV_TEXT_ALIGN_CENTER);
    if (strncmp(version, "build-", 6) == 0) version += 6;
    lv_label_set_text_fmt(to, "to %s", version);
    lv_label_set_long_mode(to, LV_LABEL_LONG_SCROLL_CIRCULAR);

    ota_bar = lv_bar_create(ota_panel);
    lv_obj_set_pos(ota_bar, 12, 172);
    lv_obj_set_size(ota_bar, 111, 12);
    lv_bar_set_range(ota_bar, 0, 1000);
    lv_obj_set_style_bg_color(ota_bar, lv_color_hex(0x24364A), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ota_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(ota_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ota_bar, accent, LV_PART_INDICATOR);
    lv_obj_set_style_radius(ota_bar, 6, LV_PART_INDICATOR);

    ota_detail = create_status_label(ota_panel, "", 0, 192, 135, 16, &lv_font_montserrat_14, lv_color_hex(0x91A3B8), LV_TEXT_ALIGN_CENTER);
}

static void draw_ota_restarting(const char *version) {
    const lv_color_t green = lv_color_hex(0x4BE37A);
    lv_obj_t *spinner = lv_spinner_create(ota_panel, 1200, 90);
    lv_obj_set_size(spinner, 70, 70);
    lv_obj_set_pos(spinner, 33, 36);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, lv_color_hex(0x1C3A2A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, green, LV_PART_INDICATOR);
    create_status_label(ota_panel, LV_SYMBOL_POWER, 33, 59, 70, 26, &lv_font_montserrat_22, green, LV_TEXT_ALIGN_CENTER);

    create_status_label(ota_panel, "Restarting", 0, 120, 135, 26, &lv_font_montserrat_22, lv_color_hex(0xE6EDF5), LV_TEXT_ALIGN_CENTER);
    lv_obj_t *detail = create_status_label(ota_panel, "", 0, 150, 135, 40, &lv_font_montserrat_14, lv_color_hex(0x91A3B8), LV_TEXT_ALIGN_CENTER);
    if (version[0] == '\0') {
        lv_label_set_text(detail, "rolling back\nto the last build");
    } else {
        if (strncmp(version, "build-", 6) == 0) version += 6;
        lv_label_set_text_fmt(detail, "into %s", version);
    }
}

static void update_ota_panel(void) {
    static ota_pull::Phase shown = ota_pull::Phase::Idle;
    static uint32_t shown_permille = UINT32_MAX;
    const ota_pull::Progress p = ota_pull::progress();

    if (p.phase != shown) {
        shown = p.phase;
        shown_permille = UINT32_MAX;
        if (ota_panel != nullptr) {
            lv_obj_del(ota_panel);
            ota_panel = nullptr;
            ota_bar = nullptr;
            ota_detail = nullptr;
        }
        if (p.phase == ota_pull::Phase::Idle) return;  // finished or failed: back to normal
        ota_panel = create_ota_panel();
        if (p.phase == ota_pull::Phase::Downloading) {
            draw_ota_downloading(p.version);
        } else {
            draw_ota_restarting(p.version);
        }
    }

    if (p.phase == ota_pull::Phase::Downloading) {
        const uint32_t permille = p.total > 0 ? static_cast<uint32_t>(static_cast<uint64_t>(p.bytes) * 1000 / p.total) : 0;
        if (permille == shown_permille) return;
        shown_permille = permille;
        lv_bar_set_value(ota_bar, static_cast<int32_t>(permille), LV_ANIM_OFF);
        if (p.total > 0) {
            lv_label_set_text_fmt(ota_detail, "%lu%%  %lu/%lu KB", (unsigned long)(permille / 10),
                                  (unsigned long)(p.bytes / 1024), (unsigned long)(p.total / 1024));
        } else {
            lv_label_set_text(ota_detail, "connecting...");
        }
    }
}

static void show_state_code(bool show) {
    if (show) {
        lv_obj_clear_flag(state_code_card, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(state_code_card, LV_OBJ_FLAG_HIDDEN);
    }
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

    printf("Booting Deskstrate deck...\n");

    // 1. Route Power to the TFT and STEMMA port
    gpio_set_direction(PIN_POWER_TFT_I2C, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_POWER_TFT_I2C, 1);

    // Give the display hardware time to wake up before sending SPI commands
    vTaskDelay(pdMS_TO_TICKS(150));
    
    // 2. Turn on the TFT Backlight
    gpio_set_direction(PIN_TFT_BACKLIGHT, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_TFT_BACKLIGHT, 1);

    // 3. The built-in NeoPixel is the status LED (status_led.h): green while booting
    gpio_set_direction(PIN_NEOPIXEL_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_NEOPIXEL_POWER, 1);
    status_led::start(PIN_NEOPIXEL);

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

    if (neokeys::start(PIN_NEOKEY_NEOPIXEL) != ESP_OK) printf("NeoKey lights not started\n");

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
    create_waiting_panel();
    show_waiting(Waiting::Starting);

    // The screen comes up first so a restart (after an update) never leaves the last
    // image on it. Then three seconds for the USB console to attach before the boot
    // log, with LVGL running so the spinner turns.
    for (int i = 0; i < 300; ++i) {
        lv_tick_inc(10);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    esp_err_t network_err = network_start();
    if (network_err != ESP_OK) {
        printf("Wi-Fi not started: %s\n", esp_err_to_name(network_err));
    } else {
        // Updates itself from GitHub releases: hourly, or on a UDP doorbell (ota_pull.h).
        network_err = ota_pull::start(ota_pull::kDeckPort);
        if (network_err != ESP_OK) printf("updater not started: %s\n", esp_err_to_name(network_err));
    }
    if (network_err != ESP_OK) status_led::set_fault(status_led::kFaultStartup, true);


    printf("UI Rendered. Entering main loop.\n");
    // Display up and nothing crashed: a new OTA image is confirmed once it's on the
    // network, otherwise it rolls back.
    ota_pull::confirm_boot(network_err == ESP_OK);
    status_led::set_ready();
    
    // NVS is up once the network has started (network_start initialises it).
    menu::Settings settings = load_settings();
    show_state_code(settings.debug_code);

    // --- 9. The Main Loop ---
    uint16_t heartbeat_ticks = 0;
    uint8_t neokey_ticks = 0;
    const gpio_num_t key_pins[controls::kKeyCount] = {
        PIN_NEOKEY_SWITCH_MENU,      // top left
        PIN_NEOKEY_SWITCH_FUNCTION,  // top right
        PIN_NEOKEY_SWITCH_A,         // bottom left
        PIN_NEOKEY_SWITCH_B,         // bottom right
    };
    KeyDebounceState key_states[controls::kKeyCount];
    controls::Gestures gestures;
    uint8_t prev_pressed = 0;
    controls::View view;
    bool core_seen = false;
    TickType_t last_core_state = xTaskGetTickCount();
    TickType_t last_key = xTaskGetTickCount();
    TickType_t last_ping = 0;

    while (true) {
        // LVGL needs to know how much time has passed to handle animations and rendering
        lv_tick_inc(10);
        lv_timer_handler(); 

        const uint32_t now_ms = pdTICKS_TO_MS(xTaskGetTickCount());
        uint8_t pressed = 0;
        for (uint8_t k = 0; k < controls::kKeyCount; ++k) {
            if (key_debounced(key_pins[k], key_states[k])) pressed |= 1u << k;
        }
        const uint8_t newly_pressed = pressed & ~prev_pressed;
        prev_pressed = pressed;
        for (uint8_t k = 0; k < controls::kKeyCount; ++k) {
            if (newly_pressed & (1u << k)) neokeys::flash(static_cast<controls::Key>(k), now_ms);
        }
        if (newly_pressed) last_key = xTaskGetTickCount();

        controls::Event events[controls::kKeyCount];
        const uint8_t event_count = gestures.update(pressed, now_ms, menu::is_open(), events);
        for (uint8_t i = 0; i < event_count; ++i) {
            const controls::Event &event = events[i];
            if (menu::is_open()) {
                const menu::Result r = menu::key(event.key, view, settings);
                switch (r.command) {
                    case menu::Command::None: break;
                    case menu::Command::Close: menu::close(); break;
                    case menu::Command::Monitor: request_change(KVM_ACT_MON, r.a, r.b); break;
                    case menu::Command::Usb: request_change(KVM_ACT_USB, r.a, r.b); break;
                    case menu::Command::Swap: request_target(controls::plan_swap(view), view); break;
                    case menu::Command::ScreensOff: {
                        controls::Target off;
                        off.mon[KVM_MON_LEFT] = KVM_IN_OFF;
                        off.mon[KVM_MON_RIGHT] = KVM_IN_OFF;
                        request_target(off, view);
                        menu::close();
                        break;
                    }
                    case menu::Command::Lights:
                        settings.light_level = r.a;
                        save_settings(settings);
                        menu::refresh(view, settings);
                        break;
                    case menu::Command::DebugCode:
                        settings.debug_code = r.a != 0;
                        save_settings(settings);
                        show_state_code(settings.debug_code);
                        menu::refresh(view, settings);
                        break;
                }
            } else if (event.type == controls::EventType::Tap && event.key == controls::kTopLeft) {
                menu::open(view, settings);
            } else {
                controls::Target target;
                if (controls::plan(event, view, target)) request_target(target, view);
            }
        }
        if (menu::is_open() && xTaskGetTickCount() - last_key >= pdMS_TO_TICKS(MENU_IDLE_MS)) {
            menu::close();
        }

        kvm_state_t core_state;
        if (network_take_kvm_state(core_state)) {
            last_core_state = xTaskGetTickCount();
            core_seen = true;
            update_view(core_state, view);
            menu::refresh(view, settings);
            if (apply_core_state(core_state, current_kvm_state)) {
                update_state_code(current_kvm_state);
                render_current_scenario();
            }
        }
        const bool core_silent = xTaskGetTickCount() - last_core_state >= pdMS_TO_TICKS(CORE_SILENT_MS);
        status_led::set_core_silent(core_silent);
        // The core answers any request with its STATE, so this clears the waiting screen
        // as soon as Wi-Fi is up instead of at the next heartbeat. Errors before Wi-Fi
        // has an IP are expected, so they aren't logged.
        if ((!core_seen || core_silent) && xTaskGetTickCount() - last_ping >= pdMS_TO_TICKS(STATE_PING_MS)) {
            last_ping = xTaskGetTickCount();
            network_send_kvm_request(KVM_ACT_GET_STATE, 0, 0);
        }
        show_waiting(!core_seen ? Waiting::ForCore : (core_silent ? Waiting::LostCore : Waiting::Hidden));
        update_ota_panel();

        if (++neokey_ticks >= NEOKEY_FRAME_MS / 10) {
            neokey_ticks = 0;
            neokeys::Scene scene;
            scene.core_seen = core_seen;
            scene.menu = menu::is_open();
            scene.held = pressed;
            scene.view = view;
            scene.brightness = menu::light_brightness(settings.light_level);
            neokeys::render(scene, now_ms);
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