# Room Sense Firmware

ESP32-S3 firmware for the Room Sense home-presence sensor mesh.

Built on top of Espressif's official [esp-csi](https://github.com/espressif/esp-csi) library
(specifically the `esp_wifi_sensing` component), which provides 3-state
WiFi-CSI-based presence detection:

- **none** — empty room
- **someone static** — person present but still (e.g. sitting, sleeping)
- **someone moves** — active motion

This replaces the previous fork of `StevenMHernandez/ESP32-CSI-Tool`. The
backend ([room-sense](https://github.com/parthipanchandrasekeran/room-sense))
consumes the data stream produced here.

## Status

🚧 **Work in progress — Phase 1 (toolchain bring-up).**

## Hardware

- 3x ESP32-S3 DevKitC boards, one per room
- Connected to a 2.4 GHz WiFi network with a known AP (router)

## Build

Requires ESP-IDF v5.4+. Activate the IDF environment, then:

```bash
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

## Roadmap

- [x] Phase 0: verify toolchain + esp-csi compatibility
- [ ] Phase 1: scaffold + clean build for ESP32-S3
- [ ] Phase 2: port BLE scanner, WiFi neighbor scanner, CPU temp publisher
- [ ] Phase 3: UDP transport + line protocol matching existing backend
- [ ] Phase 4: OTA update support
- [ ] Phase 5: per-node menuconfig (rooms, peer MACs)
- [ ] Phase 6: migrate all 3 boards to new firmware

## License

MIT. See LICENSE.

Built on:
- [espressif/esp-csi](https://github.com/espressif/esp-csi) — Apache 2.0
