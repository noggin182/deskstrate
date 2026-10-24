// DDC1 bench bring-up: probe 0x37/0x50 and log VCP 0x60, 0xD6 and the EDID serial
// every 5 s. Read-only: nothing here changes a monitor setting.
#include "ddc_bringup.h"

#include "board_config.h"
#include "ddc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_DESKSTRATE_DDC_BRINGUP

static const char *TAG = "ddc1";

static void ddc_bringup_task(void *) {
    const ddc::Pins pins{board::kDdc1Sda, board::kDdc1Scl};

#if CONFIG_DESKSTRATE_DDC_TOGGLE_TEST
    ESP_LOGW(TAG, "toggle test: meter on HDMI pin 16 (SDA) / 15 (SCL) vs pin 17");
    ddc::line_toggle_test(pins);
#endif

    static ddc::Bus bus;
    esp_err_t err = bus.init(I2C_NUM_0, pins);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init failed: %s", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }

    bool have_serial = false;
    for (;;) {
        const esp_err_t ddcci_err = bus.probe_ddcci();
        const esp_err_t edid_err = bus.probe_edid();
        const bool ddcci = ddcci_err == ESP_OK;
        const bool edid = edid_err == ESP_OK;
        ESP_LOGI(TAG, "ACK 0x37 (DDC/CI): %s, 0x50 (EDID): %s",
                 ddcci ? "yes" : esp_err_to_name(ddcci_err), edid ? "yes" : esp_err_to_name(edid_err));
        if (!ddcci && !edid) {
            // Both idle high = bus looks healthy, nothing answering; a 0 = stuck low / no pull-up.
            ESP_LOGW(TAG, "idle lines: SDA (GPIO%d) = %d, SCL (GPIO%d) = %d", pins.sda,
                     bus.sda_level(), pins.scl, bus.scl_level());
        }

        if (ddcci) {
            uint8_t input = 0;
            uint16_t raw = 0;
            err = bus.get_input(input, &raw);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "input 0x%02X = %s (raw 0x%04X, via port 0x%02X%s)", input,
                         ddc::input_name(input), raw, ddc::query_port(raw),
                         ddc::query_port(raw) == ddc::kInputHdmi ? " = HDMI, as expected" : "");
            } else {
                ESP_LOGW(TAG, "VCP 0x60: %s", esp_err_to_name(err));
            }

            ddc::VcpValue power{};
            err = bus.get_vcp(ddc::kVcpPower, power);
            if (err == ESP_OK) ESP_LOGI(TAG, "power D6 = %u", power.current & 0xFF);
        }

        if (edid && !have_serial) {
            uint32_t serial = 0;
            if (bus.read_edid_serial(serial) == ESP_OK) {
                ESP_LOGI(TAG, "EDID serial %lu (%s)", (unsigned long)serial,
                         serial == 810959170 ? "right" : serial == 810562882 ? "left" : "unknown");
                have_serial = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void ddc_bringup_start() {
    xTaskCreate(ddc_bringup_task, "ddc1", 4096, nullptr, 3, nullptr);
}

#else

void ddc_bringup_start() {}

#endif
