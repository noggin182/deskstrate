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
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {

constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
constexpr EventBits_t UDP_SOCKET_READY_BIT = BIT1;
constexpr EventBits_t WIFI_ANNOUNCE_BIT = BIT2;
constexpr uint16_t UDP_BROADCAST_PORT = 3333;
constexpr size_t MAX_UDP_MESSAGE_SIZE = 1400;

const char *TAG = "deck_network";
EventGroupHandle_t network_events;
SemaphoreHandle_t socket_mutex;
int broadcast_socket = -1;

// kvm_proto client state, guarded by kvm_mutex.
SemaphoreHandle_t kvm_mutex;
int kvm_socket = -1;
sockaddr_in core_addr = {};
bool core_known = false;
kvm_state_t latest_state = {};
bool state_fresh = false;
uint32_t boot_epoch = 0;
uint32_t packet_n = 0;
uint32_t next_req_id = 0;

// KVM_COUNTER: (boot_epoch << 32) | n, boot_epoch bumped in NVS once per boot.
void load_boot_epoch() {
    nvs_handle_t nvs;
    if (nvs_open("kvm", NVS_READWRITE, &nvs) != ESP_OK) return;
    nvs_get_u32(nvs, "epoch", &boot_epoch);
    boot_epoch++;
    nvs_set_u32(nvs, "epoch", boot_epoch);
    nvs_commit(nvs);
    nvs_close(nvs);
}

void kvm_task(void *) {
    uint8_t buf[128];
    for (;;) {
        sockaddr_in from = {};
        socklen_t from_len = sizeof(from);
        const int n = recvfrom(kvm_socket, buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &from_len);
        if (n < static_cast<int>(sizeof(kvm_hdr_t))) {
            continue;
        }
        kvm_hdr_t hdr;
        memcpy(&hdr, buf, sizeof(hdr));  // decode by copy, never by casting the buffer
        if (hdr.magic != KVM_MAGIC || hdr.version != KVM_VERSION || hdr.sender != KVM_NODE_CORE) {
            continue;
        }

        if (hdr.type == KVM_STATE && n == static_cast<int>(sizeof(kvm_state_t))) {
            xSemaphoreTake(kvm_mutex, portMAX_DELAY);
            memcpy(&latest_state, buf, sizeof(latest_state));
            state_fresh = true;
            if (!core_known || core_addr.sin_addr.s_addr != from.sin_addr.s_addr) {
                ESP_LOGI(TAG, "Core found at " IPSTR, IP2STR(reinterpret_cast<esp_ip4_addr_t *>(&from.sin_addr)));
            }
            core_addr = from;
            core_known = true;
            xSemaphoreGive(kvm_mutex);
        } else if (hdr.type == KVM_RESULT && n == static_cast<int>(sizeof(kvm_result_t))) {
            kvm_result_t result;
            memcpy(&result, buf, sizeof(result));
            if (result.code != KVM_RES_OK) {
                ESP_LOGW(TAG, "Request %u refused by core: code %u", static_cast<unsigned>(result.req_id), result.code);
            }
        }
    }
}

esp_err_t start_kvm_client() {
    load_boot_epoch();
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
        xEventGroupClearBits(network_events, WIFI_CONNECTED_BIT | UDP_SOCKET_READY_BIT);
        const esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to reconnect to Wi-Fi: %s", esp_err_to_name(err));
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Connected; IP address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(network_events, WIFI_CONNECTED_BIT | WIFI_ANNOUNCE_BIT);
    }
}

void udp_socket_task(void *) {
    int socket_fd = -1;
    while (socket_fd < 0) {
        socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (socket_fd < 0) {
            ESP_LOGE(TAG, "Failed to create UDP socket: errno %d", errno);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    const int enable_broadcast = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_BROADCAST, &enable_broadcast, sizeof(enable_broadcast)) < 0) {
        ESP_LOGE(TAG, "Failed to enable UDP broadcast: errno %d", errno);
        close(socket_fd);
        vTaskDelete(nullptr);
        return;
    }

    xSemaphoreTake(socket_mutex, portMAX_DELAY);
    broadcast_socket = socket_fd;
    xSemaphoreGive(socket_mutex);

    while (true) {
        xEventGroupWaitBits(network_events, WIFI_ANNOUNCE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        if ((xEventGroupGetBits(network_events) & WIFI_CONNECTED_BIT) == 0) {
            continue;
        }

        xEventGroupSetBits(network_events, UDP_SOCKET_READY_BIT);
        ESP_LOGI(TAG, "UDP broadcast ready on port %u", UDP_BROADCAST_PORT);
        const esp_err_t announce_err = network_broadcast_update("{\"type\":\"status\",\"event\":\"online\"}");
        if (announce_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to broadcast online announcement: %s", esp_err_to_name(announce_err));
        }
    }
}

} // namespace

