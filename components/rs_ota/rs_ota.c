/*
 * Room Sense — pull-based HTTP OTA updater.
 *
 * Implementation notes:
 *
 *  - We compare versions with a simple string equality against the running
 *    image's project_ver (set automatically from git describe). Anything
 *    other than an exact match triggers an update. Good enough for our
 *    single-author / linear-release workflow; can be replaced with semver
 *    parsing later if needed.
 *
 *  - We use esp_https_ota's update_partition_via_http() with HTTP (not HTTPS)
 *    because we run on the home LAN. A future hardening pass should switch
 *    to TLS once the backend has a cert and ESP-IDF's bundled CA store is
 *    enabled — see CONFIG_ESP_TLS_USE_GLOBAL_CA_STORE.
 *
 *  - All transient errors (manifest unreachable, parse failures, download
 *    aborted) are treated as "skip this cycle and retry later" rather than
 *    crashing the task. We don't want a flaky network to take down a board.
 */
#include "rs_ota.h"
#include "rs_telemetry.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "sdkconfig.h"

#ifndef CONFIG_RS_OTA_MANIFEST_URL
#define CONFIG_RS_OTA_MANIFEST_URL "http://10.0.0.70:5050/firmware/manifest.json"
#endif
#ifndef CONFIG_RS_OTA_CHECK_INTERVAL_S
#define CONFIG_RS_OTA_CHECK_INTERVAL_S 3600
#endif

static const char *TAG = "rs_ota";

/* Binary semaphore used by rs_ota_trigger() to wake the polling loop
 * earlier than its configured interval. Counting semaphore with max=1
 * gives us "at most one pending wake" semantics. */
static SemaphoreHandle_t s_wake_sem = NULL;

/* Naive JSON field extractor — pulls "<key>": "<value>" out of the manifest.
 * Good enough for the tiny manifest schema; if it grows we'll swap in cJSON. */
static bool extract_json_string(const char *json, const char *key,
                                char *out, size_t out_len)
{
    char needle[64];
    int n = snprintf(needle, sizeof(needle), "\"%s\"", key);
    if (n <= 0 || n >= (int) sizeof(needle)) return false;

    const char *p = strstr(json, needle);
    if (!p) return false;
    p = strchr(p + n, ':');
    if (!p) return false;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '"') return false;
    p++;

    const char *end = strchr(p, '"');
    if (!end) return false;

    size_t len = (size_t) (end - p);
    if (len >= out_len) len = out_len - 1;
    memcpy(out, p, len);
    out[len] = '\0';
    return true;
}

static esp_err_t fetch_manifest(char *body, size_t body_len)
{
    esp_http_client_config_t cfg = {
        .url = CONFIG_RS_OTA_MANIFEST_URL,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    int content_len = esp_http_client_fetch_headers(client);
    if (content_len <= 0 || content_len >= (int) body_len) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    int read_len = esp_http_client_read(client, body, content_len);
    body[read_len > 0 ? read_len : 0] = '\0';

    int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGW(TAG, "manifest HTTP %d", status);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void rs_ota_task(void *arg)
{
    (void) arg;

    /* Let Wi-Fi settle for the first check. */
    vTaskDelay(pdMS_TO_TICKS(30000));

    const esp_app_desc_t *running = esp_app_get_description();
    ESP_LOGI(TAG, "running version: %s", running->version);
    rs_telemetry_send("OTA", "running=%s", running->version);

    while (1) {
        char body[512];
        if (fetch_manifest(body, sizeof(body)) == ESP_OK) {
            char remote_version[32] = {0};
            char remote_url[256] = {0};
            if (extract_json_string(body, "version", remote_version, sizeof(remote_version)) &&
                extract_json_string(body, "url", remote_url, sizeof(remote_url))) {
                ESP_LOGI(TAG, "manifest: version=%s url=%s", remote_version, remote_url);
                if (strcmp(remote_version, running->version) != 0) {
                    ESP_LOGI(TAG, "new version available, starting OTA");
                    rs_telemetry_send("OTA", "update available=%s -> %s",
                                      running->version, remote_version);

                    esp_http_client_config_t http_cfg = {
                        .url = remote_url,
                        .timeout_ms = 30000,
                        .keep_alive_enable = true,
                    };
                    esp_https_ota_config_t ota_cfg = {
                        .http_config = &http_cfg,
                    };
                    esp_err_t result = esp_https_ota(&ota_cfg);
                    if (result == ESP_OK) {
                        rs_telemetry_send("OTA", "success rebooting");
                        ESP_LOGI(TAG, "OTA OK, rebooting");
                        vTaskDelay(pdMS_TO_TICKS(2000));
                        esp_restart();
                    } else {
                        rs_telemetry_send("OTA", "failed err=0x%x", result);
                        ESP_LOGE(TAG, "OTA failed: 0x%x", result);
                    }
                }
            }
        }
        /* Wait for either the periodic interval to elapse OR an explicit
         * wake from rs_ota_trigger(). xSemaphoreTake returns pdTRUE on
         * the trigger path, pdFALSE on timeout — we don't care which, the
         * loop iterates either way. */
        xSemaphoreTake(s_wake_sem, pdMS_TO_TICKS(CONFIG_RS_OTA_CHECK_INTERVAL_S * 1000));
    }
}

void rs_ota_start(void)
{
    if (s_wake_sem == NULL) {
        s_wake_sem = xSemaphoreCreateBinary();
    }
    xTaskCreate(rs_ota_task, "rs_ota", 8192, NULL, 1, NULL);
}

void rs_ota_trigger(void)
{
    if (s_wake_sem) {
        xSemaphoreGive(s_wake_sem);
    }
}
