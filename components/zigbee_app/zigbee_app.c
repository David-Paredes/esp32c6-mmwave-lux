#include "zigbee_app.h"
#include "esp_zigbee_attribute.h"
#include "zcl/esp_zigbee_zcl_basic.h"
#include "ld2450_driver.h"
#include <math.h>

static const char *TAG = "ZigBee";

volatile bool joined_to_zigbee = false;
static uint8_t occupancy_state[OCCUPANCY_ENDPOINT_COUNT] = {0, 0, 0};
static uint16_t lux_measured_value = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_DEFAULT;

// LD2450 control cluster state. target_coords is the ZCL-attribute storage
// for the 6 output x/y attributes (mm). control_state doubles as BOTH the
// ZCL storage for the 13 input attributes (filter_type + 12 region corners)
// AND the struct handed straight to ld2450_request_filter_update() -- its
// field order matches ld2450_filter_config_t exactly (see the attribute
// wiring in esp_zb_task() below), so no copy is needed between the two.
static int16_t target_coords[6] = {0, 0, 0, 0, 0, 0};
static ld2450_filter_config_t control_state = {0};

static void bdb_commissioning_alarm_cb(uint8_t mode)
{
    esp_zb_bdb_start_top_level_commissioning(mode);
}

static void forward_filter_update_cb(uint8_t param)
{
    (void)param;
    if (ld2450_request_filter_update(&control_state) != pdTRUE) {
        ESP_LOGW(TAG, "LD2450 filter queue busy, region update dropped (will retry on next HA write)");
    }
}

