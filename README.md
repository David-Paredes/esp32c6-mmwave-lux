# mmWave and lux sensor in ESP32C6

Project to create a motion and light sensor to add in home assistant

Connects to your existing Zigbee network, exposed to Home Assistant via ZHA
(standard clusters for presence/lux, a custom cluster + zigpy quirk for the
LD2450's full x/y/speed tracking data). See `docs/zigbee_setup.md` for the
menuconfig and partition table setup, and `docs/zigbee_clusters.md` (Phase 6)
for the cluster/quirk design once that's written.

## Dependencies

Target: `esp32c6` (`idf.py set-target esp32c6` — must be set explicitly, esp-idf
does not infer it from the board name).

Managed via the esp-idf Component Manager (`idf_component.yml` under `main/`),
resolved automatically on `idf.py build`/`reconfigure` into `managed_components/`:

- `espressif/esp-zigbee-lib` (1.6.8)
- `espressif/esp-zboss-lib` (1.6.4) — required by `esp-zigbee-lib` at the CMake
  level (`REQUIRES`), but not declared as a manifest dependency, so it has to
  be added explicitly: `idf.py add-dependency "espressif/esp-zboss-lib"`.
  Skipping this step fails the build with `Failed to resolve component
  'espressif__esp-zboss-lib': unknown name`.

Requires `esp-idf >= 6.1` per this project's toolchain; note that as of
2026-09-01 `esp-zigbee-sdk`/`esp-zboss-lib` don't officially list ESP-IDF 6.x
as supported yet (open upstream request), so if a future SDK update breaks
compatibility, pinning back to a supported 5.x esp-idf may become necessary.
