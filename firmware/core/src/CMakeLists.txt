idf_component_register(
    SRCS "main.cpp" "monitors.cpp" "hubs.cpp" "kvm_server.cpp" "web.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_http_server ddc hub_swd kvm_proto lwip nvs_flash wifi_sta
)
