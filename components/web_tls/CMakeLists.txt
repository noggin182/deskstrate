idf_component_register(
    SRCS "web_tls.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_https_server mbedtls nvs_flash ota_pull
)

# ESP-IDF 6.1's HTTPD_SSL_CONFIG_DEFAULT() leaves use_secure_element out.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
