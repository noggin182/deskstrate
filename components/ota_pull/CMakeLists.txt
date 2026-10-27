idf_component_register(
    SRCS "ota_pull.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_app_format esp_http_client esp_https_ota esp_netif lwip mbedtls status_led
)
