idf_component_register(
    SRCS "continuous_read_main.c"
    PRIV_REQUIRES esp_adc esp_timer driver heap
    INCLUDE_DIRS "."
)
