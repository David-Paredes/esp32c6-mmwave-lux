idf_component_register(
    SRCS "veml7700_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c esp_driver_gpio zigbee_app
)
