idf_component_register(
    SRCS "taiko_controller.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_adc esp_timer driver esp_driver_gpio
    )
