# Zigbee setup notes (esp-zigbee-sdk, ESP32-C6)

Deliverable draft for Phase 5's first task. Written 2026-09-01, before the WiFi→Zigbee pivot could be
synced to the actual repo (PC was off) — treat the exact Kconfig paths and sizes as a solid starting
point to confirm against your own `idf.py menuconfig` and first build/flash log, the same way we
confirmed the flash-size and partition-alignment fixes earlier in the project.

## Getting the SDK

`esp-zigbee-sdk` is Espressif's Zigbee stack on top of esp-idf (closed-source ZBOSS underneath, prebuilt
libraries). Add it as a component dependency rather than hand-writing anything at the stack level:

```
idf.py add-dependency "espressif/esp-zigbee-lib"
```

(confirm the exact component name/version against [espressif/esp-zigbee-sdk](https://github.com/espressif/esp-zigbee-sdk)
for whatever's current when you get here — SDK component names have moved before.)

## menuconfig changes

Search inside `idf.py menuconfig` with `/` and "zigbee" to find the exact menu path in your installed
version. The options themselves:

- `CONFIG_ZB_ENABLED=y` — turns on the Zigbee stack.
- Role — pick **exactly one**, the build links a different prebuilt library per role:
  - `CONFIG_ZB_ZED=y` — **End Device**. This is what you want: joins the network, reports data,
    doesn't route for other devices. Lowest power/complexity, and you already have a coordinator.
  - `CONFIG_ZB_ZCZR=y` — Coordinator/Router. Not needed here.
  - `CONFIG_ZB_ZGPD=y` — Green Power Device. Not relevant.
- Radio — leave `CONFIG_ZB_RADIO_SPINEL_UART` **unset** so it uses the ESP32-C6's on-chip 802.15.4
  radio natively (links `libzboss_port.native.a`). Sets like this only matter for boards without a
  native radio (ESP32/S3), which need an external radio co-processor — not your case.
- Optional while bringing it up: `CONFIG_ZB_DEBUG_MODE=y` for extra stack logging. Note this needs a
  bigger app partition (~1200K instead of the default) — check this against your `ota_0`/`ota_1` size
  (currently 1MB/0x100000 each) if you turn it on; may need to size up temporarily for debug builds.

## partitions.csv changes

Two new partitions, both **fixed size** — don't resize them, the stack expects exactly these:

```
zb_storage, data, fat,  , 16K,
zb_fct,     data, fat,  , 1K,
```

- `zb_storage` (16K): Zigbee network state — keys, neighbor table, binding table, node descriptor.
  This is what lets the device remember it already joined across power cycles, instead of re-joining
  every boot.
- `zb_fct` (1K): factory/install-code data.

Suggested placement in your current table: right after `nvs`, before `otadata`, offset left blank
(same auto-align approach we used for `ota_0`/`ota_1`). Full table would look like:

```
# Name,     Type, SubType,  Offset,   Size
nvs,        data, nvs,      0x9000,   0x6000
zb_storage, data, fat,      ,         16K
zb_fct,     data, fat,      ,         1K
otadata,    data, ota,      ,         0x2000
ota_0,      app,  ota_0,    0x20000,  0x100000
ota_1,      app,  ota_1,    0x120000, 0x100000
```

Room check: current layout (`nvs` 24K + `otadata` 8K + `ota_0` 1MB + `ota_1` 1MB) uses ~2.03MB of the
4MB flash. Adding 17K for Zigbee is negligible — no repartition/size squeeze expected. Worth flashing
and checking the boot log's partition table dump afterward (same as we did for the original OTA
partitions) to confirm the offsets landed where expected and nothing collided.

## What this replaces from the original WiFi plan

- No more SoftAP + captive portal, no WiFi credentials in NVS, no mDNS announcement (all of old Phase 5).
- No more hand-rolled ESPHome native API / port 6053 / protobuf (all of old Phase 6).
- Zigbee's equivalent of "provisioning" is joining/commissioning: put the coordinator in permit-join
  mode, and the device attempts a join on first boot — or whenever you re-trigger it (e.g. holding
  BOOT, planned for the new Phase 5 re-commissioning task).

## Sources

- [ESP Zigbee SDK — User Guide (ESP32-C6)](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32c6/user-guide/index.html)
- [ESP Zigbee SDK — Developing with the SDK (ESP32-C6)](https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32c6/developing.html)
- [Flash Partitions — espressif/esp-zigbee-sdk (DeepWiki)](https://deepwiki.com/espressif/esp-zigbee-sdk/2.3-flash-partitions)
- [Building and Flashing — espressif/esp-zigbee-sdk (DeepWiki)](https://deepwiki.com/espressif/esp-zigbee-sdk/3.2-building-and-flashing)
- [XIAO ESP32C6 Zigbee Quick Start Guide — Seeed Studio Wiki](https://wiki.seeedstudio.com/xiao_esp32c6_zigbee/)
- [espressif/esp-zigbee-sdk — examples/esp_zigbee_HA_sample](https://github.com/espressif/esp-zigbee-sdk/tree/main/examples/esp_zigbee_HA_sample)
