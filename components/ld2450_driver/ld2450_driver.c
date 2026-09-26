#include "ld2450_driver.h"
#include "zigbee_app.h"

static const char *TAG = "ld2450_driver";

static struct ld2450_targets_t g_ld2450_state;
static SemaphoreHandle_t g_ld2450_mutex = NULL;
static QueueHandle_t ld2450_filter_req_queue;

static const uint8_t LD2450_CMD_ENABLE_CONFIG[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                    0x04, 0x00,
                                                    0xFF, 0x00, 0x01, 0x00,
                                                    0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_ACK_ENABLE_CONFIG[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                    0x08, 0x00,
                                                    0xFF, 0x01, 0x00, 0x00,
                                                    0x01, 0x00, 0x40, 0x00,
                                                    0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_CMD_END_CONFIG[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                 0x02, 0x00, 0xFE, 0x00,
                                                 0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_ACK_END_CONFIG[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                 0x04, 0x00,
                                                 0xFE, 0x01, 0x00, 0x00,
                                                 0x04, 0x03, 0x02, 0x01 };

//static const uint8_t LD2450_CMD_SINGLE_TARGET[] = { 0xFD, 0xFC, 0xFB, 0xFA,
//                                                    0x02, 0x00, 0x80, 0x00,
//                                                    0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_ACK_SINGLE_TARGET[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                    0x04, 0x00,
                                                    0x80, 0x01, 0x00, 0x00,
                                                    0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_CMD_MULTI_TARGET[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                   0x02, 0x00, 0x90, 0x00,
                                                   0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_ACK_MULTI_TARGET[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                   0x04, 0x00,
                                                   0x90, 0x01, 0x00, 0x00,
                                                   0x04, 0x03, 0x02, 0x01 };

//static const uint8_t LD2450_CMD_SERIAL_BAUD_RATE[] = { 0xFD, 0xFC, 0xFB, 0xFA,
//                                                       0x04, 0x00, 0xA1, 0x00,
//                                                       LD2450_BAUD_RATE_INDEX, 0x00,
//                                                       0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_ACK_SERIAL_BAUD_RATE[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                       0x04, 0x00,
                                                       0xA1, 0x01, 0x00, 0x00,
                                                       0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_CMD_GET_REG_FILTER[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                     0x02, 0x00, 0xC1, 0x00,
                                                     0x04, 0x03, 0x02, 0x01 };

static uint8_t LD2450_CMD_SET_REG_FILTER[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                               0x1C, 0x00, 0xC2, 0x00,
                                               0x02, 0x00,
                                               0x00, 0x00, 0x00, 0x00,
                                               0x00, 0x00, 0x00, 0x00,
                                               0x00, 0x00, 0x00, 0x00,
                                               0x00, 0x00, 0x00, 0x00,
                                               0x00, 0x00, 0x00, 0x00,
                                               0x00, 0x00, 0x00, 0x00,
                                               0x04, 0x03, 0x02, 0x01 };

static const uint8_t LD2450_ACK_SET_REG_FILTER[] = { 0xFD, 0xFC, 0xFB, 0xFA,
                                                     0x04, 0x00,
                                                     0xC2, 0x01, 0x00, 0x00,
                                                     0x04, 0x03, 0x02, 0x01 };

esp_err_t ld2450_get_targets(struct ld2450_targets_t *out){
    if (xSemaphoreTake(g_ld2450_mutex, portMAX_DELAY) == pdTRUE) {
        *out = g_ld2450_state;
        xSemaphoreGive(g_ld2450_mutex);
    }
    else{
        ESP_LOGW(TAG, "Mutex could not be taken");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void ld2450_uart_init(QueueHandle_t *uart1_queue){
    uart_config_t uart1_config = {
        .baud_rate = UART1_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(LD2450_UART_NUM, &uart1_config);
    uart_set_pin(LD2450_UART_NUM, UART1_TX_GPIO, UART1_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(LD2450_UART_NUM, UART1_RX_BUF_SIZE, UART1_TX_BUF_SIZE, UART1_QUEUE_SIZE, uart1_queue, 0);
}

static esp_err_t ld2450_send_command(const uint8_t *cmd, size_t cmd_len, uint32_t timeout_ms)
{
    uart_flush_input(LD2450_UART_NUM);  // discard any stale bytes before sending
    uart_write_bytes(LD2450_UART_NUM, cmd, cmd_len);

    size_t total_read = 0;
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    bool compare_error = false;
    size_t resp_len;
    const uint8_t *cmd_response = NULL;
    ld2450_filter_config_t filter_buffer;

    switch(cmd[6]){
        case CMD_ENABLE_CONFIG:
            cmd_response = LD2450_ACK_ENABLE_CONFIG;
            resp_len = sizeof(LD2450_ACK_ENABLE_CONFIG);
            break;
        case CMD_END_CONFIG:
            cmd_response = LD2450_ACK_END_CONFIG;
            resp_len = sizeof(LD2450_ACK_END_CONFIG);
            break;
        case CMD_SINGLE_TARGET:
            cmd_response = LD2450_ACK_SINGLE_TARGET;
            resp_len = sizeof(LD2450_ACK_SINGLE_TARGET);
            break;
        case CMD_MULTI_TARGET:
            cmd_response = LD2450_ACK_MULTI_TARGET;
            resp_len = sizeof(LD2450_ACK_MULTI_TARGET);
            break;
        case CMD_SERIAL_BAUD_RATE:
            cmd_response = LD2450_ACK_SERIAL_BAUD_RATE;
            resp_len = sizeof(LD2450_ACK_SERIAL_BAUD_RATE);
            break;
        case CMD_GET_REG_FILTER:
            resp_len = 40;
            break;
        case CMD_SET_REG_FILTER:
            cmd_response = LD2450_ACK_SET_REG_FILTER;
            resp_len = sizeof(LD2450_ACK_SET_REG_FILTER);
            break;
        default:
            ESP_LOGW(TAG, "Invalid LD24050 serial command");
            return ESP_ERR_INVALID_ARG;
            break;
    }

    uint8_t resp_buf[resp_len];

    while (total_read < resp_len) {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline) break;
        int n = uart_read_bytes(LD2450_UART_NUM, resp_buf + total_read, resp_len - total_read, deadline - now);
        if (n <= 0) break;
        total_read += n;
    }

    if (total_read != resp_len) {
        ESP_LOGW(TAG, "Command response incomplete: got %d/%d bytes", total_read, resp_len);
        return ESP_ERR_TIMEOUT;
    }

    if(resp_buf[6] != CMD_GET_REG_FILTER && resp_buf[7] == 0x01){
        for(int i = 0; i < resp_len; i++){
            if(resp_buf[i] != cmd_response[i]){
                compare_error = true;
            }
        }
    }
    else if(resp_buf[6] == CMD_GET_REG_FILTER && resp_buf[7] == 0x01) {
        if(resp_buf[0] == 0xFD && resp_buf[1] == 0xFC && resp_buf[2] == 0xFB && resp_buf[3] == 0xFA){
            if(resp_buf[36] == 0x04 && resp_buf[37] == 0x03 && resp_buf[38] == 0x02 && resp_buf[39] == 0x01){
                filter_buffer.filter_type = (int16_t)resp_buf[10];

                filter_buffer.x1_filter_1 = (int16_t)((resp_buf[13] << MOVE_8_BITS) | resp_buf[12]);
                filter_buffer.y1_filter_1 = (int16_t)((resp_buf[15] << MOVE_8_BITS) | resp_buf[14]);
                filter_buffer.x2_filter_1 = (int16_t)((resp_buf[17] << MOVE_8_BITS) | resp_buf[16]);
                filter_buffer.y2_filter_1 = (int16_t)((resp_buf[19] << MOVE_8_BITS) | resp_buf[18]);

                filter_buffer.x1_filter_2 = (int16_t)((resp_buf[21] << MOVE_8_BITS) | resp_buf[20]);
                filter_buffer.y1_filter_2 = (int16_t)((resp_buf[23] << MOVE_8_BITS) | resp_buf[22]);
                filter_buffer.x2_filter_2 = (int16_t)((resp_buf[25] << MOVE_8_BITS) | resp_buf[24]);
                filter_buffer.y2_filter_2 = (int16_t)((resp_buf[27] << MOVE_8_BITS) | resp_buf[26]);

                filter_buffer.x1_filter_3 = (int16_t)((resp_buf[29] << MOVE_8_BITS) | resp_buf[28]);
                filter_buffer.y1_filter_3 = (int16_t)((resp_buf[31] << MOVE_8_BITS) | resp_buf[30]);
                filter_buffer.x2_filter_3 = (int16_t)((resp_buf[33] << MOVE_8_BITS) | resp_buf[32]);
                filter_buffer.y2_filter_3 = (int16_t)((resp_buf[35] << MOVE_8_BITS) | resp_buf[34]);

                if (xSemaphoreTake(g_ld2450_mutex, portMAX_DELAY) == pdTRUE) {
                    g_ld2450_state.filter_type = filter_buffer.filter_type;

                    g_ld2450_state.x1_filter_1 = filter_buffer.x1_filter_1;
                    g_ld2450_state.y1_filter_1 = filter_buffer.y1_filter_1;
                    g_ld2450_state.x2_filter_1 = filter_buffer.x2_filter_1;
                    g_ld2450_state.y2_filter_1 = filter_buffer.y2_filter_1;

                    g_ld2450_state.x1_filter_2 = filter_buffer.x1_filter_2;
                    g_ld2450_state.y1_filter_2 = filter_buffer.y1_filter_2;
                    g_ld2450_state.x2_filter_2 = filter_buffer.x2_filter_2;
                    g_ld2450_state.y2_filter_2 = filter_buffer.y2_filter_2;

                    g_ld2450_state.x1_filter_3 = filter_buffer.x1_filter_3;
                    g_ld2450_state.y1_filter_3 = filter_buffer.y1_filter_3;
                    g_ld2450_state.x2_filter_3 = filter_buffer.x2_filter_3;
                    g_ld2450_state.y2_filter_3 = filter_buffer.y2_filter_3;
                    xSemaphoreGive(g_ld2450_mutex);
                }
                else{
                    ESP_LOGW(TAG, "Mutex could not be taken");
                    return ESP_ERR_TIMEOUT;
                }

            }
            else{
                ESP_LOGW(TAG, "End frame does not match expected for command region filter");
                return ESP_ERR_INVALID_RESPONSE;
            }
        }
        else{
            ESP_LOGW(TAG, "Header does not match expected response for command region filter");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    else{
        ESP_LOGW(TAG, "NACK received. Command could not be transmited");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if(compare_error){
        ESP_LOGW(TAG, "Command respone does not match expected response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "Command %d sent and ACK received", cmd[6]);
    return ESP_OK;
}

static esp_err_t send_with_retry(const uint8_t *cmd, size_t cmd_len, uint32_t timeout_ms, const char *cmd_name)
{
    esp_err_t ret = ESP_FAIL;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        ret = ld2450_send_command(cmd, cmd_len, timeout_ms);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Command %s sent and ACKed (attempt %d/%d)", cmd_name, attempt, MAX_RETRIES);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Could not send command %s. Error: %d (attempt %d/%d)", cmd_name, ret, attempt, MAX_RETRIES);
        vTaskDelay(pdMS_TO_TICKS(TIME_10_MS));
    }

    ESP_LOGE(TAG, "Command %s could not be sent after %d attempts", cmd_name, MAX_RETRIES);
    return ESP_FAIL;
}

static esp_err_t ld2450_sensor_init(void){
    esp_err_t ret;

    if (g_ld2450_mutex == NULL) {
        g_ld2450_mutex = xSemaphoreCreateMutex();
        if (g_ld2450_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "LD2450 mutex created");
    }

    ret = send_with_retry(LD2450_CMD_ENABLE_CONFIG, sizeof(LD2450_CMD_ENABLE_CONFIG), TIME_3000_MS, "ENABLE_CONFIG");
    if (ret != ESP_OK) return ret;

    ret = send_with_retry(LD2450_CMD_MULTI_TARGET, sizeof(LD2450_CMD_MULTI_TARGET), TIME_3000_MS, "MULTI_TARGET");
    if (ret != ESP_OK) return ret;

    ret = send_with_retry(LD2450_CMD_GET_REG_FILTER, sizeof(LD2450_CMD_GET_REG_FILTER), TIME_3000_MS, "GET_REG_FILTER");
    if (ret != ESP_OK) return ret;

    ret = send_with_retry(LD2450_CMD_END_CONFIG, sizeof(LD2450_CMD_END_CONFIG), TIME_3000_MS, "END_CONFIG");
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

static esp_err_t ld2450_set_reg_filter(ld2450_filter_config_t *cmd_temp){
    esp_err_t ret;

    LD2450_CMD_SET_REG_FILTER[8] = (uint8_t)cmd_temp->filter_type;

    LD2450_CMD_SET_REG_FILTER[10] = (uint8_t)(cmd_temp->x1_filter_1 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[11] = (uint8_t)(cmd_temp->x1_filter_1 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[12] = (uint8_t)(cmd_temp->y1_filter_1 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[13] = (uint8_t)(cmd_temp->y1_filter_1 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[14] = (uint8_t)(cmd_temp->x2_filter_1 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[15] = (uint8_t)(cmd_temp->x2_filter_1 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[16] = (uint8_t)(cmd_temp->y2_filter_1 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[17] = (uint8_t)(cmd_temp->y2_filter_1 >> MOVE_8_BITS);

    LD2450_CMD_SET_REG_FILTER[18] = (uint8_t)(cmd_temp->x1_filter_2 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[19] = (uint8_t)(cmd_temp->x1_filter_2 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[20] = (uint8_t)(cmd_temp->y1_filter_2 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[21] = (uint8_t)(cmd_temp->y1_filter_2 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[22] = (uint8_t)(cmd_temp->x2_filter_2 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[23] = (uint8_t)(cmd_temp->x2_filter_2 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[24] = (uint8_t)(cmd_temp->y2_filter_2 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[25] = (uint8_t)(cmd_temp->y2_filter_2 >> MOVE_8_BITS);

    LD2450_CMD_SET_REG_FILTER[26] = (uint8_t)(cmd_temp->x1_filter_3 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[27] = (uint8_t)(cmd_temp->x1_filter_3 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[28] = (uint8_t)(cmd_temp->y1_filter_3 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[29] = (uint8_t)(cmd_temp->y1_filter_3 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[30] = (uint8_t)(cmd_temp->x2_filter_3 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[31] = (uint8_t)(cmd_temp->x2_filter_3 >> MOVE_8_BITS);
    LD2450_CMD_SET_REG_FILTER[32] = (uint8_t)(cmd_temp->y2_filter_3 & REMOVE_MSB_MASK);
    LD2450_CMD_SET_REG_FILTER[33] = (uint8_t)(cmd_temp->y2_filter_3 >> MOVE_8_BITS);

    ret = send_with_retry(LD2450_CMD_ENABLE_CONFIG, sizeof(LD2450_CMD_ENABLE_CONFIG), TIME_3000_MS, "ENABLE_CONFIG");
    if (ret != ESP_OK) return ret;

    ret = send_with_retry(LD2450_CMD_SET_REG_FILTER, sizeof(LD2450_CMD_SET_REG_FILTER), TIME_3000_MS, "SET_REG_FILTER");
    if (ret != ESP_OK) return ret;

    ret = send_with_retry(LD2450_CMD_END_CONFIG, sizeof(LD2450_CMD_END_CONFIG), TIME_3000_MS, "END_CONFIG");
    if (ret != ESP_OK) return ret;

    if (xSemaphoreTake(g_ld2450_mutex, portMAX_DELAY) == pdTRUE) {
        g_ld2450_state.filter_type = cmd_temp->filter_type;

        g_ld2450_state.x1_filter_1 = cmd_temp->x1_filter_1;
        g_ld2450_state.y1_filter_1 = cmd_temp->y1_filter_1;
        g_ld2450_state.x2_filter_1 = cmd_temp->x2_filter_1;
        g_ld2450_state.y2_filter_1 = cmd_temp->y2_filter_1;

        g_ld2450_state.x1_filter_2 = cmd_temp->x1_filter_2;
        g_ld2450_state.y1_filter_2 = cmd_temp->y1_filter_2;
        g_ld2450_state.x2_filter_2 = cmd_temp->x2_filter_2;
        g_ld2450_state.y2_filter_2 = cmd_temp->y2_filter_2;

        g_ld2450_state.x1_filter_3 = cmd_temp->x1_filter_3;
        g_ld2450_state.y1_filter_3 = cmd_temp->y1_filter_3;
        g_ld2450_state.x2_filter_3 = cmd_temp->x2_filter_3;
        g_ld2450_state.y2_filter_3 = cmd_temp->y2_filter_3;
        xSemaphoreGive(g_ld2450_mutex);
    }
    else{
        ESP_LOGW(TAG, "Mutex could not be taken");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static esp_err_t ld2450_read_data(void)
{
    int total_read = 0;
    int header_index = -1;
    int missing_end_bytes = 0;
    const size_t resp_len = LD2450_DATA_LENGTH;
    uint8_t resp_buf[resp_len];
    bool occupancy_1, occupancy_2, occupancy_3;
    int16_t x_distance_1, y_distance_1, speed_1, resolution_1;
    int16_t x_distance_2, y_distance_2, speed_2, resolution_2;
    int16_t x_distance_3, y_distance_3, speed_3, resolution_3;

    uint32_t timeout = pdMS_TO_TICKS(TIME_100_MS);

    total_read = uart_read_bytes(LD2450_UART_NUM, resp_buf, resp_len, timeout);

    if(total_read < 0){
        ESP_LOGW(TAG, "uart returned error: %d", total_read);
        return ESP_ERR_TIMEOUT;
    }

    for(int i = 0; i <= total_read - 4; i++){
        if(resp_buf[i] == 0xAA && resp_buf[i+1] == 0xFF && resp_buf[i+2] == 0x03 && resp_buf[i+3] == 0x00){
            header_index = i;
            break;
        }
    }

    if(header_index < 0){
        ESP_LOGW(TAG, "No header found in the buffer. Discarding!");
        return ESP_ERR_INVALID_RESPONSE;
    }

    missing_end_bytes = resp_len - total_read;

    if(header_index > 0 || missing_end_bytes > 0){
        int bytes_kept = total_read - header_index; // How many good bytes we already have
        int bytes_missing = header_index + missing_end_bytes; // How many bytes are missing

        if(header_index > 0){
            memmove(resp_buf, resp_buf + header_index, bytes_kept); // Shift good bytes to the front of the buffer
        }

        int n = uart_read_bytes(LD2450_UART_NUM, resp_buf + bytes_kept, bytes_missing, timeout);

        if(n < 0){
            ESP_LOGW(TAG, "uart returned error: %d", n);
            return ESP_ERR_TIMEOUT;
        }

        if(n != bytes_missing){
            ESP_LOGW(TAG, "Resync read incomplete: got %d/%d bytes", n, bytes_missing);
            return ESP_ERR_TIMEOUT;
        }
    }

    if(resp_buf[0] == 0xAA && resp_buf[1] == 0xFF && resp_buf[2] == 0x03 && resp_buf[3] == 0x00){
        if(resp_buf[28] == 0x55 && resp_buf[29] == 0xCC){
            x_distance_1 = resp_buf[4] + ((resp_buf[5] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[5] & SIGN_MASK) == 0) x_distance_1 *= -1;

            y_distance_1 = resp_buf[6] + ((resp_buf[7] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[7] & SIGN_MASK) == 0) y_distance_1 *= -1;

            speed_1 = resp_buf[8] + ((resp_buf[9] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[9] & SIGN_MASK) == 0) speed_1 *= -1;

            resolution_1 = resp_buf[10] + (resp_buf[11] << MOVE_8_BITS);

            x_distance_2 = resp_buf[12] + ((resp_buf[13] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[13] & SIGN_MASK) == 0) x_distance_2 *= -1;

            y_distance_2 = resp_buf[14] + ((resp_buf[15] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[15] & SIGN_MASK) == 0) y_distance_2 *= -1;

            speed_2 = resp_buf[16] + ((resp_buf[17] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[17] & SIGN_MASK) == 0) speed_2 *= -1;

            resolution_2 = resp_buf[18] + (resp_buf[19] << MOVE_8_BITS);

            x_distance_3 = resp_buf[20] + ((resp_buf[21] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[21] & SIGN_MASK) == 0) x_distance_3 *= -1;

            y_distance_3 = resp_buf[22] + ((resp_buf[23] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[23] & SIGN_MASK) == 0) y_distance_3 *= -1;

            speed_3 = resp_buf[24] + ((resp_buf[25] & REMOVE_SIGN_MASK) << MOVE_8_BITS);
            if((resp_buf[25] & SIGN_MASK) == 0) speed_3 *= -1;

            resolution_3 = resp_buf[26] + (resp_buf[27] << MOVE_8_BITS);
        }
        else{
            ESP_LOGW(TAG, "End Frame does not match 0x55CC");
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    else {
        ESP_LOGW(TAG, "Data Header does not match 0xAAFF0300");
        return ESP_ERR_INVALID_RESPONSE;
    }

    occupancy_1 = (x_distance_1 != 0 || y_distance_1 != 0) ? true : false;
    occupancy_2 = (x_distance_2 != 0 || y_distance_2 != 0) ? true : false;
    occupancy_3 = (x_distance_3 != 0 || y_distance_3 != 0) ? true : false;

    if (xSemaphoreTake(g_ld2450_mutex, portMAX_DELAY) == pdTRUE) {
        g_ld2450_state.x_distance_1 = x_distance_1;
        g_ld2450_state.y_distance_1 = y_distance_1;
        g_ld2450_state.speed_1 = speed_1;
        g_ld2450_state.resolution_1 = resolution_1;

        g_ld2450_state.x_distance_2 = x_distance_2;
        g_ld2450_state.y_distance_2 = y_distance_2;
        g_ld2450_state.speed_2 = speed_2;
        g_ld2450_state.resolution_2 = resolution_2;

        g_ld2450_state.x_distance_3 = x_distance_3;
        g_ld2450_state.y_distance_3 = y_distance_3;
        g_ld2450_state.speed_3 = speed_3;
        g_ld2450_state.resolution_3 = resolution_3;

        g_ld2450_state.occupancy_state[0] = occupancy_1;
        g_ld2450_state.occupancy_state[1] = occupancy_2;
        g_ld2450_state.occupancy_state[2] = occupancy_3;

        xSemaphoreGive(g_ld2450_mutex);
    }

    //ESP_LOGI(TAG, "LD2450 data received");
    return ESP_OK;
}

void ld2450_task(void *pvParameters)
{
    uart_event_t event;
    QueueHandle_t uart1_queue;
    ld2450_filter_config_t pending_cfg;

    ld2450_uart_init(&uart1_queue);

    if (ld2450_filter_req_queue == NULL) {
        ld2450_filter_req_queue = xQueueCreate(1, sizeof(ld2450_filter_config_t));
        if (ld2450_filter_req_queue == NULL) {
            ESP_LOGE(TAG, "Failed to create filter request queue");
            vTaskDelete(NULL);
        }
    }

    esp_err_t ret = ld2450_sensor_init();
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Sensor Init Failed, aborting task!");
        vTaskDelete(NULL);
    }

    while(1){
        if(xQueueReceive(uart1_queue, &event, pdMS_TO_TICKS(TIME_200_MS))){
            switch(event.type){
                case UART_DATA:
                    ret = ld2450_read_data();
                    // Do not attepmt sending data to zigbee until joined to a network
                    if(ret == ESP_OK && joined_to_zigbee == true){
                        for(int i = 0; i < OCCUPANCY_ENDPOINT_COUNT; i++){
                            zigbee_app_report_occupancy(i, g_ld2450_state.occupancy_state[i]);
                        }
                    }
                    if(ret == ESP_OK && xSemaphoreTake(g_ld2450_mutex, portMAX_DELAY) == pdTRUE){
                        printf("$LD2450,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                            g_ld2450_state.x_distance_1,
                            g_ld2450_state.y_distance_1,
                            g_ld2450_state.resolution_1,
                            g_ld2450_state.speed_1,
                            g_ld2450_state.x1_filter_1,
                            g_ld2450_state.y1_filter_1,
                            g_ld2450_state.x2_filter_1,
                            g_ld2450_state.y2_filter_1,
                            g_ld2450_state.x_distance_2,
                            g_ld2450_state.y_distance_2,
                            g_ld2450_state.resolution_2,
                            g_ld2450_state.speed_2,
                            g_ld2450_state.x1_filter_2,
                            g_ld2450_state.y1_filter_2,
                            g_ld2450_state.x2_filter_2,
                            g_ld2450_state.y2_filter_2,
                            g_ld2450_state.x_distance_3,
                            g_ld2450_state.y_distance_3,
                            g_ld2450_state.resolution_3,
                            g_ld2450_state.speed_3,
                            g_ld2450_state.x1_filter_3,
                            g_ld2450_state.y1_filter_3,
                            g_ld2450_state.x2_filter_3,
                            g_ld2450_state.y2_filter_3
                        );
                        xSemaphoreGive(g_ld2450_mutex);
                    }
                    else{
                        ESP_LOGI(TAG,"LD2450 data could not be read from UART1, waiting for next frame");
                    }
                    break;

                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                case UART_FRAME_ERR:
                case UART_PARITY_ERR:
                    ESP_LOGW(TAG, "UART event %d, flushing and resyncing", event.type);
                    uart_flush_input(LD2450_UART_NUM);
                    xQueueReset(uart1_queue);
                    break;

                default:
                    break;
            }
        }

        if(xQueueReceive(ld2450_filter_req_queue, &pending_cfg, 0) == pdTRUE){
            ld2450_set_reg_filter(&pending_cfg);
        }
    }
}

BaseType_t ld2450_request_filter_update(const ld2450_filter_config_t *cfg) {
    BaseType_t ret= xQueueSend(ld2450_filter_req_queue, cfg, pdMS_TO_TICKS(TIME_100_MS));
    return ret;
}
