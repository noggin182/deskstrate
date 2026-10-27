idf_component_register(
    SRCS "status_led.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_netif
)
