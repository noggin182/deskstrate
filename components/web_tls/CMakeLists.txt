# ESP-IDF builds every component it finds, so the deck compiles this too. Only boards that
# turn on the HTTPS server and CSR writing (core, kb: see their sdkconfig.defaults) get
# the code; for the rest it is an empty component.
set(srcs "")
if(CONFIG_ESP_HTTPS_SERVER_ENABLE AND CONFIG_MBEDTLS_X509_CSR_WRITE_C)
    set(srcs "web_tls.cpp")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server esp_https_server mbedtls nvs_flash ota_pull
)

if(srcs)
    # ESP-IDF 6.1's HTTPD_SSL_CONFIG_DEFAULT() leaves use_secure_element out.
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
endif()
