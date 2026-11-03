#include "menu.h"

#include <cstring>

#include "esp_app_desc.h"
#include "lvgl.h"

namespace menu {

namespace {

enum Item : uint8_t {
    kLeft,
    kRight,
    kKeyboard,
    kMedia,
    kSwap,
    kLights,
    kDebugCode,
    kExit,
    kItemCount
};

const char *const kItemNames[kItemCount] = {
    "Left", "Right", "Keyboard", "Media", "Swap screens", "Key lights", "Debug code", "Exit",
};

constexpr lv_coord_t kRowY = 30;
constexpr lv_coord_t kRowHeight = 20;

lv_obj_t *panel = nullptr;
lv_obj_t *rows[kItemCount] = {};
lv_obj_t *values[kItemCount] = {};
uint8_t selected = 0;

const char *input_name(uint8_t mon) {
    if (kvm_mon_standby(mon)) return "Standby";
    switch (kvm_mon_host(mon)) {
        case KVM_HOST_PC: return "DP";
        case KVM_HOST_MAC: return "USB-C";
        case KVM_HOST_GUEST: return "HDMI";
    }
    return "n/c";  // not connected (or unreadable)
}

const char *host_name(uint8_t host) {
    switch (host) {
        case controls::kPc: return "PC";
        case controls::kMac: return "Mac";
        case controls::kGuest1: return "Guest 3";
        case controls::kGuest2: return "Guest 4";
    }
    return "n/c";  // hub not connected
}

const char *light_name(uint8_t level) {
    switch (level) {
        case 0: return "High";
        case 1: return "Low";
    }
    return "Off";
}

uint8_t next_input(uint8_t mon) {
    switch (kvm_mon_host(mon)) {
        case KVM_HOST_PC: return KVM_HOST_MAC;
        case KVM_HOST_MAC: return KVM_HOST_GUEST;
    }
    return KVM_HOST_PC;  // from HDMI or unknown
}

uint8_t next_host(uint8_t host) {
    return host < controls::kGuest2 ? host + 1 : controls::kPc;
}

void highlight() {
    for (uint8_t i = 0; i < kItemCount; ++i) {
        const bool on = i == selected;
        lv_obj_set_style_bg_opa(rows[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(rows[i], on ? lv_color_hex(0x08111D) : lv_color_hex(0xE6EDF5), 0);
    }
}

}  // namespace

uint8_t light_brightness(uint8_t level) {
    switch (level) {
        case 0: return 255;
        case 1: return 70;
    }
    return 0;
}

bool is_open() { return panel != nullptr; }

void open(const controls::View &view, const Settings &settings) {
    if (panel != nullptr) return;
    selected = 0;

    panel = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(panel, 0, 0);
    lv_obj_set_size(panel, 135, 240);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x08111D), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "MENU");
    lv_obj_set_pos(title, 8, 6);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x55E6D2), 0);

    for (uint8_t i = 0; i < kItemCount; ++i) {
        lv_obj_t *row = lv_obj_create(panel);
        lv_obj_set_pos(row, 4, kRowY + i * kRowHeight);
        lv_obj_set_size(row, 127, kRowHeight - 2);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x55E6D2), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_text_font(row, &lv_font_montserrat_14, 0);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, kItemNames[i]);
        lv_obj_set_pos(name, 5, 2);

        lv_obj_t *value = lv_label_create(row);
        lv_label_set_text(value, "");
        lv_obj_set_width(value, 60);
        lv_obj_set_pos(value, 62, 2);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);

        rows[i] = row;
        values[i] = value;
    }

    // Labels inherit the row's text colour; the version sits at the bottom.
    lv_obj_t *version = lv_label_create(panel);
    // CI versions are "build-<run>.<attempt>[-<commits>-g<sha>]": drop the prefix so
    // it fits, and scroll anything still too wide rather than cutting it off.
    const char *version_text = esp_app_get_description()->version;
    if (strncmp(version_text, "build-", 6) == 0) version_text += 6;
    lv_label_set_text(version, version_text);
    lv_label_set_long_mode(version, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(version, 135);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(version, 0, kRowY + kItemCount * kRowHeight + 4);
    lv_obj_set_style_text_font(version, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(version, lv_color_hex(0x91A3B8), 0);

    highlight();
    refresh(view, settings);
}

void close() {
    if (panel == nullptr) return;
    lv_obj_del(panel);
    panel = nullptr;
}

void refresh(const controls::View &view, const Settings &settings) {
    if (panel == nullptr) return;
    lv_label_set_text(values[kLeft], input_name(view.b[KVM_MON_LEFT]));
    lv_label_set_text(values[kRight], input_name(view.b[KVM_MON_RIGHT]));
    lv_label_set_text(values[kKeyboard], host_name(view.b[KVM_HUB_HID]));
    lv_label_set_text(values[kMedia], host_name(view.b[KVM_HUB_MEDIA]));
    lv_label_set_text(values[kLights], light_name(settings.light_level));
    lv_label_set_text(values[kDebugCode], settings.debug_code ? "On" : "Off");
}

Result key(controls::Key key, const controls::View &view, const Settings &settings) {
    Result r;
    switch (key) {
        case controls::kTopLeft:
            r.command = Command::Close;
            return r;
        case controls::kBottomLeft:
            selected = selected == 0 ? kItemCount - 1 : selected - 1;
            highlight();
            return r;
        case controls::kBottomRight:
            selected = (selected + 1) % kItemCount;
            highlight();
            return r;
        default:
            break;
    }

    switch (selected) {
        case kLeft:
        case kRight: {
            const uint8_t mon = selected == kLeft ? KVM_MON_LEFT : KVM_MON_RIGHT;
            if (view.b[mon] == KVM_UNKNOWN) break;  // not connected
            r = {Command::Monitor, mon, next_input(view.b[mon])};
            break;
        }
        case kKeyboard:
        case kMedia: {
            const uint8_t hub = selected == kKeyboard ? KVM_HUB_HID : KVM_HUB_MEDIA;
            if (view.b[hub] == KVM_UNKNOWN) break;  // not connected
            r = {Command::Usb, hub, next_host(view.b[hub])};
            break;
        }
        case kSwap: r.command = Command::Swap; break;
        case kLights: r = {Command::Lights, static_cast<uint8_t>((settings.light_level + 1) % kLightLevels), 0}; break;
        case kDebugCode: r = {Command::DebugCode, static_cast<uint8_t>(!settings.debug_code), 0}; break;
        case kExit: r.command = Command::Close; break;
    }
    return r;
}

}  // namespace menu
