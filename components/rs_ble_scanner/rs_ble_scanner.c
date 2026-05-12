/*
 * Room Sense — passive BLE scanner.
 *
 * Direct port of ble_scanner.h from the previous fork. NimBLE config
 * unchanged: passive mode, no duplicate filtering, no GATT server.
 *
 * The scan runs forever (BLE_HS_FOREVER). Every advert callback emits a
 * "BLE" line via rs_telemetry.
 */
#include "rs_ble_scanner.h"
#include "rs_telemetry.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

static const char *TAG = "rs_ble";
static uint8_t s_own_addr_type = 0;

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    (void) arg;
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    char addr_str[18];
    snprintf(addr_str, sizeof(addr_str),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             event->disc.addr.val[5], event->disc.addr.val[4],
             event->disc.addr.val[3], event->disc.addr.val[2],
             event->disc.addr.val[1], event->disc.addr.val[0]);
    int rssi = (int) event->disc.rssi;

    /* Try to extract Complete (0x09) or Shortened (0x08) Local Name from
     * the advertisement data. BLE adv data is (length, type, data...). */
    char name_buf[33] = {0};
    const uint8_t *adv = event->disc.data;
    int len = event->disc.length_data;
    int i = 0;
    while (i < len) {
        int field_len = adv[i];
        if (field_len == 0 || i + field_len >= len) break;
        int field_type = adv[i + 1];
        if (field_type == 0x09 || field_type == 0x08) {
            int name_len = field_len - 1;
            if (name_len > 32) name_len = 32;
            memcpy(name_buf, &adv[i + 2], name_len);
            name_buf[name_len] = '\0';
            /* strip non-printable chars to keep UDP line clean */
            for (int j = 0; j < name_len; j++) {
                if (name_buf[j] < 0x20 || name_buf[j] >= 0x7f) name_buf[j] = '?';
            }
            break;
        }
        i += field_len + 1;
    }

    if (name_buf[0]) {
        rs_telemetry_send("BLE", "%s %d %s", addr_str, rssi, name_buf);
    } else {
        rs_telemetry_send("BLE", "%s %d", addr_str, rssi);
    }
    return 0;
}

static void ble_start_scan(void)
{
    struct ble_gap_disc_params disc_params = {0};
    disc_params.passive = 1;
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_duplicates = 0;  /* see every advert; backend de-dupes */
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &disc_params,
                          ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE scan started");
    }
}

static void ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ensure_addr failed: %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "infer_auto failed: %d", rc);
        return;
    }
    ble_start_scan();
}

static void ble_host_task(void *param)
{
    (void) param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void rs_ble_scanner_start(void)
{
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return;
    }
    ble_hs_cfg.sync_cb = ble_on_sync;
    /* Passive scanner only — no advertising or GATT services. We skip
     * ble_svc_gap_init() because it expects a full GATT server, which we
     * don't run. */
    nimble_port_freertos_init(ble_host_task);
    ESP_LOGI(TAG, "NimBLE host started");
}