// Handles HA writes to the LD2450 control cluster's input attributes
// (filter_type + the 12 region corners). Their ZCL storage IS control_state
// (see the attribute wiring in esp_zb_task()), so by the time we're called
// the new value is already in place -- we just need to forward it to the
// driver. HA/zigpy normally writes all 13 attributes in one ZCL Write
// Attributes command, so debounce with a short scheduler alarm instead of
// calling ld2450_request_filter_update() once per attribute (each call can
// block this task for up to 100 ms if the driver hasn't drained the queue
// yet -- see ld2450_request_filter_update() in ld2450_driver.c).
static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    switch (callback_id) {
        case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID: {
            const esp_zb_zcl_set_attr_value_message_t *msg = (const esp_zb_zcl_set_attr_value_message_t *)message;
            if (msg->info.status == ESP_ZB_ZCL_STATUS_SUCCESS &&
                msg->info.dst_endpoint == CONTROL_ENDPOINT &&
                msg->info.cluster == LD2450_CONTROL_CLUSTER_ID &&
                msg->attribute.id >= ATTR_FILTER_TYPE && msg->attribute.id <= ATTR_REGION3_Y2) {
                esp_zb_scheduler_alarm_cancel(forward_filter_update_cb, 0);
                esp_zb_scheduler_alarm(forward_filter_update_cb, 0, FILTER_WRITE_DEBOUNCE_MS);
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,
        .install_code_policy = false,
        .nwk_cfg.zed_cfg = {
            .ed_timeout = ESP_ZB_ED_AGING_TIMEOUT_64MIN,
            .keep_alive = 3000,
        },
    };
    esp_zb_init(&zb_nwk_cfg);
    esp_zb_core_action_handler_register(zb_action_handler);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();

    for(int i = 1; i <= OCCUPANCY_ENDPOINT_COUNT; i++){
        esp_zb_occupancy_sensing_cluster_cfg_t occupancy_cfg = {
            .occupancy = 0,
            .sensor_type = ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_PIR,
            .sensor_type_bitmap = 1 << ESP_ZB_ZCL_OCCUPANCY_SENSING_OCCUPANCY_SENSOR_TYPE_PIR,
        };

        esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
        esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
        esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, "\x08""fortytwo");
        esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, "\x06mmWave");
        esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_attribute_list_t *identify_cluster = esp_zb_identify_cluster_create(NULL);
        esp_zb_cluster_list_add_identify_cluster(cluster_list, identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_attribute_list_t *occupancy_cluster = esp_zb_occupancy_sensing_cluster_create(&occupancy_cfg);
        esp_zb_cluster_list_add_occupancy_sensing_cluster(cluster_list,occupancy_cluster,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_endpoint_config_t ep_cfg = {
            .endpoint = i,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = HA_ESP_OCCUPANCY_ID, // ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID if it does not work
            .app_device_version = 0,
        };
        esp_zb_ep_list_add_ep(ep_list, cluster_list, ep_cfg);
    }

    {
        esp_zb_illuminance_meas_cluster_cfg_t lux_cfg = {
            .measured_value = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_DEFAULT,
            .min_value = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MIN_MEASURED_VALUE_MIN_VALUE,
            .max_value = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MAX_MEASURED_VALUE_MAX_VALUE,
        };

        esp_zb_cluster_list_t *lux_cluster_list = esp_zb_zcl_cluster_list_create();
        esp_zb_attribute_list_t *lux_basic_cluster = esp_zb_basic_cluster_create(NULL);
        esp_zb_basic_cluster_add_attr(lux_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, "\x08""fortytwo");
        esp_zb_basic_cluster_add_attr(lux_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, "\x09luxSensor");
        esp_zb_cluster_list_add_basic_cluster(lux_cluster_list, lux_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_attribute_list_t *lux_identify_cluster = esp_zb_identify_cluster_create(NULL);
        esp_zb_cluster_list_add_identify_cluster(lux_cluster_list, lux_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_attribute_list_t *illuminance_cluster = esp_zb_illuminance_meas_cluster_create(&lux_cfg);
        esp_zb_cluster_list_add_illuminance_meas_cluster(lux_cluster_list, illuminance_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_endpoint_config_t lux_ep_cfg = {
            .endpoint = LUX_ENDPOINT,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_LIGHT_SENSOR_DEVICE_ID,
            .app_device_version = 0,
        };
        esp_zb_ep_list_add_ep(ep_list, lux_cluster_list, lux_ep_cfg);
    }

    {
        esp_zb_cluster_list_t *ctrl_cluster_list = esp_zb_zcl_cluster_list_create();
        esp_zb_attribute_list_t *ctrl_basic_cluster = esp_zb_basic_cluster_create(NULL);
        esp_zb_basic_cluster_add_attr(ctrl_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, "\x08""fortytwo");
        esp_zb_basic_cluster_add_attr(ctrl_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, "\x0Cld2450ctrl");
        esp_zb_cluster_list_add_basic_cluster(ctrl_cluster_list, ctrl_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_attribute_list_t *ctrl_identify_cluster = esp_zb_identify_cluster_create(NULL);
        esp_zb_cluster_list_add_identify_cluster(ctrl_cluster_list, ctrl_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_attribute_list_t *ctrl_attrs = esp_zb_zcl_attr_list_create(LD2450_CONTROL_CLUSTER_ID);

        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_TARGET_X_1, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &target_coords[0]);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_TARGET_Y_1, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &target_coords[1]);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_TARGET_X_2, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &target_coords[2]);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_TARGET_Y_2, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &target_coords[3]);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_TARGET_X_3, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &target_coords[4]);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_TARGET_Y_3, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_ONLY | ESP_ZB_ZCL_ATTR_ACCESS_REPORTING, &target_coords[5]);

        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_FILTER_TYPE, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.filter_type);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION1_X1, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.x1_filter_1);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION1_Y1, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.y1_filter_1);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION1_X2, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.x2_filter_1);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION1_Y2, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.y2_filter_1);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION2_X1, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.x1_filter_2);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION2_Y1, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.y1_filter_2);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION2_X2, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.x2_filter_2);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION2_Y2, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.y2_filter_2);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION3_X1, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.x1_filter_3);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION3_Y1, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.y1_filter_3);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION3_X2, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.x2_filter_3);
        esp_zb_custom_cluster_add_custom_attr(ctrl_attrs, ATTR_REGION3_Y2, ESP_ZB_ZCL_ATTR_TYPE_S16,
            ESP_ZB_ZCL_ATTR_ACCESS_READ_WRITE, &control_state.y2_filter_3);

        esp_zb_cluster_list_add_custom_cluster(ctrl_cluster_list, ctrl_attrs, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

        esp_zb_endpoint_config_t ctrl_ep_cfg = {
            .endpoint = CONTROL_ENDPOINT,
            .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
            .app_device_id = ESP_ZB_HA_SIMPLE_SENSOR_DEVICE_ID,
            .app_device_version = 0,
        };
        esp_zb_ep_list_add_ep(ep_list, ctrl_cluster_list, ctrl_ep_cfg);
    }

    esp_zb_device_register(ep_list);

    // Reporting config must be created AFTER the endpoints are registered
    // (the ZCL attribute database entries have to exist first). It doesn't
    // need the network to be up yet, this is local config only.
    for(int i = 1; i <= OCCUPANCY_ENDPOINT_COUNT; i++){
        esp_zb_zcl_reporting_info_t rep_info = {
            .direction = ESP_ZB_ZCL_REPORT_DIRECTION_SEND,
            .ep = i,
            .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
            .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            .attr_id = ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
            .u.send_info = {
                .min_interval = OCCUPANCY_REPORT_MIN_INTERVAL_S,
                .max_interval = OCCUPANCY_REPORT_MAX_INTERVAL_S,
                .def_min_interval = OCCUPANCY_REPORT_MIN_INTERVAL_S,
                .def_max_interval = OCCUPANCY_REPORT_MAX_INTERVAL_S,
            },
            .manuf_code = 0,
        };
        rep_info.u.send_info.delta.u8 = 1;

        esp_err_t rep_ret = esp_zb_zcl_update_reporting_info(&rep_info);
        if (rep_ret != ESP_OK) {
            ESP_LOGW(TAG, "Endpoint %d: could not set up occupancy reporting info (err %d)", i, rep_ret);
            continue;
        }

        esp_zb_zcl_attr_location_info_t loc_info = {
            .endpoint_id = i, // 1, 2 or 3
            .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
            .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            .attr_id = ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
        };
        esp_err_t start_ret = esp_zb_zcl_start_attr_reporting(loc_info);
        if (start_ret != ESP_OK) {
            ESP_LOGW(TAG, "Endpoint %d: could not enable occupancy reporting (err %d)", i, start_ret);
        }
    }

    {
        esp_zb_zcl_reporting_info_t lux_rep_info = {
            .direction = ESP_ZB_ZCL_REPORT_DIRECTION_SEND,
            .ep = LUX_ENDPOINT,
            .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT,
            .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
            .attr_id = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID,
            .u.send_info = {
                .min_interval = LUX_REPORT_MIN_INTERVAL_S,
                .max_interval = LUX_REPORT_MAX_INTERVAL_S,
                .def_min_interval = LUX_REPORT_MIN_INTERVAL_S,
                .def_max_interval = LUX_REPORT_MAX_INTERVAL_S,
            },
            .manuf_code = 0,
        };
        lux_rep_info.u.send_info.delta.u16 = LUX_REPORT_DELTA;

        esp_err_t lux_rep_ret = esp_zb_zcl_update_reporting_info(&lux_rep_info);
        if (lux_rep_ret != ESP_OK) {
            ESP_LOGW(TAG, "Lux endpoint: could not set up reporting info (err %d)", lux_rep_ret);
        } else {
            esp_zb_zcl_attr_location_info_t lux_loc_info = {
                .endpoint_id = LUX_ENDPOINT,
                .cluster_id = ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT,
                .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                .attr_id = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID,
            };
            esp_err_t lux_start_ret = esp_zb_zcl_start_attr_reporting(lux_loc_info);
            if (lux_start_ret != ESP_OK) {
                ESP_LOGW(TAG, "Lux endpoint: could not enable reporting (err %d)", lux_start_ret);
            }
        }
    }

    {
        static const uint16_t target_attr_ids[6] = {
            ATTR_TARGET_X_1, ATTR_TARGET_Y_1, ATTR_TARGET_X_2,
            ATTR_TARGET_Y_2, ATTR_TARGET_X_3, ATTR_TARGET_Y_3,
        };
        for (int i = 0; i < 6; i++) {
            esp_zb_zcl_reporting_info_t target_rep_info = {
                .direction = ESP_ZB_ZCL_REPORT_DIRECTION_SEND,
                .ep = CONTROL_ENDPOINT,
                .cluster_id = LD2450_CONTROL_CLUSTER_ID,
                .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                .attr_id = target_attr_ids[i],
                .u.send_info = {
                    .min_interval = TARGET_REPORT_MIN_INTERVAL_S,
                    .max_interval = TARGET_REPORT_MAX_INTERVAL_S,
                    .def_min_interval = TARGET_REPORT_MIN_INTERVAL_S,
                    .def_max_interval = TARGET_REPORT_MAX_INTERVAL_S,
                },
                .manuf_code = 0,
            };
            target_rep_info.u.send_info.delta.s16 = TARGET_REPORT_DELTA_MM;

            esp_err_t target_rep_ret = esp_zb_zcl_update_reporting_info(&target_rep_info);
            if (target_rep_ret != ESP_OK) {
                ESP_LOGW(TAG, "Control endpoint: could not set up reporting for attr 0x%04x (err %d)",
                    target_attr_ids[i], target_rep_ret);
                continue;
            }
            esp_zb_zcl_attr_location_info_t target_loc_info = {
                .endpoint_id = CONTROL_ENDPOINT,
                .cluster_id = LD2450_CONTROL_CLUSTER_ID,
                .cluster_role = ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                .attr_id = target_attr_ids[i],
            };
            esp_err_t target_start_ret = esp_zb_zcl_start_attr_reporting(target_loc_info);
            if (target_start_ret != ESP_OK) {
                ESP_LOGW(TAG, "Control endpoint: could not enable reporting for attr 0x%04x (err %d)",
                    target_attr_ids[i], target_start_ret);
            }
        }
    }

    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void zigbee_app_report_occupancy(uint8_t target_index, bool occupied)
{
    if (target_index >= OCCUPANCY_ENDPOINT_COUNT) {
        ESP_LOGW(TAG, "zigbee_app_report_occupancy: invalid target_index %d", target_index);
        return;
    }

    uint8_t val = occupied ? 1 : 0;
    if (occupancy_state[target_index] == val) {
        return; // no change, avoid redundant Zigbee traffic
    }
    occupancy_state[target_index] = val;

    uint8_t endpoint = target_index + 1;

    if (esp_zb_lock_acquire(portMAX_DELAY)) {
        esp_zb_zcl_set_attribute_val(endpoint, ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
            &val, false);

        // Push an immediate Report Attributes command instead of waiting for
        // the periodic reporting window -- presence changes should reach
        // Home Assistant right away, not on the next max-interval tick.
        esp_zb_zcl_report_attr_cmd_t report_cmd = {
            .zcl_basic_cmd = {
                .dst_addr_u.addr_short = 0x0000, // coordinator
                .dst_endpoint = 1,               // confirmed via ZHA: coordinator endpoint 1, cluster 0x0000 (Basic)
                .src_endpoint = endpoint,
            },
            .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            .clusterID = ESP_ZB_ZCL_CLUSTER_ID_OCCUPANCY_SENSING,
            .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
            .dis_default_resp = 1,
            .attributeID = ESP_ZB_ZCL_ATTR_OCCUPANCY_SENSING_OCCUPANCY_ID,
        };
        esp_zb_zcl_report_attr_cmd_req(&report_cmd);

        esp_zb_lock_release();
    }
}

