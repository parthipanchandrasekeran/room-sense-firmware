/*
 * Room Sense — periodic presence + wander reporter.
 *
 * Polls the esp_wifi_sensing FSM channel diagnostics for each registered
 * peer and emits two new telemetry line types:
 *
 *   PRESENCE <channel> <static:0|1> <wander_avg> <jitter>     @ 1 Hz per channel
 *     - "channel" is "AP" / "MAC_1" / "MAC_2" (set via rs_presence_set_peers)
 *     - "static" is presence_someone_status — true when the FSM thinks
 *       someone is in the room regardless of whether they're moving
 *     - wander_avg, jitter are diagnostic numbers
 *
 *   WANDER <wander_value>                                      @ 5 Hz, AP only
 *     - For breathing-rate FFT on the backend. Higher rate because we need
 *       enough samples to resolve respiratory frequency (~12-20 breaths/min)
 *
 * STATE events (active/inactive) are still emitted from app_main's
 * on_motion_event callback — this component is purely additive.
 */
#pragma once

#include "esp_wifi_sensing.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    uint8_t mac[6];
} rs_presence_peer_t;

/* Register the peer-name → MAC map. Pass the same array as rs_control_set_peers. */
void rs_presence_set_peers(const rs_presence_peer_t *peers, int count);

/* Start the periodic reporter task. Safe to call after WiFi STA up and
 * after FSM creation. */
void rs_presence_start(esp_wifi_sensing_fsm_handle_t fsm);

#ifdef __cplusplus
}
#endif
