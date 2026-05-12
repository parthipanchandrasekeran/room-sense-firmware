/*
 * Room Sense — pull-based HTTP OTA updater.
 *
 * Periodically fetches a manifest JSON from the backend and, if it advertises
 * a newer version than the running image, downloads and installs the
 * referenced binary, then reboots into the new slot.
 *
 * Manifest format (served by the dashboard):
 *
 *   {
 *     "version": "0.3.0",
 *     "url": "http://10.0.0.70:5050/firmware/room-sense-firmware.bin",
 *     "min_version": "0.1.0"
 *   }
 *
 * Configuration via menuconfig:
 *   RS_OTA_MANIFEST_URL — URL to GET the manifest from
 *   RS_OTA_CHECK_INTERVAL_S — how often to poll (default 1 hour)
 *
 * Pull-based (not push-based) so the firmware doesn't need to expose any
 * inbound HTTP server. Boards behind NAT or with no static IPs still update.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Start the OTA polling task. Safe to call after Wi-Fi STA is connected. */
void rs_ota_start(void);

#ifdef __cplusplus
}
#endif
