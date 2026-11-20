idf_component_register(
    SRCS "main.cpp" "serial.cpp" "udp.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_driver_gpio esp_driver_usb_serial_jtag esp_netif esp_wifi kvm_proto lwip ota_pull status_led wifi_sta
)
