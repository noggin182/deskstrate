#include "network.h"

#include <cerrno>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "wifi_sta.h"

namespace {

constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;

const char *TAG = "deck_network";
EventGroupHandle_t network_events;

// kvm_proto client state, guarded by kvm_mutex.
SemaphoreHandle_t kvm_mutex;
int kvm_socket = -1;
sockaddr_in core_addr = {};
bool core_known = false;
uint8_t latest_state[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};
uint8_t latest_levels[KVM_LEVELS][KVM_LEVEL_LEN] = {{KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN},
                                                     {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN}};
bool state_fresh = false;

// The state asked for and not yet seen in a STATE ('-' = nothing asked). UDP can drop
// a request, so it's sent again every kResendMs until a STATE shows it, for kGiveUpMs.
// A REQUEST is the state wanted, so a repeat that arrives after all changes nothing.
constexpr uint32_t kResendMs = 300;
constexpr uint32_t kGiveUpMs = 3000;
uint8_t wanted[KVM_SLOTS] = {KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN, KVM_UNKNOWN};
bool pending = false;
TickType_t asked_at = 0;
TickType_t sent_at = 0;
uint8_t sends = 0;

// Call with kvm_mutex held.
bool send_request_locked(const uint8_t b[KVM_SLOTS]) {
    sockaddr_in destination = core_addr;
    if (!core_known) {
        destination = {};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(KVM_PORT);
        destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }
    const kvm_msg_t msg = kvm_msg(KVM_REQUEST, b);
    uint8_t raw[KVM_MAX_LEN];
    const unsigned len = kvm_encode(&msg, raw);
    const ssize_t sent = sendto(kvm_socket, raw, len, 0, reinterpret_cast<sockaddr *>(&destination), sizeof(destination));
    return sent == static_cast<ssize_t>(len);
}


// Call with kvm_mutex held: drops the request once a STATE shows it, else resends it.
void check_pending_locked() {
    if (!pending) return;
    if (kvm_shows(latest_state, wanted)) {
        if (sends > 1) ESP_LOGI(TAG, "Request took %u sends", sends);
        pending = false;
        return;
    }
    const TickType_t now = xTaskGetTickCount();
    if (now - asked_at >= pdMS_TO_TICKS(kGiveUpMs)) {
        ESP_LOGW(TAG, "Request %.4s not shown after %u sends; state %.4s", reinterpret_cast<const char *>(wanted),
                 sends, reinterpret_cast<const char *>(latest_state));
        pending = false;
        return;
    }
    if (now - sent_at >= pdMS_TO_TICKS(kResendMs)) {
        send_request_locked(wanted);
        sent_at = now;
        ++sends;
    }
}

void kvm_task(void *) {
    uint8_t buf[64];
    for (;;) {
        sockaddr_in from = {};
        socklen_t from_len = sizeof(from);
        const int n = recvfrom(kvm_socket, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &from_len);
        kvm_msg_t msg;
        const bool state = n > 0 && kvm_decode(buf, n, KVM_STATE, &msg);

        xSemaphoreTake(kvm_mutex, portMAX_DELAY);
        if (state) {
            memcpy(latest_state, msg.b, sizeof(latest_state));
            memcpy(latest_levels, msg.level, sizeof(latest_levels));
            state_fresh = true;
            if (!core_known || core_addr.sin_addr.s_addr != from.sin_addr.s_addr) {
                ESP_LOGI(TAG, "Core found at " IPSTR, IP2STR(reinterpret_cast<esp_ip4_addr_t *>(&from.sin_addr)));
            }
            core_addr = from;
            core_known = true;
        }
        check_pending_locked();
        xSemaphoreGive(kvm_mutex);
    }
}

esp_err_t start_kvm_client() {
    kvm_mutex = xSemaphoreCreateMutex();
    if (kvm_mutex == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    kvm_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (kvm_socket < 0) {
        ESP_LOGE(TAG, "Failed to create kvm_proto socket: errno %d", errno);
        return ESP_FAIL;
    }
    const int enable_broadcast = 1;
    setsockopt(kvm_socket, SOL_SOCKET, SO_BROADCAST, &enable_broadcast, sizeof(enable_broadcast));
    timeval tv = {.tv_sec = 0, .tv_usec = 50 * 1000};  // wake up for resends
    setsockopt(kvm_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(KVM_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(kvm_socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind kvm_proto port %u: errno %d", static_cast<unsigned>(KVM_PORT), errno);
        close(kvm_socket);
        kvm_socket = -1;
        return ESP_FAIL;
    }

    if (xTaskCreate(kvm_task, "kvm_client", 4096, nullptr, 5, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "kvm_proto client on port %u (unauthenticated)", static_cast<unsigned>(KVM_PORT));
    return ESP_OK;
}

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Wi-Fi connection: %s", esp_err_to_name(err));
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *disconnected = static_cast<wifi_event_sta_disconnected_t *>(event_data);
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %d); reconnecting", disconnected->reason);
        xEventGroupClearBits(network_events, WIFI_CONNECTED_BIT);
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reconnect to Wi-Fi: %s", esp_err_to_name(err));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Connected; IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(network_events, WIFI_CONNECTED_BIT);
    }
}

} // namespace

esp_err_t network_start(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Resetting NVS to recover from incompatible or full storage");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "failed to erase NVS");
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "failed to initialize NVS");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "failed to initialize network interface");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "failed to create default event loop");
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == nullptr) {
        ESP_LOGE(TAG, "failed to create Wi-Fi station interface");
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(esp_netif_set_hostname(sta_netif, "deskstrate-deck"), TAG,
                        "failed to set hostname");

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&wifi_init_config), TAG, "failed to initialize Wi-Fi");

    network_events = xEventGroupCreate();
    if (network_events == nullptr) {
        ESP_LOGE(TAG, "failed to allocate Wi-Fi event group");
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr),
        TAG,
        "failed to register Wi-Fi event handler"
    );
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr),
        TAG,
        "failed to register IP event handler"
    );

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to set Wi-Fi station mode");
    // This build's menuconfig credentials, or the ones stored in NVS (see wifi_sta.h).
    ESP_RETURN_ON_ERROR(wifi_sta_apply_credentials(), TAG, "no usable Wi-Fi credentials");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start Wi-Fi");
    ESP_RETURN_ON_ERROR(start_kvm_client(), TAG, "failed to start kvm_proto client");

    ESP_LOGI(TAG, "Wi-Fi station started");
    return ESP_OK;
}

