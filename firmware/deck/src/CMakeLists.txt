idf_component_register(
    SRCS "controls.cpp" "icons.c" "main.cpp" "menu.cpp" "neokeys.cpp" "network.cpp"
    INCLUDE_DIRS "."
    # wifi_sta: the DESKSTRATE_WIFI_* menuconfig entries and wifi_sta_apply_credentials().
    REQUIRES esp_app_format esp_event esp_netif esp_wifi kvm_proto lwip nvs_flash ota_pull status_led wifi_sta
)