esp_err_t network_start(void) {
    const size_t ssid_length = strlen(CONFIG_DESKSTRATE_WIFI_SSID);
    const size_t password_length = strlen(CONFIG_DESKSTRATE_WIFI_PASSWORD);
    if (ssid_length == 0 || ssid_length > 32 || password_length > 64) {
        ESP_LOGE(TAG, "Set a Wi-Fi SSID (max 32 bytes) and password (max 64 bytes) in menuconfig");
        return ESP_ERR_INVALID_ARG;
    }

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
    socket_mutex = xSemaphoreCreateMutex();
    if (network_events == nullptr || socket_mutex == nullptr) {
        ESP_LOGE(TAG, "failed to allocate Wi-Fi event group or UDP socket mutex");
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

    wifi_config_t wifi_config = {};
    memcpy(wifi_config.sta.ssid, CONFIG_DESKSTRATE_WIFI_SSID, ssid_length);
    memcpy(wifi_config.sta.password, CONFIG_DESKSTRATE_WIFI_PASSWORD, password_length);

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "failed to set Wi-Fi station mode");
    ESP_RETURN_ON_ERROR(
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config),
        TAG,
        "failed to configure Wi-Fi station"
    );
    if (xTaskCreate(udp_socket_task, "udp_broadcast", 4096, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "failed to create UDP broadcast task");
        return ESP_ERR_NO_MEM;
    }
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "failed to start Wi-Fi");
    ESP_RETURN_ON_ERROR(start_kvm_client(), TAG, "failed to start kvm_proto client");

    ESP_LOGI(TAG, "Wi-Fi station started");
    return ESP_OK;
}

bool network_take_kvm_state(kvm_state_t &out) {
    if (kvm_mutex == nullptr) {
        return false;
    }
    xSemaphoreTake(kvm_mutex, portMAX_DELAY);
    const bool fresh = state_fresh;
    if (fresh) {
        out = latest_state;
        state_fresh = false;
    }
    xSemaphoreGive(kvm_mutex);
    return fresh;
}

esp_err_t network_send_kvm_request(uint8_t action, uint8_t a, uint8_t b) {
    if (kvm_mutex == nullptr || kvm_socket < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((xEventGroupGetBits(network_events) & WIFI_CONNECTED_BIT) == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    kvm_request_t request = {};
    request.hdr.magic = KVM_MAGIC;
    request.hdr.version = KVM_VERSION;
    request.hdr.type = KVM_REQUEST;
    request.hdr.sender = KVM_NODE_DECK;
    request.action = action;
    request.a = a;
    request.b = b;

    xSemaphoreTake(kvm_mutex, portMAX_DELAY);
    request.hdr.counter = (static_cast<uint64_t>(boot_epoch) << 32) | ++packet_n;
    request.req_id = ++next_req_id;
    sockaddr_in destination = core_addr;
    if (!core_known) {
        destination = {};
        destination.sin_family = AF_INET;
        destination.sin_port = htons(KVM_PORT);
        destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }
    const ssize_t sent = sendto(
        kvm_socket,
        &request,
        sizeof(request),
        0,
        reinterpret_cast<sockaddr *>(&destination),
        sizeof(destination)
    );
    const int send_errno = errno;
    xSemaphoreGive(kvm_mutex);

    if (sent != static_cast<ssize_t>(sizeof(request))) {
        ESP_LOGE(TAG, "kvm_proto request failed: errno %d", send_errno);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t network_broadcast_update(const char *json_message) {
    if (json_message == nullptr || network_events == nullptr || socket_mutex == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t message_length = strnlen(json_message, MAX_UDP_MESSAGE_SIZE + 1);
    if (message_length == 0 || message_length > MAX_UDP_MESSAGE_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }

    if ((xEventGroupGetBits(network_events) & UDP_SOCKET_READY_BIT) == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(socket_mutex, portMAX_DELAY);
    const int socket_fd = broadcast_socket;
    if (socket_fd < 0) {
        xSemaphoreGive(socket_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    sockaddr_in destination = {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(UDP_BROADCAST_PORT);
    destination.sin_addr.s_addr = htonl(INADDR_BROADCAST);

    const ssize_t sent = sendto(
        socket_fd,
        json_message,
        message_length,
        0,
        reinterpret_cast<sockaddr *>(&destination),
        sizeof(destination)
    );
    const int send_errno = errno;
    xSemaphoreGive(socket_mutex);

    if (sent < 0) {
        ESP_LOGE(TAG, "UDP broadcast failed: errno %d", send_errno);
        return ESP_FAIL;
    }
    if (static_cast<size_t>(sent) != message_length) {
        ESP_LOGE(TAG, "UDP broadcast sent %d of %u bytes", static_cast<int>(sent), static_cast<unsigned>(message_length));
        return ESP_FAIL;
    }

    return ESP_OK;
}
