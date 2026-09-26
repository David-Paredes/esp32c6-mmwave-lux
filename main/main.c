#include "esp_err.h"
#include "nvs_flash.h"
#include "zigbee_app.h"
#include "ld2450_driver.h"
#include "veml7700_driver.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret;
    esp_zb_platform_config_t config = {
        .radio_config = { .radio_mode = ZB_RADIO_MODE_NATIVE },
        .host_config = { .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE },
    };

    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase (err %s), erasing and retrying", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = esp_zb_platform_config(&config);
    if(ret != ESP_OK){
        ESP_LOGW(TAG, "zigbee platform could not be configured");
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
    xTaskCreate(zigbee_recommission_button_task, "recommission_btn", 2048, NULL, 3, NULL);
    xTaskCreate(ld2450_task, "LD2450_main", 4096, NULL, 4, NULL);
    xTaskCreate(lux_task, "Lux_main", 4096, NULL, 2, NULL);
}
