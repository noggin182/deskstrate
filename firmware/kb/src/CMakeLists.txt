idf_component_register(
    SRCS "main.cpp" "kb_link.cpp" "sleep.cpp" "web.cpp"
    INCLUDE_DIRS "."
    REQUIRES app_update esp_app_format esp_driver_gpio esp_driver_uart esp_http_server esp_hw_support esp_timer esp_wifi ota_pull status_led web_tls wifi_sta
)
