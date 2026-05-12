/*
 * Room Sense — WiFi neighbor scanner.
 *
 * Periodically scans all 2.4 GHz channels for nearby APs and reports each
 * one to the backend, plus a heartbeat ("WIFI_HB") so we can detect a
 * wedged task. Includes recovery logic for the stuck-scan state that
 * showed up in the previous fork after running for hours.
 *
 * Important: WiFi scans briefly take the radio off the STA channel,
 * which interrupts CSI capture. We use passive scanning + a 60-second
 * cadence to minimise the impact.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Start the WiFi neighbor scan task. Must be called after WiFi STA is
 * connected (the task waits internally but takes longer to spin up if
 * called pre-association). */
void rs_wifi_neighbor_start(void);

#ifdef __cplusplus
}
#endif
