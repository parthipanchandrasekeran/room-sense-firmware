/*
 * Room Sense — WiFi neighbor scanner.
 *
 * Direct port of wifi_scan_task() from the previous fork's csi_component.h,
 * carrying forward the hardening added there (stop-before-start, error
 * counter with 30 s recovery, heartbeats, esp_wifi_set_csi(1) restore).
 *
 * Output lines (via rs_telemetry):
 *   WIFI_HB <consec_errors>
 *   WIFI <bssid> <rssi> <channel> <auth_mode> <ssid_or_dash>
 *   WIFI_ERR 0x<code> <consec_errors>
 */
#include "rs_wifi_neighbor.h"
#include "rs_telemetry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "rs_wifi_neighbor";

static void rs_wifi_neighbor_task(void *arg)
{
    (void) arg;

    /* Wait for STA association before first scan. esp_wifi_scan_start
     * requires Wi-Fi to be up; otherwise the call returns an error. */
    int wait_count = 0;
    while (wait_count < 30) {
        wifi_ap_record_t info;
        if (esp_wifi_sta_get_ap_info(&info) == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
        wait_count++;
    }

    /* Stagger initial scan per-board so multiple boards don't all scan
     * at the same instant. Use last MAC byte to spread across ~50 sec. */
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    vTaskDelay(pdMS_TO_TICKS(5000 + (mac[5] % 50) * 1000));

    int consecutive_errors = 0;

    while (1) {
        /* Heartbeat: tell the backend this task is alive before each scan. */
        rs_telemetry_send("WIFI_HB", "%d", consecutive_errors);

        /* Defensive: cancel any prior scan that didn't finish.
         * esp_wifi_scan_stop() can disable CSI as a side effect, so we
         * re-enable it both before and after the scan. */
        esp_wifi_scan_stop();
        esp_wifi_set_csi(1);

        wifi_scan_config_t scan_cfg = {0};
        scan_cfg.ssid = NULL;
        scan_cfg.bssid = NULL;
        scan_cfg.channel = 0;
        scan_cfg.show_hidden = true;
        scan_cfg.scan_type = WIFI_SCAN_TYPE_PASSIVE;
        scan_cfg.scan_time.passive = 120;  /* ms per channel */

        esp_err_t err = esp_wifi_scan_start(&scan_cfg, true);
        esp_wifi_set_csi(1);  /* restore CSI after scan */

        if (err == ESP_OK) {
            consecutive_errors = 0;
            uint16_t ap_count = 0;
            esp_wifi_scan_get_ap_num(&ap_count);
            if (ap_count > 0) {
                if (ap_count > 32) ap_count = 32;
                wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
                if (records != NULL) {
                    if (esp_wifi_scan_get_ap_records(&ap_count, records) == ESP_OK) {
                        for (int i = 0; i < ap_count; i++) {
                            wifi_ap_record_t *r = &records[i];
                            /* Sanitize SSID: line-based UDP, so strip CR/LF
                             * and replace spaces with '_'. */
                            char ssid_clean[33] = {0};
                            for (int j = 0; j < 32 && r->ssid[j]; j++) {
                                char c = (char) r->ssid[j];
                                if (c >= 32 && c < 127 && c != ' ') {
                                    ssid_clean[j] = c;
                                } else if (c == ' ') {
                                    ssid_clean[j] = '_';
                                } else {
                                    ssid_clean[j] = '?';
                                }
                            }
                            char bssid_str[18];
                            snprintf(bssid_str, sizeof(bssid_str),
                                     "%02x:%02x:%02x:%02x:%02x:%02x",
                                     r->bssid[0], r->bssid[1], r->bssid[2],
                                     r->bssid[3], r->bssid[4], r->bssid[5]);
                            rs_telemetry_send("WIFI", "%s %d %d %d %s",
                                              bssid_str, r->rssi, r->primary,
                                              (int) r->authmode,
                                              ssid_clean[0] ? ssid_clean : "-");
                            vTaskDelay(pdMS_TO_TICKS(5));
                        }
                    }
                    free(records);
                }
            }
        } else {
            consecutive_errors++;
            ESP_LOGW(TAG, "scan_start failed: 0x%x (consec=%d)", err, consecutive_errors);
            rs_telemetry_send("WIFI_ERR", "0x%x %d", err, consecutive_errors);

            /* After many consecutive failures, the WiFi state may be stuck.
             * 30 s recovery delay then retry. */
            if (consecutive_errors >= 3) {
                vTaskDelay(pdMS_TO_TICKS(30000));
                consecutive_errors = 0;
                continue;
            }
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(60000));  /* next scan in 60s */
    }
}

void rs_wifi_neighbor_start(void)
{
    /* 6 KB stack: the scan-result handling allocates a wifi_ap_record_t
     * array (up to 32 entries × ~120 bytes) plus several string buffers.
     * Bumped from 4096 for safety margin on top of IDF v5.5 changes. */
    xTaskCreate(rs_wifi_neighbor_task, "rs_wifi_nbr", 6144, NULL, 1, NULL);
}
