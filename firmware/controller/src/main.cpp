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

    // Now you can rotate and configure it
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, true));

    // The Adafruit 1.14" TFT uses 40-column and 53-row offsets.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 40, 53));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // --- 7. Initialize LVGL ---
    lv_init();
    
    // Allocate a draw buffer (1/10th of the screen size is standard for microcontrollers)
    const uint16_t screen_width = 240;
    const uint16_t screen_height = 135;
    
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

    // --- 8. Draw "Hello World" ---
    lv_obj_t *label = lv_label_create(lv_scr_act()); // Create label on the active screen
    lv_label_set_text(label, "Hello, George!");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_22, 0);

    lv_obj_center(label);                            // Automatically calculate coordinates

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

        if (key_press_detected(PIN_NEOKEY_SWITCH_A, key_a_state)) {
            key_hue_offset += 48;
        }
        if (key_press_detected(PIN_NEOKEY_SWITCH_B, key_b_state)) {
            key_hue_offset += 96;
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