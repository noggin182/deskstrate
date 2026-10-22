idf_component_register(
    SRCS "main.cpp" "network.cpp" "ddc_bringup.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_event esp_netif esp_wifi lwip nvs_flash board ddc
)
