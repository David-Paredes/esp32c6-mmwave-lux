idf_component_register(
    SRCS "ld2450_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_uart esp_driver_gpio zigbee_app
)
