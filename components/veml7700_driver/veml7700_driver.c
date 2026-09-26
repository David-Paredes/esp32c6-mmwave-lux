#include "veml7700_driver.h"
#include "zigbee_app.h"

static const char *TAG = "veml7700_driver";
// ALS_CONF_0 = 0x0000: gain x1, IT=100ms, powered on, int disabled
static const uint8_t VEML7700_I2C_CMD_CFG[3] = { 0x00, 0x00, 0x00 };
static const uint8_t VEML7700_I2C_CMD_READ = 0x04;

static float g_lux_state;
static SemaphoreHandle_t g_lux_mutex = NULL;

static esp_err_t veml7700_i2c_init(i2c_master_dev_handle_t *dev_handle)
{
    esp_err_t ret;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };

    i2c_master_bus_handle_t bus_handle;

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = VEML7700_ADDRESS,
        .scl_speed_hz = VEML7700_SPEED,
    };

    ret = i2c_new_master_bus(&bus_config, &bus_handle);
    if(ret != ESP_OK){
        ESP_LOGW(TAG, "Master Bus could not be created. Error: %d", ret);
        return ret;
    }

    ret = i2c_master_probe(bus_handle, VEML7700_ADDRESS, TIME_100_MS);
    if(ret != ESP_OK){
        ESP_LOGW(TAG, "Device %d not connected. Error: %d", VEML7700_ADDRESS, ret);
        return ret;
    }

    ret = i2c_master_bus_add_device(bus_handle, &dev_config, dev_handle);
    if(ret != ESP_OK){
        ESP_LOGW(TAG, "Device %d could not be added. Error: %d", VEML7700_ADDRESS, ret);
        return ret;
    }

    ESP_LOGI(TAG, "I2C master bus initiated. Device %d added.", VEML7700_ADDRESS);
    return ESP_OK;
}

static esp_err_t veml7700_sensor_init(i2c_master_dev_handle_t *dev_handle)
{
    esp_err_t ret;

    g_lux_mutex = xSemaphoreCreateMutex();
    if (g_lux_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "VEML7700 mutex created");

    ret = i2c_master_transmit(*dev_handle, VEML7700_I2C_CMD_CFG, sizeof(VEML7700_I2C_CMD_CFG), -1);
    if(ret != ESP_OK){
        ESP_LOGW(TAG, "I2C command could not be written. Error: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "VEML7700 light sensor initialized.");
    return ESP_OK;
}

static esp_err_t veml7700_read_data(i2c_master_dev_handle_t *dev_handle)
{
    esp_err_t ret;
    float lux_value = 0.0;
    uint8_t read_buffer[2];

    ret = i2c_master_transmit_receive(*dev_handle, &VEML7700_I2C_CMD_READ, sizeof(VEML7700_I2C_CMD_READ),
                                     read_buffer, sizeof(read_buffer), -1);

    if(ret != ESP_OK){
        ESP_LOGW(TAG, "I2C data could not be read. Error: %d", ret);
        return ret;
    }

    lux_value = (read_buffer[0] + (read_buffer[1] << MOVE_8_BITS)) * LX_PER_COUNT;

    if (xSemaphoreTake(g_lux_mutex, portMAX_DELAY) == pdTRUE) {
        g_lux_state = lux_value;
        xSemaphoreGive(g_lux_mutex);
    }
    else{
        ESP_LOGW(TAG, "Mutex could not be taken.");
        return ESP_ERR_TIMEOUT;
    }

    //ESP_LOGI(TAG, "VEML7700 lux data read.");
    return ESP_OK;
}

esp_err_t veml7700_get_state(float *out){
    if (xSemaphoreTake(g_lux_mutex, portMAX_DELAY) == pdTRUE) {
        *out = g_lux_state;
        xSemaphoreGive(g_lux_mutex);
    }
    else{
        ESP_LOGW(TAG, "Mutex could not be taken");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void lux_task(void *pvParameters)
{
    esp_err_t ret;
    i2c_master_dev_handle_t i2c_dev_handle;
    TickType_t lux_task_delay = pdMS_TO_TICKS(TIME_500_MS);

    ret = veml7700_i2c_init(&i2c_dev_handle);
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "I2C could not be initilized. Aborting task!");
        vTaskDelete(NULL);
    }

    ret = veml7700_sensor_init(&i2c_dev_handle);
    if(ret != ESP_OK){
        ESP_LOGE(TAG, "Sensor Init Failed, Aborting task!");
        vTaskDelete(NULL);
    }

    while(1){
        ret = veml7700_read_data(&i2c_dev_handle);
        if(ret != ESP_OK){
            ESP_LOGW(TAG, "I2C data could not be read. Error: %d", ret);
        }
        else{
            if(joined_to_zigbee == true){
                float lux_value;
                veml7700_get_state(&lux_value);
                zigbee_app_report_lux(lux_value);
            }
            else{
                ESP_LOGI(TAG, "Waiting to join a zigbee network");
            }
        }
        vTaskDelay(lux_task_delay);
    }
}
