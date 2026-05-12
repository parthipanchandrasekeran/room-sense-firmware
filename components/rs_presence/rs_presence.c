/*
 * Room Sense — periodic presence + wander reporter.
 *
 * Two tasks:
 *   1) rs_presence_task @ 1 Hz: polls esp_wifi_sensing_fsm_get_channel_diag
 *      for each registered peer and emits one PRESENCE line per channel
 *      (3 lines/sec total at the default 3-peer configuration).
 *   2) rs_wander_task @ 5 Hz: reads only the AP channel's wander_value and
 *      emits a single WANDER line per sample. The 5 Hz cadence is enough
 *      to capture breathing frequencies (typically 0.2-0.4 Hz) via FFT on
 *      the backend.
 *
 * Both tasks share a single FSM handle and a peer table set via
 * rs_presence_set_peers(). The handle is stored in a static so the tasks
 * can pick it up without their xTaskCreate arg.
 */
#include "rs_presence.h"
#include "rs_telemetry.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "rs_presence";

static esp_wifi_sensing_fsm_handle_t s_fsm = NULL;
static const rs_presence_peer_t *s_peers = NULL;
static int s_peer_count = 0;

void rs_presence_set_peers(const rs_presence_peer_t *peers, int count)
{
    s_peers = peers;
    s_peer_count = count;
}

static void rs_presence_task(void *arg)
{
    (void) arg;
    /* Wait for the FSM baseline to stabilize before reporting. */
    vTaskDelay(pdMS_TO_TICKS(15000));

    while (1) {
        for (int i = 0; i < s_peer_count; i++) {
            esp_wifi_sensing_fsm_channel_diag_t diag = {0};
            esp_err_t err = esp_wifi_sensing_fsm_get_channel_diag(
                s_fsm, s_peers[i].mac, &diag);
            if (err != ESP_OK) continue;

            rs_telemetry_send("PRESENCE", "%s %d %.6f %.6f",
                              s_peers[i].name,
                              diag.presence_someone_status ? 1 : 0,
                              diag.presence_wander_average,
                              diag.jitter_value);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void rs_wander_task(void *arg)
{
    (void) arg;
    /* Wait for baseline. */
    vTaskDelay(pdMS_TO_TICKS(15000));

    /* AP channel is always the first peer by convention (see app_main). */
    while (1) {
        if (s_peer_count > 0) {
            esp_wifi_sensing_fsm_channel_diag_t diag = {0};
            if (esp_wifi_sensing_fsm_get_channel_diag(s_fsm, s_peers[0].mac, &diag) == ESP_OK) {
                rs_telemetry_send("WANDER", "%.6f", diag.wander_value);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));  /* 5 Hz */
    }
}

void rs_presence_start(esp_wifi_sensing_fsm_handle_t fsm)
{
    s_fsm = fsm;
    xTaskCreate(rs_presence_task, "rs_presence", 4096, NULL, 1, NULL);
    xTaskCreate(rs_wander_task,   "rs_wander",   4096, NULL, 1, NULL);
    ESP_LOGI(TAG, "started (1 Hz PRESENCE per channel, 5 Hz WANDER on AP)");
}
