---
project: ESP32-C6 mmWave + Lux + native Home Assistant
type: peripheral reference notes
date: 2026-09-04
esp-idf version: v6.1 (matches installed toolchain)
---

# UART & I2C — ESP32-C6 reference notes (esp-idf v6.1)

Sourced from the official esp-idf v6.1 docs for ESP32-C6, matching the installed toolchain exactly. Written for this project's two upcoming drivers: LD2450 over UART (Phase 3) and VEML7700 over I2C (Phase 4).

## UART — for the LD2450

The ESP32-C6 has 2 general-purpose UART controllers (`UART_NUM_0`, `UART_NUM_1`) plus a separate low-power UART not needed here. `UART_NUM_0` is almost certainly tied up as the console/log output (used by `idf.py monitor`), so plan on `UART_NUM_1` for the LD2450.

Setup is a three-call sequence:

```c
uart_param_config(uart_num, &uart_config);   // baud rate, data bits, parity, stop bits
uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
uart_driver_install(uart_num, rx_buf_size, tx_buf_size, queue_size, &uart_queue, 0);
```

Order matters less than expected (config-before-install or after both work), but this is the conventional order in most examples.

**Key design point**: the LD2450 streams continuously and unprompted (not request/response — it just pushes frames), so this calls for the **event-driven pattern**, not polling `uart_read_bytes` in a tight loop. Pass a non-NULL `uart_queue` to `uart_driver_install`, then run a dedicated FreeRTOS task blocking on `xQueueReceive(uart_queue, &event, portMAX_DELAY)`. On `UART_DATA` events, pull bytes with `uart_read_bytes()` and feed them into the frame parser/state machine — this is exactly the "resync on a corrupt frame" deliverable from Phase 3: the parser needs to look for the LD2450's frame header bytes and resync if it sees garbage, rather than assuming byte-aligned frames. On `UART_FRAME_ERR` or `UART_PARITY_ERR`, call `uart_flush_input()` and let the parser's resync logic take over rather than trying to salvage the partial frame.

