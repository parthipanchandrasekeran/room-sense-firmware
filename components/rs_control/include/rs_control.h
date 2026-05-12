/*
 * Room Sense — remote control UDP listener.
 *
 * Accepts ASCII line-oriented commands from any host on the LAN so the
 * dashboard (or a CLI tool) can drive the board without USB serial.
 *
 * Wire format: a single line per UDP datagram, no trailing CR/LF required.
 *
 *   TRAIN_START <peer>    Begin FSM training for peer (AP|MAC_1|MAC_2|<full mac>).
 *   TRAIN_STOP  <peer>    Stop training; thresholds locked in.
 *   TRAIN_REMOVE <peer>   Clear trained thresholds (revert to defaults).
 *   OTA_NOW               Wake the OTA poller and check manifest immediately.
 *   REBOOT                esp_restart() — useful for stuck FSMs.
 *   PING                  Send back "PONG" telemetry line. Connectivity check.
 *
 * Replies come back over the regular telemetry UDP channel as:
 *
 *   <board_mac> CTRL <cmd> ok|err [detail]
 *
 * Port is set via menuconfig (CONFIG_RS_CONTROL_PORT, default 5052).
 */
#pragma once

#include "esp_wifi_sensing.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the UDP listener task. Must be called after Wi-Fi STA + rs_telemetry
 * are up. The fsm handle is the same one passed to esp_wifi_sensing_fsm_create. */
void rs_control_start(esp_wifi_sensing_fsm_handle_t fsm);

/* Register the demo's peer-name-to-MAC map so commands like
 * "TRAIN_START AP" can resolve the right MAC. */
typedef struct {
    const char *name;       /* e.g. "AP", "MAC_1", "MAC_2" */
    uint8_t mac[6];
} rs_control_peer_t;

void rs_control_set_peers(const rs_control_peer_t *peers, int count);

#ifdef __cplusplus
}
#endif
