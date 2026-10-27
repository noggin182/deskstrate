idf_component_register(
    SRCS "main.cpp" "network.cpp"
    INCLUDE_DIRS "."
    # wifi_sta: the DESKSTRATE_WIFI_* menuconfig entries and wifi_sta_apply_credentials().
    REQUIRES esp_event esp_netif esp_wifi kvm_proto lwip nvs_flash ota_pull status_led wifi_sta
)
