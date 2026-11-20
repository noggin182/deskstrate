idf_component_register(
    SRCS "controls.cpp" "font_mono_22.c" "icons.c" "main.cpp" "menu.cpp" "mirror.cpp" "neokeys.cpp" "network.cpp" "web.cpp"
    INCLUDE_DIRS "."
    # wifi_sta: the DESKSTRATE_WIFI_* menuconfig entries and wifi_sta_apply_credentials().
    REQUIRES esp_app_format esp_driver_ledc esp_event esp_http_server esp_netif esp_timer esp_wifi kvm_controls kvm_proto lwip nvs_flash ota_pull status_led web_tls wifi_sta
)
