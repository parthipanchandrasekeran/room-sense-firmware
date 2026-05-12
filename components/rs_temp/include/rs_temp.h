/*
 * Room Sense — on-chip temperature reporter.
 *
 * Spawns a FreeRTOS task that polls the ESP32-S3 internal temperature
 * sensor every 10 s and emits a "TEMP" telemetry line via rs_telemetry.
 *
 * No-op on chips without an internal temperature sensor.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Start the temperature reporter task. Safe to call after rs_telemetry_init. */
void rs_temp_start(void);

#ifdef __cplusplus
}
#endif