static uint16_t lux_to_measured_value(float lux)
{
    if (lux <= 0.0f) {
        return ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_TOO_LOW;
    }

    float mv = 10000.0f * log10f(lux) + 1.0f;

    if (mv < ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MIN_MEASURED_VALUE_MIN_VALUE) {
        mv = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MIN_MEASURED_VALUE_MIN_VALUE;
    }
    if (mv > ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MAX_MEASURED_VALUE_MAX_VALUE) {
        mv = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MAX_MEASURED_VALUE_MAX_VALUE;
    }

    return (uint16_t)(mv + 0.5f);
}

void zigbee_app_report_lux(float lux)
{
    uint16_t mv = lux_to_measured_value(lux);

    uint16_t diff = (mv > lux_measured_value) ? (mv - lux_measured_value) : (lux_measured_value - mv);
    if (diff < LUX_REPORT_DELTA) {
        return; // change too small to bother; the periodic max-interval report still covers it
    }
    lux_measured_value = mv;

    if (esp_zb_lock_acquire(portMAX_DELAY)) {
        esp_zb_zcl_set_attribute_val(LUX_ENDPOINT, ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT,
            ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID,
            &mv, false);

        esp_zb_zcl_report_attr_cmd_t report_cmd = {
            .zcl_basic_cmd = {
                .dst_addr_u.addr_short = 0x0000, // coordinator
                .dst_endpoint = 1,               // confirmed via ZHA: coordinator endpoint 1, cluster 0x0000 (Basic)
                .src_endpoint = LUX_ENDPOINT,
            },
            .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            .clusterID = ESP_ZB_ZCL_CLUSTER_ID_ILLUMINANCE_MEASUREMENT,
            .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
            .dis_default_resp = 1,
            .attributeID = ESP_ZB_ZCL_ATTR_ILLUMINANCE_MEASUREMENT_MEASURED_VALUE_ID,
        };
        esp_zb_zcl_report_attr_cmd_req(&report_cmd);

        esp_zb_lock_release();
    }
}

