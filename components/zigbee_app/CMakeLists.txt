idf_component_register(
	SRCS "zigbee_app.c"
	INCLUDE_DIRS "include"
	REQUIRES espressif__esp-zigbee-lib esp_timer driver esp_driver_gpio ld2450_driver
)
