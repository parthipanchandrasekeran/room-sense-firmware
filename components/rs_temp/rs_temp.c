/*
 * Room Sense — on-chip temperature reporter.
 *
 * Ported from the previous fork's cpu_temp_task in csi_component.h. Now
 * a proper component, but the behaviour is identical: install + enable
 * the temperature sensor, then publish a TEMP line every 10 seconds.
 *
 * ESP32 classic does not expose a usable on-chip temperature sensor via
 * this API, so we compile to a no-op there.
 */
#include "rs_temp.h"
#include "rs_telemetry.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2) || \
    defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define RS_TEMP_HAS_SENSOR 1
#include "driver/temperature_sensor.h"
#else
#define RS_TEMP_HAS_SENSOR 0
#endif

static const char *TAG = "rs_temp";

#if RS_TEMP_HAS_SENSOR

static temperature_sensor_handle_t s_handle = NULL;

static void rs_temp_task(void *arg)
{
    (void) arg;
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80);
    if (temperature_sensor_install(&cfg, &s_handle) != ESP_OK) {
        ESP_LOGE(TAG, "install failed");
        vTaskDelete(NULL);
        return;
    }
    if (temperature_sensor_enable(s_handle) != ESP_OK) {
        ESP_LOGE(TAG, "enable failed");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "started");
    while (1) {
        float c = 0.0f;
        if (temperature_sensor_get_celsius(s_handle, &c) == ESP_OK) {
            rs_telemetry_send("TEMP", "%.2f", c);
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void rs_temp_start(void)
{
    /* 6 KB stack: the IDF v5.5 temperature_sensor driver pulls in more
     * stack than v4's. Saw stack overflow with 3072 — bumped to 6144. */
    xTaskCreate(rs_temp_task, "rs_temp", 6144, NULL, 1, NULL);
}

#else  /* !RS_TEMP_HAS_SENSOR */

void rs_temp_start(void)
{
    ESP_LOGW(TAG, "no on-chip temperature sensor available for this target");
}

#endif