void zigbee_app_report_targets(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3)
{
    target_coords[0] = x1;
    target_coords[1] = y1;
    target_coords[2] = x2;
    target_coords[3] = y2;
    target_coords[4] = x3;
    target_coords[5] = y3;

    if (esp_zb_lock_acquire(portMAX_DELAY)) {
        static const uint16_t attr_ids[6] = {
            ATTR_TARGET_X_1, ATTR_TARGET_Y_1, ATTR_TARGET_X_2,
            ATTR_TARGET_Y_2, ATTR_TARGET_X_3, ATTR_TARGET_Y_3,
        };
        for (int i = 0; i < 6; i++) {
            esp_zb_zcl_set_attribute_val(CONTROL_ENDPOINT, LD2450_CONTROL_CLUSTER_ID,
                ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, attr_ids[i], &target_coords[i], false);
        }
        // No immediate manual report here (unlike occupancy/lux): targets
        // move every ~100 ms frame, so an unsolicited push per frame would
        // flood the network. The reporting config set up above in
        // esp_zb_task() (TARGET_REPORT_MIN/MAX_INTERVAL_S + delta) makes the
        // stack itself decide when a change is worth sending.
        esp_zb_lock_release();
    }
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

    switch (sig_type) {
        case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
            ESP_LOGI(TAG, "Zigbee stack initialized");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;
        case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (err_status == ESP_OK) {
                if(esp_zb_bdb_is_factory_new()) {
                    // There is not a network saved (first time joining), do steering
                    ESP_LOGI(TAG, "%s: Factory new, starting network steering",
                        sig_type == ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START ? "First start" : "Reboot");
                    esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
                }
                else {
                    // There is a network saved. There is not need to search
                    ESP_LOGI(TAG, "%s: Rejoined persisted network, start reporting",
                        sig_type == ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START ? "First start" : "Reboot");
                    joined_to_zigbee = true;
                }
            } else {
                ESP_LOGW(TAG, "%s failed (status %d), retrying initialization",
                    sig_type == ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START ? "First start" : "Reboot", err_status);
                esp_zb_scheduler_alarm(bdb_commissioning_alarm_cb, ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
            }
            break;
        case ESP_ZB_BDB_SIGNAL_STEERING:
            if (err_status == ESP_OK) {
                ESP_LOGI(TAG, "Network steering successful, joined PAN");
                joined_to_zigbee = true;
            } else {
                ESP_LOGW(TAG, "Steering failed, retrying...");
                esp_zb_scheduler_alarm(bdb_commissioning_alarm_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
            }
            break;
        case ESP_ZB_ZDO_SIGNAL_LEAVE:
            ESP_LOGI(TAG, "Left network (local reset), restarting commissioning");
            joined_to_zigbee = false;
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
            break;
        default:
            ESP_LOGI(TAG, "Unhandled Zigbee signal: 0x%x, status: %d", sig_type, err_status);
            break;
    }
}

void zigbee_recommission_button_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RECOMMISSION_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    int64_t task_start = esp_timer_get_time();
    int64_t press_start = 0;
    bool triggered = false;

    while(1) {
        bool guard_active = (esp_timer_get_time() - task_start) < (RECOMMISSION_BOOT_GUARD_MS * 1000);
        bool pressed = !guard_active && (gpio_get_level(RECOMMISSION_BUTTON_GPIO) == 0);

        if(pressed) {
            if(press_start == 0){
                press_start = esp_timer_get_time();
                triggered = false;
            }
            else if(!triggered && (esp_timer_get_time() - press_start) >= (RECOMMISSION_HOLD_MS * 1000)){
                ESP_LOGW(TAG, "BOOT held %d ms, leaving network for re-commissioning", RECOMMISSION_HOLD_MS);
                if(esp_zb_lock_acquire(portMAX_DELAY)){
                    esp_zb_bdb_reset_via_local_action();
                    esp_zb_lock_release();
                }
                triggered = true;
            }
        }
        else{
            press_start = 0;
            triggered = false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
