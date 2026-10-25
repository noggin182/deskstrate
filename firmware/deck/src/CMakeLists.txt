idf_component_register(
    SRCS "main.cpp" "network.cpp"
    INCLUDE_DIRS "."
    # wifi_sta provides the DESKSTRATE_WIFI_* menuconfig entries used by network.cpp.
    REQUIRES esp_event esp_netif esp_wifi kvm_proto lwip nvs_flash wifi_sta
)
