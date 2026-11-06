idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp_wifi esp_netif esp_event nvs_flash
)

# Plain ESP-IDF builds (CI) choose the variant with -DRADIO_TEST_SENDER=0|1 and
# -DRADIO_TEST_LR=0|1; PlatformIO passes the same defines in build_flags instead.
foreach(opt RADIO_TEST_SENDER RADIO_TEST_LR RADIO_TEST_CHANNEL)
    if(DEFINED ${opt})
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${opt}=${${opt}})
    endif()
endforeach()
