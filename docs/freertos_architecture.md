# FreeRTOS Architecture

## Tasks

| Task              | Description                                                                     | Priority    |
|-------------------|----------------------------------------------------------------------------------|:-----------:|
| mmwave_task       | Reads UART, parses LD2450 frames, validates checksum, updates shared state       | Medium-high |
| lux_task          | Periodic I2C poll of the VEML7700, updates shared lux value                      | Low         |
| esphome_api_task  | TCP server on port 6053, handshake, ListEntities, sends states to HA             | Medium      |
| usb_stream_task   | Detects a host connected over USB (CDC), packs and streams LD2450 data           | Medium      |

> Shared state is not a FreeRTOS task: it's global structs protected by a mutex (see Queues below), with no "owner" task managing them.

## Queues

| Resource         | Data              | Written by   | Read by                            | Protection |
|------------------|:-----------------:|:------------:|:------------------------------------:|:----------:|
| g_ld2450_state   | ld2450_targets_t  | mmwave_task  | esphome_api_task, usb_stream_task  | mutex      |
| g_lux_state      | float             | lux_task     | esphome_api_task                   | mutex      |
