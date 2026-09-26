# Bootloader boot log — standard bootloader, unmodified

Deliverable for task 2b. Annotated `idf.py -p /dev/ttyACM0 flash monitor` output from the
`blink` example, built against the OTA-ready partition table, unmodified esp-idf 2nd-stage
bootloader (esp-idf v6.1, XIAO ESP32-C6, 4MB flash). Stage labels follow the breakdown in
[`bootloader_notes.md`](./bootloader_notes.md).

## First stage (ROM) bootloader

```
--- esp-idf-monitor 1.9.0 on /dev/ttyACM0 115200
--- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H
ESP-ROM:esp32c6-20220919
Build:Sep 19 2022
rst:0x15 (USB_UART_HPSYS),boot:0x1f (SPI_FAST_FLASH_BOOT)
Saved PC:0x40022bcc
--- 0x40022bcc: uart_serial_tx_one_char in ROM
SPIWP:0xee
mode:DIO, clock div:2
load:0x40875730,len:0x1648
load:0x4086b910,len:0xdbc
--- 0x4086b910: esp_bootloader_get_description at .../esp_bootloader_format/esp_bootloader_desc.c:40
load:0x4086e610,len:0x31f0
--- 0x4086e610: is_xmc_chip_strict at .../bootloader_support/bootloader_flash/src/bootloader_flash.c:911
entry 0x4086b91a
--- 0x4086b91a: call_start_cpu0 at .../bootloader/subproject/main/bootloader_start.c:27
```

Note: right before `ESP-ROM:esp32c6-20220919`, the raw capture had a garbled fragment
(`I (107) esp_image: segment 2: ... size0 lenESP-ROM:...`). That is not a real log line —
it's leftover buffered output from the *previous* boot that got interleaved with the fresh
ROM banner right as the monitor reconnected after the reset pulse. Removed here for clarity;
worth knowing this kind of artifact can show up whenever flash/monitor resets mid-capture.

`rst:0x15 (USB_UART_HPSYS)` is the reset reason (this run: reset triggered via the USB
Serial/JTAG controller, i.e. the RTS pin toggle `esptool` does after flashing — matches
"Power on Reset, SW reset and watchdog SOC reset" from the notes, not the deep-sleep path).
`boot:0x1f (SPI_FAST_FLASH_BOOT)` is the boot mode read from the strapping pins. The `load:`
lines are the ROM loading pieces of the 2nd-stage bootloader into RAM; `entry 0x4086b91a` /
`call_start_cpu0` is the ROM handing off execution to the 2nd-stage bootloader.

## Second stage bootloader

```
I (23) boot: ESP-IDF v6.1 2nd stage bootloader
I (23) boot: compile time Aug 30 2026 17:38:26
I (24) boot: chip revision: v0.2
I (24) boot: efuse block revision: v0.3
I (26) boot.esp32c6: SPI Speed      : 80MHz
I (30) boot.esp32c6: SPI Mode       : DIO
I (34) boot.esp32c6: SPI Flash Size : 4MB
I (38) boot: Enabling RNG early entropy source...
```

2nd-stage bootloader identifies itself and reports the flash config it read (speed, mode,
size — confirms the earlier menuconfig fix: 4MB, matching the chip's real hardware).

### Loads the partition table

```
I (42) boot: Partition Table:
I (45) boot: ## Label            Usage          Type ST Offset   Length
I (51) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (58) boot:  1 otadata          OTA data         01 00 0000f000 00002000
I (64) boot:  2 ota_0            OTA app          00 10 00020000 00100000
I (71) boot:  3 ota_1            OTA app          00 11 00120000 00100000
I (77) boot: End of partition table
```

Reads the partition table from `0x8000` and prints it. `ota_0`/`ota_1` both 64KB-aligned
(`0x20000`, `0x120000`) — the fix from the earlier partition table review held.

### Loads the app image

```
I (81) esp_image: segment 0: paddr=00020020 vaddr=42020020 size=079c8h ( 31176) map
I (94) esp_image: segment 1: paddr=000279f0 vaddr=40800000 size=08628h ( 34344) load
I (103) esp_image: segment 2: paddr=00030020 vaddr=42000020 size=134d4h ( 79060) map
I (119) esp_image: segment 3: paddr=000434fc vaddr=40808628 size=00e70h (  3696) load
I (120) esp_image: segment 4: paddr=00044374 vaddr=408094a0 size=01b74h (  7028) load
I (127) boot: Loaded app from partition at offset 0x20000
I (128) boot: Disabling RNG early entropy source...
```

The bootloader already decided `ota_0` (offset `0x20000`) is the partition to boot (no
`otadata` pointer set yet on first flash, so it defaults to `ota_0`), and loads the app's
segments into IRAM/DRAM per the ELF's mapping.

## Application Startup

```
I (144) cpu_start: Unicore app
```

This line comes from `components/esp_system/port/cpu_start.c` — per the notes, that's the
**Port initialization** step (stage 1 of Application Startup), not "System Initialization"
(stage 2, `esp_system/startup.c`). At this log level (`I`, default `CONFIG_LOG_DEFAULT_LEVEL`)
System Initialization doesn't print anything distinctive of its own before the main task
starts — it happens, just not verbosely logged here.

```
--- Error: device reports readiness to read but returned no data (device disconnected or multiple access on port?)
--- Waiting for the device to reconnect..
```

Same benign USB re-enumeration blip seen on every boot so far — expected with the native
USB Serial/JTAG controller when the port is reopened right after a reset, not a real error.

```
I (2243) example: Turning the LED OFF!
I (3243) example: Turning the LED ON!
I (4243) example: Turning the LED OFF!
I (5243) example: Turning the LED ON!
I (6243) example: Turning the LED OFF!
I (7243) example: Turning the LED ON!
I (8243) example: Turning the LED OFF!
I (9243) example: Turning the LED ON!
I (10243) example: Turning the LED OFF!
I (11243) example: Turning the LED ON!
```

The main task is running (`app_main` → blink's own loop) — end of the startup flow.
