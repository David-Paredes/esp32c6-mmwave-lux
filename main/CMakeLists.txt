idf_component_register(
	SRCS "main.c"
	INCLUDE_DIRS "."
	REQUIRES 
		zigbee_app
		nvs_flash
		driver
        ld2450_driver
        veml7700_driver
)