bool network_take_kvm_state(uint8_t out[KVM_SLOTS], uint8_t levels[KVM_LEVELS][KVM_LEVEL_LEN]) {
    if (kvm_mutex == nullptr) {
        return false;
    }
    xSemaphoreTake(kvm_mutex, portMAX_DELAY);
    const bool fresh = state_fresh;
    if (fresh) {
        memcpy(out, latest_state, KVM_SLOTS);
        memcpy(levels, latest_levels, sizeof(latest_levels));
        state_fresh = false;
    }
    xSemaphoreGive(kvm_mutex);
    return fresh;
}

esp_err_t network_send_kvm_request(const uint8_t want[KVM_SLOTS]) {
    if (kvm_mutex == nullptr || kvm_socket < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((xEventGroupGetBits(network_events) & WIFI_CONNECTED_BIT) == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    bool asks = false;
    for (uint8_t i = 0; i < KVM_SLOTS; ++i) asks |= want[i] != KVM_UNKNOWN;
    xSemaphoreTake(kvm_mutex, portMAX_DELAY);
    if (asks) {
        if (!pending) {
            memset(wanted, KVM_UNKNOWN, sizeof(wanted));
            sends = 0;
        }
        // On top of a request still in flight: the latest wins for each byte.
        for (uint8_t i = 0; i < KVM_SLOTS; ++i) {
            if (want[i] != KVM_UNKNOWN) wanted[i] = want[i];
        }
        pending = true;
        asked_at = xTaskGetTickCount();
        sent_at = asked_at;
        ++sends;
    }
    // A request of all '-' changes nothing: it just asks the core for a STATE.
    const bool sent = send_request_locked(asks ? wanted : want);
    const int send_errno = errno;
    xSemaphoreGive(kvm_mutex);

    if (!sent) {
        ESP_LOGE(TAG, "kvm_proto request failed: errno %d", send_errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}
