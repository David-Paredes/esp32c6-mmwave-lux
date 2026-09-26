/*
 * SPDX-FileCopyrightText: 2015-2021 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define PRESENCE_SENSOR_BOOTLOADER_WELCOME_MESSAGE "Presence Sensor bootloader"

#include <stdbool.h>
#include <sys/reent.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "bootloader_init.h"
#include "bootloader_utility.h"
#include "bootloader_common.h"

static const char* TAG = "boot";

//static int select_partition_number(bootloader_state_t *bs);

/*
 * We arrive here after the ROM bootloader finished loading this second stage bootloader from flash.
 * The hardware is mostly uninitialized, flash cache is down and the app CPU is in reset.
 * We do have a stack, so we can do the initialization in C.
 */
void __attribute__((noreturn)) call_start_cpu0(void)
{
    // 1. Hardware initialization
    if (bootloader_init() != ESP_OK) {
        bootloader_reset();
    }

#ifdef CONFIG_BOOTLOADER_SKIP_VALIDATE_IN_DEEP_SLEEP
    // If this boot is a wake up from the deep sleep then go to the short way,
    // try to load the application which worked before deep sleep.
    // It skips a lot of checks due to it was done before (while first boot).
    bootloader_utility_load_boot_image_from_deep_sleep();
    // If it is not successful try to load an application as usual.
#endif

    // 2. Select the number of boot partition
    // 2.1 Load Partition Table
    bootloader_state_t bs = {0};
    if(!bootloader_utility_load_partition_table(&bs)) {
        ESP_LOGE(TAG, "Load Partition Table Error!");
        bootloader_reset();
    }
    // 2.2 Decide where to boot
    int boot_index;
    // Check if boot button (GPIO9) has been pressed for at least 1s
    esp_comm_gpio_hold_t boot_pressed = bootloader_common_check_long_hold_gpio(9, 1);
    esp_rom_printf("%d\n", boot_pressed);
    // If boot button is pressed, boot from OTA_0
    if(boot_pressed == GPIO_LONG_HOLD){
        boot_index = 0;
    }
    // Boots normally
    else{
        boot_index = bootloader_utility_get_selected_boot_partition(&bs);
    }

    if (boot_index == INVALID_INDEX) {
        bootloader_reset();
    }

    // 2.1 Print a custom message!
    esp_rom_printf("[%s] %s\n", TAG, PRESENCE_SENSOR_BOOTLOADER_WELCOME_MESSAGE);

    // 3. Load the app image for booting
    bootloader_utility_load_boot_image(&bs, boot_index);
}

/* Select the number of boot partition
static int select_partition_number(bootloader_state_t *bs)
{
    // 1. Load partition table
    if (!bootloader_utility_load_partition_table(bs)) {
        ESP_LOGE(TAG, "load partition table error!");
        return INVALID_INDEX;
    }

    // 2. Select the number of boot partition
    return bootloader_utility_get_selected_boot_partition(bs);
}
*/
#if CONFIG_LIBC_NEWLIB
// Return global reent struct if any newlib functions are linked to bootloader
struct _reent *__getreent(void)
{
    return _GLOBAL_REENT;
}
#endif
