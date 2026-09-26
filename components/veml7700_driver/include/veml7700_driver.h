#pragma once

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define I2C_SDA_GPIO GPIO_NUM_22
#define I2C_SCL_GPIO GPIO_NUM_23
#define VEML7700_ADDRESS 0x10
#define VEML7700_SPEED 400000
#define MOVE_8_BITS 8
#define LX_PER_COUNT 0.0672
#define TIME_100_MS 100
#define TIME_500_MS 500

esp_err_t veml7700_get_state(float *out);
void lux_task(void *pvParameters);

