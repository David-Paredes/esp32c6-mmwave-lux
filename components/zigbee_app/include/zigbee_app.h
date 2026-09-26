#pragma once

#include <stdbool.h>
#include "esp_zigbee_core.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define HA_ESP_OCCUPANCY_ID 0x0107
#define OCCUPANCY_ENDPOINT_COUNT 3
// Endpoints 1..OCCUPANCY_ENDPOINT_COUNT, one per LD2450 target.
// target_index (0..2, as used by zigbee_app_report_occupancy) -> endpoint target_index + 1
#define OCCUPANCY_REPORT_MIN_INTERVAL_S 0
#define OCCUPANCY_REPORT_MAX_INTERVAL_S 3600

#define LUX_ENDPOINT (OCCUPANCY_ENDPOINT_COUNT + 1)
#define LUX_REPORT_MIN_INTERVAL_S 10
#define LUX_REPORT_MAX_INTERVAL_S 3600
// Minimum change in ZCL MeasuredValue (10000*log10(lux)+1 scale) worth an
// unsolicited report. ~500 is roughly a 12% relative change in lux -- tune
// to taste once you see real VEML7700 noise on your board.
#define LUX_REPORT_DELTA 500

// LD2450 control cluster: sends the 3 tracked targets' x/y coordinates to
// Home Assistant, and receives back the region-filter config HA writes.
// Manufacturer/application-specific cluster ID, in the custom range
// (0xFC00-0xFFFF).
#define LD2450_CONTROL_CLUSTER_ID 0xFC10
#define CONTROL_ENDPOINT (LUX_ENDPOINT + 1)

// Output attributes (device -> HA, read-only + reportable): target x/y, mm.
// 0,0 means that target slot is empty.
#define ATTR_TARGET_X_1 0x0000
#define ATTR_TARGET_Y_1 0x0001
#define ATTR_TARGET_X_2 0x0002
#define ATTR_TARGET_Y_2 0x0003
#define ATTR_TARGET_X_3 0x0004
#define ATTR_TARGET_Y_3 0x0005

// Input attributes (HA -> device, read/write): region filter config. IDs are
// contiguous (0x0010..0x001C) and in the same order as ld2450_filter_config_t
// so a simple range check identifies "this write is for the filter config"
// and zigbee_app.c can use control_state's storage directly as that struct.
#define ATTR_FILTER_TYPE 0x0010  // 0 = disabled, 1 = detect only inside regions, 2 = ignore inside regions
#define ATTR_REGION1_X1  0x0011
#define ATTR_REGION1_Y1  0x0012
#define ATTR_REGION1_X2  0x0013
#define ATTR_REGION1_Y2  0x0014
#define ATTR_REGION2_X1  0x0015
#define ATTR_REGION2_Y1  0x0016
#define ATTR_REGION2_X2  0x0017
#define ATTR_REGION2_Y2  0x0018
#define ATTR_REGION3_X1  0x0019
#define ATTR_REGION3_Y1  0x001A
#define ATTR_REGION3_X2  0x001B
#define ATTR_REGION3_Y2  0x001C

#define TARGET_REPORT_MIN_INTERVAL_S 1
#define TARGET_REPORT_MAX_INTERVAL_S 60
// Minimum coordinate change, mm, worth an unsolicited report (checked per
// attribute). Targets move every ~100 ms frame -- this keeps normal walking
// from flooding the network while still reporting promptly. Tune once you
// see real jitter on your board.
#define TARGET_REPORT_DELTA_MM 100

// How long to wait after the last region-filter attribute write from HA
// before forwarding the combined config to the LD2450 driver. HA/zigpy
// normally writes all 13 attributes (filter_type + 12 region coords) in one
// burst; this coalesces that burst into a single
// ld2450_request_filter_update() call instead of up to 13 back-to-back ones.
#define FILTER_WRITE_DEBOUNCE_MS 200

#define ESP_ZB_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK
// BOOT button (GPIO9) re-commissioning: the button is ignored for the first
// RECOMMISSION_BOOT_GUARD_MS after the task starts (avoids a false trigger from
// residual hold left over from flashing/reset), then must be held for
// RECOMMISSION_HOLD_MS to trigger a network leave. Effective worst-case hold
// time as perceived by the user: guard + hold = 5s + 3s = 8s.
#define RECOMMISSION_BUTTON_GPIO GPIO_NUM_9
#define RECOMMISSION_HOLD_MS 3000
#define RECOMMISSION_BOOT_GUARD_MS 5000

extern volatile bool joined_to_zigbee;

void esp_zb_task(void *pvParameters);

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct);

void zigbee_recommission_button_task(void *pvParameters);

// Called whenever a target's presence changes (e.g. from ld2450_driver).
// target_index is 0..2 (target 1..3); maps to Zigbee endpoint target_index + 1.
// Updates the local ZCL attribute and pushes an immediate Report Attributes
// command to the coordinator.
void zigbee_app_report_occupancy(uint8_t target_index, bool occupied);

// Called whenever a new lux reading is available (e.g. from veml7700_driver).
// Converts to the ZCL Illuminance Measurement MeasuredValue encoding,
// updates the local attribute and pushes a Report Attributes command when
// the change is large enough to bother (see LUX_REPORT_DELTA).
void zigbee_app_report_lux(float lux);

// Called whenever a new set of target coordinates is available (e.g. from
// ld2450_driver, after ld2450_read_data()). x/y in mm; pass 0,0 for an empty
// target slot. Updates the local ZCL attributes -- the stack itself decides
// when to report per TARGET_REPORT_MIN/MAX_INTERVAL_S and
// TARGET_REPORT_DELTA_MM (no immediate manual report, unlike occupancy/lux:
// targets move every frame and an immediate push per frame would flood the
// network).
void zigbee_app_report_targets(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3);
