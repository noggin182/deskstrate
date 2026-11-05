idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_http_server esp_timer esp_wifi wifi_sta
)
