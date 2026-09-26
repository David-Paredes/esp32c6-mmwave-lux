# Phase 0

## Shopping list

- [x] ESP32-C6 board: seeed studio XIAO-ESP32-C6. 512KB SRAM, 10 pines libres, USB onboard.
- [ ] LD2450: [Context: "https://www.aliexpress.com/item/1005007254785237.html?spm=a2g0o.cart.0.0.5ad57a9dtzPKmI&mp=1&pdp_npi=6%40dis%21MXN%21MXN%2082.06%21MXN%2079.90%21%21MXN%2079.90%21%21%21%402103129017880496700918460e0eb8%2112000039961554064%21ct%21MX%216702999535%21%211%210%21"]
- [ ] VEML7700: [Context: "https://www.aliexpress.com/item/1005008125338381.html?spm=a2g0o.cart.0.0.5ad57a9dtzPKmI&mp=1&pdp_npi=6%40dis%21MXN%21MXN%2037.50%21MXN%2037.50%21%21MXN%2037.50%21%21%21%402103129017880496700918460e0eb8%2112000045671133798%21ct%21MX%216702999535%21%211%210%21"]
- [ ] Dupont connectors: [Context: "https://es.aliexpress.com/item/1005011947332215.html?spm=a2g0o.productlist.main.8.6cac2ab5W51tvM&algo_pvid=a380afac-d101-41e3-a14e-7253213411a0&aem_p4p_detail=202608291731579202893108857580003808975&algo_exp_id=a380afac-d101-41e3-a14e-7253213411a0-7&pdp_ext_f={%22order%22%3A%22-1%22%2C%22spu_best_type%22%3A%22price%22%2C%22eval%22%3A%221%22%2C%22fromPage%22%3A%22search%22}&pdp_npi=6%40dis!MXN!20.67!20.67!!!8.03!8.03!%402103292b17880499177328841e0fdb!12000057095429482!sea!MX!6702999535!X!1!0!n_tag%3A-29919%3Bd%3A6d31b6ad%3Bm03_new_user%3A-29895&curPageLogUid=e1F35Dp4NfNP&utparam-url=scene%3Asearch|query_from%3A|x_object_id%3A1005011947332215|_p_origin_prod%3A&search_p4p_id=202608291731579202893108857580003808975_3"]
- [x] Protoboard

## esp-idf toolchain

```bash
❯ idf.py --version
ESP-IDF v6.1-rc1
```

## Blink flashed and serial communication

```bash
I (23) boot: ESP-IDF v6.1-rc1 2nd stage bootloader
I (23) boot: compile time Aug 29 2026 18:07:59
I (24) boot: chip revision: v0.2
I (24) boot: efuse block revision: v0.3
I (27) boot.esp32c6: SPI Speed      : 80MHz
I (30) boot.esp32c6: SPI Mode       : DIO
I (34) boot.esp32c6: SPI Flash Size : 2MB
I (38) boot: Enabling RNG early entropy source...
I (42) boot: Partition Table:
I (45) boot: ## Label            Usage          Type ST Offset   Length
I (51) boot:  0 nvs              WiFi data        01 02 00009000 00006000
I (58) boot:  1 phy_init         RF data          01 01 0000f000 00001000
I (64) boot:  2 factory          factory app      00 00 00010000 00100000
I (71) boot: End of partition table
I (74) esp_image: segment 0: paddr=00010020 vaddr=42018020 size=079c8h ( 31176) map
I (88) esp_image: segment 1: paddr=000179f0 vaddr=40800000 size=00628h (  1576) load
I (89) esp_image: segment 2: paddr=00018020 vaddr=42000020 size=134c4h ( 79044) map
I (112) esp_image: segment 3: paddr=0002b4ec vaddr=40800628 size=08e70h ( 36464) load
I (121) esp_image: segment 4: paddr=00034364 vaddr=408094a0 size=01b74h (  7028) load
I (125) boot: Loaded app from partition at offset 0x10000
I (126) boot: Disabling RNG early entropy source...
I (138) cpu_start: Unicore app
--- Error: device reports readiness to read but returned no data (device disconnected or multiple access on port?)
--- Waiting for the device to reconnect..
I (2250) example: Turning the LED OFF!
I (3250) example: Turning the LED ON!
I (4250) example: Turning the LED OFF!
I (5250) example: Turning the LED ON!
I (6250) example: Turning the LED OFF!
I (7250) example: Turning the LED ON!
I (8250) example: Turning the LED OFF!
I (9250) example: Turning the LED ON!
I (10250) example: Turning the LED OFF!
I (11250) example: Turning the LED ON!
I (12250) example: Turning the LED OFF!
I (13250) example: Turning the LED ON!
I (14250) example: Turning the LED OFF!
I (15250) example: Turning the LED ON!
```
