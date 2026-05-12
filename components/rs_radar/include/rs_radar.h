/*
 * Room Sense — HLK-LD2410C 24 GHz mmWave radar driver.
 *
 * The LD2410C is a small UART module that detects motion AND breathing
 * (static targets) up to ~6 m. Where WiFi CSI is room-wide but noisy,
 * the radar is single-point but high-confidence. Used for:
 *   - Sleep tracking (breathing rate via static-target energy)
 *   - Cross-validating WiFi CSI's room-level detection
 *   - Catching false positives (radar says empty, CSI says occupied)
 *
 * Telemetry emitted (once per radar frame, rate-limited to ~1 Hz):
 *
 *   RADAR <state> <move_cm> <move_e> <static_cm> <static_e> <detect_cm>
 *
 * Fields:
 *   state      — 0=none 1=moving 2=static 3=moving+static
 *   move_cm    — distance to moving target, 0 if none
 *   move_e     — moving-target energy, 0..100
 *   static_cm  — distance to static (breathing) target, 0 if none
 *   static_e   — static-target energy, 0..100
 *   detect_cm  — overall detection distance (closest target)
 *
 * Wiring (ESP32-S3 DevKitC defaults; configurable via menuconfig):
 *   LD2410 VCC  -> ESP 5V
 *   LD2410 GND  -> ESP GND
 *   LD2410 TX   -> ESP GPIO 18 (uart1 rx)
 *   LD2410 RX   -> ESP GPIO 17 (uart1 tx)
 *   LD2410 OUT  -> leave unconnected
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Start the radar UART driver task. Safe to call after WiFi STA is up
 * (uses rs_telemetry to emit RADAR lines). No-op if the radar isn't
 * wired up — the driver just waits for valid frames forever. */
void rs_radar_start(void);

#ifdef __cplusplus
}
#endif