ESP32-C6-specific note: routing TX/RX to non-default GPIOs (likely, given this project's pinout) means those signals go through the GPIO matrix rather than the direct IO MUX path — functionally transparent, but worth knowing if any matrix-routing timing quirks show up (rare, mentioned in errata).

### Function signatures

```c
esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size,
                               int queue_size, QueueHandle_t *uart_queue, int intr_alloc_flags);

esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *uart_config);

esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num,
                        int rts_io_num, int cts_io_num, int dtr_io_num, int dsr_io_num);

int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size);

int uart_write_bytes_with_break(uart_port_t uart_num, const void *src, size_t size, int brk_len);

int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, uint32_t ticks_to_wait);

esp_err_t uart_get_buffered_data_len(uart_port_t uart_num, size_t *size);

esp_err_t uart_driver_delete(uart_port_t uart_num);
```

`uart_event_t` structure delivered via the queue:

```c
typedef struct {
    uart_event_type_t type;
    size_t size;
    bool timeout_flag;
} uart_event_t;
```

Relevant event types: `UART_DATA` (new data available), `UART_FIFO_OVF` (RX FIFO overflow), `UART_BUFFER_FULL` (ring buffer full), `UART_BREAK` (break signal detected), `UART_PARITY_ERR`, `UART_FRAME_ERR`.

Other notes:
- RX buffer size must exceed the hardware FIFO length (`UART_HW_FIFO_LEN(uart_num)`).
- TX buffer can be zero (blocks until transmission) or larger than the FIFO.
- `UART_PIN_NO_CHANGE` is the sentinel to skip pin reconfiguration.
- Glitch filtering via `uart_config_t.rx_glitch_filt_thresh` (nanosecond units) improves noisy RX reliability.

## I2C — for the VEML7700

The ESP32-C6 has exactly **1 general-purpose I2C controller** (plus a cut-down LP-I2C not needed here). Since the project is on esp-idf v6.1, it's on the **new** `i2c_master` API (`driver/i2c_master.h`) — the legacy `i2c.h` driver from older esp-idf versions doesn't apply here, so older VEML7700 example code using `i2c_master_cmd_begin()` or similar is the old API and shouldn't be followed directly.

Setup is two steps:

```c
i2c_master_bus_config_t bus_config = {
    .i2c_port = -1,           // auto-select
    .sda_io_num = YOUR_SDA_GPIO,
    .scl_io_num = YOUR_SCL_GPIO,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .glitch_ignore_cnt = 7,   // typical default
};
i2c_master_bus_handle_t bus_handle;
i2c_new_master_bus(&bus_config, &bus_handle);

i2c_device_config_t dev_config = {
    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
    .device_address = 0x10,   // VEML7700's fixed 7-bit address
    .scl_speed_hz = 100000,   // standard mode; check the datasheet for the max it supports
};
i2c_master_dev_handle_t dev_handle;
i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle);
```

**Register access pattern**: for reading/writing sensor registers (writing ALS_CONF for gain/integration time, reading back the ALS output register), use `i2c_master_transmit_receive()`. It writes the command byte(s) (register pointer) and reads the response **without a STOP condition in between** — the repeated-start pattern the VEML7700 (like most sensor register interfaces) expects. Plain `i2c_master_transmit`/`i2c_master_receive` are one-directional only.

**I2C scan / device detection** (Phase 4 deliverable): `i2c_master_probe(bus_handle, address, timeout_ms)` does a bare address+ACK check without a real transaction, returning `ESP_ERR_NOT_FOUND` if nothing acks. Important: pull-ups must physically be present on SDA/SCL for probing (and operation in general) to work — worth confirming the breakout board has them built in (most VEML7700 breakouts do) before spending time debugging a "no ACK" that's actually a wiring/pull-up issue.

### Function signatures

```c
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config,
                              i2c_master_bus_handle_t *ret_bus_handle);

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                     const i2c_device_config_t *dev_config,
                                     i2c_master_dev_handle_t *ret_handle);

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev, const uint8_t *write_buffer,
                               size_t write_size, int xfer_timeout_ms);

esp_err_t i2c_master_receive(i2c_master_dev_handle_t i2c_dev, uint8_t *read_buffer,
                              size_t read_size, int xfer_timeout_ms);

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev,
                                       const uint8_t *write_buffer, size_t write_size,
                                       uint8_t *read_buffer, size_t read_size,
                                       int xfer_timeout_ms);

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle, uint16_t address,
                            int xfer_timeout_ms);

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle);
esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle);
```

`i2c_master_bus_config_t` key fields: `i2c_port` (-1 for auto), `sda_io_num`/`scl_io_num`, `clk_source` (`I2C_CLK_SRC_DEFAULT`, `I2C_CLK_SRC_XTAL`, or `I2C_CLK_SRC_RC_FAST`), `glitch_ignore_cnt` (typically 7), `intr_priority`, `trans_queue_depth`, `enable_internal_pullup`, `allow_pd`.

`i2c_device_config_t` key fields: `dev_addr_length` (`I2C_ADDR_BIT_LEN_7` or `I2C_ADDR_BIT_LEN_10`), `device_address`, `scl_speed_hz`, `scl_wait_us` (0 = default register value), `disable_ack_check`.

Clock limits: 100 kHz (Standard-mode) / 400 kHz (Fast-mode) max for SCL in master mode. Pull-up resistors: 2 kΩ–5 kΩ recommended range, adjusted based on frequency and current draw.

Thread safety: `i2c_new_master_bus`/`i2c_master_bus_add_device` and the master operation functions (`i2c_master_transmit`, `i2c_master_receive`, `i2c_master_transmit_receive`, `i2c_master_probe`) are thread-safe via a bus semaphore. Other functions require external synchronization.

Other utilities: `i2c_master_multi_buffer_transmit()` (multiple buffers in one transaction, START/STOP only at boundaries), `i2c_master_execute_defined_operations()` (full manual control for non-standard devices), `i2c_master_device_change_address()`, `i2c_master_bus_reset()`, `i2c_master_bus_wait_all_done()`, `i2c_master_get_bus_handle()`.

## Sources

- [Universal Asynchronous Receiver/Transmitter (UART) - ESP32-C6 - ESP-IDF Programming Guide v6.1](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/uart.html)
- [Inter-Integrated Circuit (I2C) - ESP32-C6 - ESP-IDF Programming Guide v6.1](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/i2c.html)

**Note**: these signatures come from the official v6.1 doc pages (matching the installed toolchain), not yet cross-checked against the actual installed esp-idf headers on this machine (the device bridge was disconnected when this was written). If anything looks off once writing real code against the installed headers, verify against source, consistent with how the Zigbee SDK docs mismatch was caught earlier in this project.
