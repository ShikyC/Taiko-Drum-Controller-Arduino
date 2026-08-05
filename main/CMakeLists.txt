idf_component_register(
    SRCS
        "taiko_controller.c"
        "taiko_hit_led.c"
        "taiko_hit_processor.c"
        "taiko_reports.c"
        "taiko_usb.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        esp_adc
        esp_timer
        driver
        esp_driver_gpio
        esp_driver_rmt
    )
