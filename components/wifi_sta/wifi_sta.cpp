#include "wifi_sta.h"

#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {

constexpr EventBits_t kConnectedBit = BIT0;
const char *TAG = "wifi_sta";
EventGroupHandle_t events = nullptr;

void on_event(void *, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *d = static_cast<wifi_event_sta_disconnected_t *>(data);
        ESP_LOGW(TAG, "disconnected (reason %d); reconnecting", d->reason);
        xEventGroupClearBits(events, kConnectedBit);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto *e = static_cast<ip_event_got_ip_t *>(data);
        ESP_LOGI(TAG, "connected, IP " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(events, kConnectedBit);
    }
}

}  // namespace

esp_err_t wifi_sta_apply_credentials() {
    wifi_config_t cfg = {};
    const size_t ssid_len = strlen(CONFIG_DESKSTRATE_WIFI_SSID);
    const size_t pass_len = strlen(CONFIG_DESKSTRATE_WIFI_PASSWORD);
    if (ssid_len > 0) {
        if (ssid_len > 32 || pass_len > 64) {
            ESP_LOGE(TAG, "menuconfig SSID (max 32) or password (max 64) too long");
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(cfg.sta.ssid, CONFIG_DESKSTRATE_WIFI_SSID, ssid_len);
        memcpy(cfg.sta.password, CONFIG_DESKSTRATE_WIFI_PASSWORD, pass_len);
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "sta config");
        ESP_LOGI(TAG, "credentials from this build (stored in NVS)");
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_get_config(WIFI_IF_STA, &cfg), TAG, "stored config");
    if (cfg.sta.ssid[0] == 0) {
        ESP_LOGE(TAG, "no Wi-Fi credentials stored: flash a build with menuconfig -> "
                      "Deskstrate Wi-Fi set over USB once");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "credentials from NVS");
    return ESP_OK;
}

esp_err_t wifi_sta_start(const char *hostname) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "erasing NVS (full or incompatible)");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "NVS init");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    if (!netif) return ESP_ERR_NO_MEM;
    ESP_RETURN_ON_ERROR(esp_netif_set_hostname(netif, hostname), TAG, "hostname");

    events = xEventGroupCreate();
    if (!events) return ESP_ERR_NO_MEM;

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr),
                        TAG, "wifi handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr),
                        TAG, "ip handler");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "sta mode");
    ESP_RETURN_ON_ERROR(wifi_sta_apply_credentials(), TAG, "credentials");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    ESP_LOGI(TAG, "started as %s", hostname);
    return ESP_OK;
}

bool wifi_sta_connected() {
    return events && (xEventGroupGetBits(events) & kConnectedBit);
}

bool wifi_sta_wait_connected(TickType_t timeout) {
    if (!events) return false;
    return xEventGroupWaitBits(events, kConnectedBit, pdFALSE, pdTRUE, timeout) & kConnectedBit;
}
