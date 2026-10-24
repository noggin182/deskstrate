idf_component_register(
    SRCS "main.cpp" "monitors.cpp" "web.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_http_server ddc kvm_proto wifi_sta
)
