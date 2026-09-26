# ESP32C6 Pinout

| Signal           | GPIO   | Peripheral | Direction         | Notes           |
|------------------|:------:|:----------:|:-----------------:|:---------------:|
| LD2450_UART_TX   | GPIO0  | UART1      | ESP32 → LD2450 RX |                 |
| LD2450_UART_RX   | GPIO1  | UART1      | ESP32 ← LD2450 TX |                 |
| VEML7700_I2C_SDA | GPIO22 | I2C0       | bidirectional     | pullup may be needed |
| VEML7700_I2C_SCL | GPIO23 | I2C0       | ESP32 → sensor    |                 |
| BTN_BOOT         | GPIO9  | —          | input             | on-board        |
