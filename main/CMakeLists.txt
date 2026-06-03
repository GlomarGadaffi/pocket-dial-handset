# IDF v6.0 split the `driver` umbrella into esp_driver_* components; v5.x still
# ships the combined `driver`. Mirror pocket-dial's version-conditional pattern.
if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    set(DRIVER_REQUIRES esp_driver_gpio esp_driver_i2s)
else()
    set(DRIVER_REQUIRES driver)
endif()

idf_component_register(
    SRCS
        "app_main.cpp"
        "sip_uac.cpp"
        "audio_io.c"
        "g711.c"
        "net_wifi.c"
    INCLUDE_DIRS "."
    REQUIRES
        ${DRIVER_REQUIRES}   # i2s_std + gpio
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        esp_timer
        esp_hw_support       # esp_random()
        sip_core
)
