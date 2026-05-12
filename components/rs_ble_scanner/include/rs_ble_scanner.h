/*
 * Room Sense — passive BLE scanner.
 *
 * Uses NimBLE (the lightweight Bluetooth host stack) to passively listen
 * for BLE advertisements in range. Every advertisement is emitted as a
 * "BLE" telemetry line:
 *
 *   BLE <addr> <rssi>          (no local name in advert)
 *   BLE <addr> <rssi> <name>   (Complete or Shortened Local Name present)
 *
 * Used by the backend for phone localization and "stranger device" alerts.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise NimBLE and start an indefinite passive scan.
 * Should be called once early in app_main (after NVS init). */
void rs_ble_scanner_start(void);

#ifdef __cplusplus
}
#endif
