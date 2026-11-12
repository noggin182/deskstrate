idf_component_register(
    SRCS "main.cpp" "monitors.cpp" "hubs.cpp" "kvm_server.cpp" "ota.cpp" "web.cpp" "log_ring.cpp" "wol.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_app_format esp_driver_gpio esp_http_server ddc hub_swd kvm_proto lwip nvs_flash ota_pull status_led web_tls wifi_sta
)
