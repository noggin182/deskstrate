idf_component_register(
    SRCS "main.cpp" "ddc_bringup.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio ddc kvm_proto wifi_sta
)
