/*
 * Room Sense — crash uploader.
 *
 * Reads the coredump partition (configured in partitions.csv as "coredump")
 * in 4 KB chunks and PUTs the bytes to /api/coredump/<board_mac>. The HTTP
 * client uses chunked transfer so we don't have to buffer the whole dump.
 * On a successful upload we call esp_core_dump_image_erase() so the same
 * dump won't be re-uploaded on every reboot.
 *
 * No-op (warns and returns) if no dump is present.
 */
#include "rs_crash.h"
#include "rs_telemetry.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_core_dump.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "sdkconfig.h"

#ifndef CONFIG_RS_CRASH_UPLOAD_URL_PREFIX
#define CONFIG_RS_CRASH_UPLOAD_URL_PREFIX "http://10.0.0.70:5050/api/coredump"
#endif

static const char *TAG = "rs_crash";

void rs_crash_upload_if_present(void)
{
    size_t addr = 0, size = 0;
    esp_err_t err = esp_core_dump_image_get(&addr, &size);
    if (err == ESP_ERR_NOT_FOUND || size == 0) {
        ESP_LOGI(TAG, "no coredump present");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_core_dump_image_get returned 0x%x", err);
        return;
    }

    /* Read in chunks straight from flash via esp_partition_read (rather than
     * mmap-ing the whole image) so we keep RAM usage flat. */
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                           ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                                           NULL);
    if (!part) {
        ESP_LOGW(TAG, "coredump partition not found");
        return;
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char url[160];
    snprintf(url, sizeof(url),
             "%s/%02x:%02x:%02x:%02x:%02x:%02x",
             CONFIG_RS_CRASH_UPLOAD_URL_PREFIX,
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "uploading %u-byte coredump to %s", (unsigned) size, url);
    rs_telemetry_send("CRASH", "uploading bytes=%u", (unsigned) size);

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "http_client_init failed");
        rs_telemetry_send("CRASH", "upload-failed client-init");
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    if (esp_http_client_open(client, (int) size) != ESP_OK) {
        ESP_LOGE(TAG, "http_client_open failed");
        esp_http_client_cleanup(client);
        rs_telemetry_send("CRASH", "upload-failed open");
        return;
    }

    uint8_t buf[1024];
    size_t off = 0;
    while (off < size) {
        size_t want = (size - off > sizeof(buf)) ? sizeof(buf) : (size - off);
        if (esp_partition_read(part, off, buf, want) != ESP_OK) {
            ESP_LOGE(TAG, "partition_read failed at offset %u", (unsigned) off);
            break;
        }
        int wrote = esp_http_client_write(client, (const char *) buf, want);
        if (wrote != (int) want) {
            ESP_LOGE(TAG, "http write short at off=%u (got %d expected %u)",
                     (unsigned) off, wrote, (unsigned) want);
            break;
        }
        off += want;
    }

    int status = esp_http_client_fetch_headers(client);
    int code = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (off == size && (code >= 200 && code < 300)) {
        ESP_LOGI(TAG, "upload ok (HTTP %d), erasing coredump", code);
        rs_telemetry_send("CRASH", "upload-ok bytes=%u erased=1", (unsigned) size);
        esp_core_dump_image_erase();
    } else {
        ESP_LOGW(TAG, "upload incomplete: off=%u/%u status_fetch=%d code=%d",
                 (unsigned) off, (unsigned) size, status, code);
        rs_telemetry_send("CRASH", "upload-partial off=%u code=%d",
                          (unsigned) off, code);
        /* leave coredump in flash so we retry next boot */
    }
}
