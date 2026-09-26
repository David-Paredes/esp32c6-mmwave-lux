#pragma once

#include "driver/uart.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define UART1_RX_GPIO GPIO_NUM_1
#define UART1_TX_GPIO GPIO_NUM_0
#define LD2450_UART_NUM UART_NUM_1
#define UART1_BUF_SIZE 128
#define UART1_RX_BUF_SIZE UART1_BUF_SIZE * 2
#define UART1_TX_BUF_SIZE 0
#define UART1_BAUD_RATE 256000
#define UART1_QUEUE_SIZE 10
#define CMD_ENABLE_CONFIG 0xFF
#define CMD_END_CONFIG 0xFE
#define CMD_SINGLE_TARGET 0x80
#define CMD_MULTI_TARGET 0x90
#define CMD_SERIAL_BAUD_RATE 0xA1
#define CMD_GET_REG_FILTER 0xC1
#define CMD_SET_REG_FILTER 0xC2
#define REMOVE_SIGN_MASK 0x7F
#define SIGN_MASK 0x80
#define REMOVE_MSB_MASK 0x00FF
#define MOVE_8_BITS 8
#define TIME_3000_MS 3000
#define TIME_200_MS 200
#define TIME_100_MS 100
#define TIME_10_MS 10
#define LD2450_DATA_LENGTH 30
#define MAX_RETRIES 5

#if   UART1_BAUD_RATE == 9600
    #define LD2450_BAUD_RATE_INDEX 0x01
#elif UART1_BAUD_RATE == 19200
    #define LD2450_BAUD_RATE_INDEX 0x02
#elif UART1_BAUD_RATE == 38400
    #define LD2450_BAUD_RATE_INDEX 0x03
#elif UART1_BAUD_RATE == 57600
    #define LD2450_BAUD_RATE_INDEX 0x04
#elif UART1_BAUD_RATE == 115200
    #define LD2450_BAUD_RATE_INDEX 0x05
#elif UART1_BAUD_RATE == 230400
    #define LD2450_BAUD_RATE_INDEX 0x06
#elif UART1_BAUD_RATE == 256000
    #define LD2450_BAUD_RATE_INDEX 0x07
#elif UART1_BAUD_RATE == 460800
    #define LD2450_BAUD_RATE_INDEX 0x08
#else
    #error "UART1_BAUD_RATE is not supported in LD2450"
#endif

struct ld2450_targets_t{
    int16_t x_distance_1;
    int16_t y_distance_1;
    int16_t speed_1;
    int16_t resolution_1;
    int16_t x_distance_2;
    int16_t y_distance_2;
    int16_t speed_2;
    int16_t resolution_2;
    int16_t x_distance_3;
    int16_t y_distance_3;
    int16_t speed_3;
    int16_t resolution_3;
    int16_t filter_type;
    int16_t x1_filter_1;
    int16_t y1_filter_1;
    int16_t x2_filter_1;
    int16_t y2_filter_1;
    int16_t x1_filter_2;
    int16_t y1_filter_2;
    int16_t x2_filter_2;
    int16_t y2_filter_2;
    int16_t x1_filter_3;
    int16_t y1_filter_3;
    int16_t x2_filter_3;
    int16_t y2_filter_3;
    bool occupancy_state[3];
};

typedef struct {
    int16_t filter_type;
    int16_t x1_filter_1, y1_filter_1, x2_filter_1, y2_filter_1;
    int16_t x1_filter_2, y1_filter_2, x2_filter_2, y2_filter_2;
    int16_t x1_filter_3, y1_filter_3, x2_filter_3, y2_filter_3;
} ld2450_filter_config_t;

esp_err_t ld2450_get_targets(struct ld2450_targets_t *out);
void ld2450_task(void *pvParameters);
BaseType_t ld2450_request_filter_update(const ld2450_filter_config_t *cfg);
