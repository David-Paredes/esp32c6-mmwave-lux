# ESP32-C6 boot flow (ROM bootloader → 2nd-stage → app)

This note is related to what happens when the ESP32C6 is
energized and before app_main function.

The boot can be divided in 3 steps:

## First stage (ROM) bootloader

Loads 2nd-stage bootloader to RAM (IRAM & DRAM) from flash (0x0)
Checks the reset reason:

1. Reset from deep sleep.
Runs from STORE6 register if it exists and its CRC is valid or restarts as power on reset.

2. Power on Reset, SW reset and watchdog SOC reset.
Can execute code based on the boot mode from GPIO_STRAP_REG.

3. CPU reset and watchdog CPU reset.
Configures SPI flash based on EFUSE values and attempts to run code from flash.

## Second stage bootloader

Loads partition table (partition table starts at 0x8000 bootloader has size of 0x8000)
and main app image from flash (IRAM & DRAM).
In ESP-IDF runs the binary image from 0x0 in flash (code in components/bootloader),
this stage is also where flash encryption and secure boot takes place.
The bootloader reads otadata partition to determine which OTA partition
should be booted.

## Application Startup

1. Port initialization.
Runs call_start_cpu0 (components/esp_system/port/cpu_start.c) from 2nd stage bootloader,
Initializes the basic C runtime environment and performs initial configuration
of the SOC's HW.

2. System Initialization.
Its function is start_cpu0 (components/esp_system/startup.c).
Logs information about the application, initializes the heap, several libraries,
SPI flash API support and constructors. This is where efuse can be burned.

3. Running the Main Task.
The main task is created and the FreeRTOS scheduler starts running (app_main).

CRC
: Cyclic Redundancy Check

RTC
: Real-Time Control
