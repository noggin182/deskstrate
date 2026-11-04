idf_component_register(
    SRCS "web_tls.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_https_server mbedtls nvs_flash ota_pull
)
