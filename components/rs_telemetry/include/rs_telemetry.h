/*
 * Room Sense — UDP telemetry transport.
 *
 * Sends line-oriented packets to the Room Sense backend. Every packet is
 * prefixed with the board's STA MAC so a single receiver can dispatch
 * datagrams from many boards.
 *
 * Backend parser: csi_udp_receiver.py
 * Format on the wire:
 *     <board_mac> <TYPE> <payload>\n
 * where <TYPE> is one of: STATE | TEMP | BLE | WIFI | WIFI_HB | WIFI_ERR
 */
#pragma once

#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the UDP socket and resolve our board MAC.
 *
 * Safe to call once Wi-Fi STA is up and we have an IP. The destination
 * host/port are taken from menuconfig (CONFIG_RS_TELEMETRY_HOST_IP /
 * CONFIG_RS_TELEMETRY_HOST_PORT). Returns ESP_OK on success.
 */
int rs_telemetry_init(void);

/* True once rs_telemetry_init() has set up the socket. Other modules
 * (BLE scanner, etc.) can poll this to defer transmission until ready. */
bool rs_telemetry_ready(void);

/* Send a typed line: "<board_mac> <type> <formatted_payload>"
 * Drops silently if the socket is not initialised. */
void rs_telemetry_send(const char *type, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
