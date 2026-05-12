# Room Sense Firmware

ESP32-S3 firmware for the Room Sense home-presence sensor mesh.

Built on top of Espressif's official [esp-csi](https://github.com/espressif/esp-csi)
library (specifically the `esp_wifi_sensing` component), which provides
3-state WiFi-CSI-based presence detection:

- **none** — empty room
- **someone static** — person present but still (e.g. sitting, sleeping)
- **someone moves** — active motion

This replaces the previous fork of `StevenMHernandez/ESP32-CSI-Tool`. The
backend ([room-sense](https://github.com/parthipanchandrasekeran/room-sense))
consumes the data stream produced here.

---

## Status

✅ **Phase 3 complete (v0.3.0)**

| Capability | Status |
|---|---|
| esp_wifi_sensing 3-state FSM | ✅ wired |
| Wireless UDP telemetry | ✅ `rs_telemetry` |
| BLE passive scanner | ✅ `rs_ble_scanner` |
| WiFi neighbor scanner w/ watchdog | ✅ `rs_wifi_neighbor` |
| CPU temperature publisher | ✅ `rs_temp` |
| Pull-based HTTP OTA updater | ✅ `rs_ota` |
| Dual-OTA partition layout (8 MB flash) | ✅ |
| Backend manifest endpoint | ✅ (in room-sense repo) |
| Live validation on real hardware | ⏳ Phase 4 |

Binary size: ~1.15 MB. App partitions: 2 MB each. **44% free** for future features.

---

## Components

```
main/                     FSM + WiFi + LED wiring (from esp_wifi_sensing demo)
components/
├── rs_telemetry/         UDP transport, line protocol, menuconfig host/port
├── rs_temp/              ESP32-S3 on-chip temp sensor → TEMP lines
├── rs_wifi_neighbor/     Passive AP scan + hardening + WIFI_HB heartbeats
├── rs_ble_scanner/       NimBLE scanner + Local Name extraction → BLE lines
└── rs_ota/               Polls dashboard manifest, downloads + reboots
```

---

## Build

Requires ESP-IDF v5.4+ (tested on v5.5.4).

```bash
# Activate ESP-IDF environment
. $IDF_PATH/export.sh        # Linux/macOS
# or:
%IDF_PATH%\export.bat        # Windows

# Configure
idf.py set-target esp32s3
idf.py menuconfig            # set WiFi SSID, OTA manifest URL, telemetry host

# Build
idf.py build
```

Key menuconfig settings under `Room Sense`:

- **Telemetry → Backend host IP** — your dashboard server's LAN IP
- **Telemetry → Backend UDP port** — default 5051 (matches `csi_udp_receiver.py`)
- **OTA → Firmware manifest URL** — default `http://10.0.0.70:5050/firmware/manifest.json`
- **OTA → Check interval** — default 3600 seconds (1 hour)

And under `Example Connection Configuration`:

- **WiFi SSID** + **WiFi Password** — your AP

---

## Migration from old fork

If you're moving from `parthipanchandrasekeran/ESP32-CSI-Tool` (the old
Steven-Hernandez-based fork), the cutover is **one board at a time** to
keep the dashboard healthy:

1. Build this firmware with your WiFi creds + dashboard host configured (`idf.py menuconfig`).
2. Pick the least-important board (e.g. Bedroom 2). Plug into USB.
3. `idf.py -p COM7 flash monitor` — replace `COM7` with the actual port.
4. Watch the serial monitor for a successful WiFi connect + `STATE active|inactive` events.
5. Open the dashboard. The board should still show its same `esp32-xxxx` name (MAC is unchanged). Verify it's reporting motion / presence correctly.
6. **Leave it for 24 hours.** Confirm no crashes, BLE still works, etc.
7. Once stable, repeat for the remaining two boards.

After all three are migrated, future updates use OTA:

1. Bump version in `main/app_main.c` (search for `BOOT firmware=`)
2. `idf.py build`
3. `cp build/room-sense-firmware.bin /path/to/room-sense/firmware/`
4. Edit `firmware/manifest.json` with the new version
5. Boards self-update within an hour. Done — no USB cables.

---

## Line protocol (UDP → backend)

Every packet is prefixed with the board's STA MAC. Payload formats:

```
<mac> BOOT firmware=room-sense v<x.y.z>
<mac> STATE active <jitter>          # CSI: someone moves
<mac> STATE inactive 0               # CSI: room quiet
<mac> TEMP <celsius>                 # every 10 s
<mac> BLE <addr> <rssi> [<name>]     # per BLE advertisement
<mac> WIFI <bssid> <rssi> <chan> <auth> <ssid>   # ~every 60 s
<mac> WIFI_HB <consec_errors>        # scanner heartbeat
<mac> WIFI_ERR 0x<code> <consec_errors>
<mac> OTA running=<v> | update available=<from> -> <to> | success rebooting | failed err=0x<x>
```

Backend parser: `csi_udp_receiver.py` in the room-sense repo.

---

## License

MIT. See LICENSE.

Built on:
- [espressif/esp-csi](https://github.com/espressif/esp-csi) — Apache 2.0
- ESP-IDF — Apache 2.0

The legacy fork ([parthipanchandrasekeran/ESP32-CSI-Tool](https://github.com/parthipanchandrasekeran/ESP32-CSI-Tool))
is preserved for historical reference but no longer the active firmware.
