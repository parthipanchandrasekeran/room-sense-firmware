/*
 * Room Sense — HLK-LD2410C 24 GHz mmWave radar driver.
 *
 * The radar emits binary frames over UART at 256000 baud by default.
 * Standard data frame layout (little-endian for multi-byte fields):
 *
 *   F4 F3 F2 F1            header (4 bytes)
 *   LL LL                  intra-frame length, LE
 *   02                     "report" type marker
 *   AA                     "head" marker
 *   ST                     target state: 0/1/2/3
 *   MD MD                  moving distance, cm, LE
 *   ME                     moving energy, 0..100
 *   SD SD                  static distance, cm, LE
 *   SE                     static energy, 0..100
 *   DD DD                  detection distance, cm, LE
 *   55                     tail marker
 *   00                     reserved
 *   F8 F7 F6 F5            footer (4 bytes)
 *
 * We scan the byte stream for the 0xF4 0xF3 0xF2 0xF1 header, parse the
 * payload, validate the 0xF8 0xF7 0xF6 0xF5 footer, then publish.
 *
 * Rate limit: the radar emits ~10 Hz. We dedupe consecutive identical
 * states and emit at most once per second to keep UDP volume sane.
 */
#include "rs_radar.h"
#include "rs_telemetry.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#ifndef CONFIG_RS_RADAR_UART_NUM
#define CONFIG_RS_RADAR_UART_NUM 1
#endif
#ifndef CONFIG_RS_RADAR_RX_GPIO
#define CONFIG_RS_RADAR_RX_GPIO 18
#endif
#ifndef CONFIG_RS_RADAR_TX_GPIO
#define CONFIG_RS_RADAR_TX_GPIO 17
#endif
#ifndef CONFIG_RS_RADAR_BAUD
/* LD2410C ships at 256000 baud by default. Some clones / older units
 * ship at 115200. If you see "RADAR_DEBUG bytes=0" forever, try 115200
 * here. We rotate between the two during diagnostics: see rs_radar_task
 * which cycles through candidate baud rates until one yields bytes. */
#define CONFIG_RS_RADAR_BAUD 256000
#endif

/* Diagnostic: candidate baud rates to try when no bytes are arriving.
 * Once any rate yields data, the task locks to it. */
static const int CANDIDATE_BAUDS[] = {256000, 115200, 230400, 9600};
#define N_CANDIDATE_BAUDS (sizeof(CANDIDATE_BAUDS) / sizeof(CANDIDATE_BAUDS[0]))

static const char *TAG = "rs_radar";

typedef struct {
    uint8_t state;
    uint16_t move_cm;
    uint8_t move_e;
    uint16_t static_cm;
    uint8_t static_e;
    uint16_t detect_cm;
} radar_frame_t;

static bool find_header_and_parse(const uint8_t *buf, int n, int *consumed_out,
                                  radar_frame_t *out)
{
    /* Need at least: 4 hdr + 2 len + 9 payload + 4 footer = 19 bytes minimum. */
    for (int i = 0; i + 19 <= n; i++) {
        if (buf[i] == 0xF4 && buf[i + 1] == 0xF3 &&
            buf[i + 2] == 0xF2 && buf[i + 3] == 0xF1) {
            uint16_t length = (uint16_t) buf[i + 4] | ((uint16_t) buf[i + 5] << 8);
            int frame_end = i + 6 + length + 4;   /* + footer */
            if (frame_end > n) {
                /* Need more data; tell caller how much we already consumed
                 * before the header so we don't re-scan it. */
                *consumed_out = i;
                return false;
            }
            /* Validate footer. */
            const uint8_t *foot = &buf[i + 6 + length];
            if (foot[0] != 0xF8 || foot[1] != 0xF7 ||
                foot[2] != 0xF6 || foot[3] != 0xF5) {
                /* Bad frame — skip this header and keep scanning. */
                continue;
            }
            /* Payload starts at i+6. Format: 02 AA <state> ... 55 00. */
            const uint8_t *p = &buf[i + 6];
            if (length < 13 || p[0] != 0x02 || p[1] != 0xAA) {
                continue;
            }
            out->state     = p[2];
            out->move_cm   = (uint16_t) p[3] | ((uint16_t) p[4] << 8);
            out->move_e    = p[5];
            out->static_cm = (uint16_t) p[6] | ((uint16_t) p[7] << 8);
            out->static_e  = p[8];
            out->detect_cm = (uint16_t) p[9] | ((uint16_t) p[10] << 8);
            *consumed_out = frame_end;
            return true;
        }
    }
    /* No header found in the buffer; discard everything but the last 3
     * bytes (which might be the start of a header straddling reads). */
    *consumed_out = (n > 3) ? (n - 3) : 0;
    return false;
}

static void rs_radar_task(void *arg)
{
    (void) arg;

    /* Driver: 2 KB rx buffer, no tx buffer, no event queue. */
    if (uart_driver_install(CONFIG_RS_RADAR_UART_NUM, 2048, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed");
        vTaskDelete(NULL);
        return;
    }

    /* Start at the configured (or default) baud rate. If we don't see
     * any bytes within 8 seconds, cycle to the next candidate. Once a
     * baud rate yields data we stick with it forever. */
    int baud_idx = 0;
    uart_config_t cfg = {
        .baud_rate = CANDIDATE_BAUDS[baud_idx],
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(CONFIG_RS_RADAR_UART_NUM, &cfg);
    uart_set_pin(CONFIG_RS_RADAR_UART_NUM,
                 CONFIG_RS_RADAR_TX_GPIO, CONFIG_RS_RADAR_RX_GPIO,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ESP_LOGI(TAG, "started: uart%d rx=%d tx=%d @%d baud",
             CONFIG_RS_RADAR_UART_NUM,
             CONFIG_RS_RADAR_RX_GPIO, CONFIG_RS_RADAR_TX_GPIO,
             CANDIDATE_BAUDS[baud_idx]);
    rs_telemetry_send("RADAR", "init uart=%d rx=%d tx=%d baud=%d",
                      CONFIG_RS_RADAR_UART_NUM,
                      CONFIG_RS_RADAR_RX_GPIO, CONFIG_RS_RADAR_TX_GPIO,
                      CANDIDATE_BAUDS[baud_idx]);
    int64_t baud_started_us = esp_timer_get_time();
    bool baud_locked = false;

    uint8_t buf[512];
    int filled = 0;
    int64_t last_emit_us = 0;
    uint8_t last_state = 0xFF;     /* sentinel — force first emit */

    /* Debug: count bytes received so we can tell wiring issues
     * (bytes=0 means nothing arriving) from parsing issues
     * (bytes>0 but no frames). Emit one DEBUG line every 5 sec. */
    uint32_t total_bytes = 0;
    int64_t last_debug_us = esp_timer_get_time();
    uint8_t first_bytes[16] = {0};
    int first_bytes_n = 0;

    while (1) {
        int got = uart_read_bytes(CONFIG_RS_RADAR_UART_NUM,
                                  buf + filled, sizeof(buf) - filled,
                                  pdMS_TO_TICKS(200));
        if (got < 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        /* Baud-rotation diagnostic: if no bytes after 8 sec, try next baud. */
        if (!baud_locked) {
            if (got > 0) {
                baud_locked = true;
                rs_telemetry_send("RADAR_DEBUG", "baud-locked=%d", CANDIDATE_BAUDS[baud_idx]);
            } else if (esp_timer_get_time() - baud_started_us > 8000000) {
                baud_idx = (baud_idx + 1) % N_CANDIDATE_BAUDS;
                cfg.baud_rate = CANDIDATE_BAUDS[baud_idx];
                uart_param_config(CONFIG_RS_RADAR_UART_NUM, &cfg);
                rs_telemetry_send("RADAR_DEBUG", "trying baud=%d", CANDIDATE_BAUDS[baud_idx]);
                baud_started_us = esp_timer_get_time();
                /* reset diagnostics so we capture fresh bytes from new baud */
                first_bytes_n = 0;
                memset(first_bytes, 0, sizeof(first_bytes));
            }
        }

        if (got > 0) {
            total_bytes += got;
            /* Stash the very first bytes we ever see for diagnostic dump. */
            while (first_bytes_n < (int) sizeof(first_bytes) && first_bytes_n < got + (int)(filled)) {
                int src_idx = filled + first_bytes_n - first_bytes_n; // simpler: read fresh
                if (first_bytes_n < got) {
                    first_bytes[first_bytes_n] = buf[filled + first_bytes_n];
                }
                first_bytes_n++;
                if (first_bytes_n >= (int) sizeof(first_bytes)) break;
            }
        }
        filled += got;

        /* Debug emit every 5 sec — total bytes + first hex sample */
        int64_t now_dbg = esp_timer_get_time();
        if (now_dbg - last_debug_us > 5000000) {
            char hex[64] = {0};
            int off = 0;
            for (int i = 0; i < first_bytes_n && off < (int) sizeof(hex) - 4; i++) {
                off += snprintf(hex + off, sizeof(hex) - off, "%02x", first_bytes[i]);
            }
            rs_telemetry_send("RADAR_DEBUG", "bytes=%u first=%s",
                              (unsigned) total_bytes, hex);
            last_debug_us = now_dbg;
        }

        /* Parse all complete frames in the buffer. */
        while (filled > 0) {
            int consumed = 0;
            radar_frame_t f = {0};
            bool ok = find_header_and_parse(buf, filled, &consumed, &f);
            if (consumed > 0) {
                memmove(buf, buf + consumed, filled - consumed);
                filled -= consumed;
            }
            if (!ok) break;

            /* Decide whether to emit: state changed OR >=1 sec since last. */
            int64_t now_us = esp_timer_get_time();
            bool state_changed = (f.state != last_state);
            bool throttle_passed = (now_us - last_emit_us) >= 1000000;
            if (state_changed || throttle_passed) {
                rs_telemetry_send("RADAR", "%d %u %u %u %u %u",
                                  f.state,
                                  f.move_cm, f.move_e,
                                  f.static_cm, f.static_e,
                                  f.detect_cm);
                last_state = f.state;
                last_emit_us = now_us;
            }
        }

        /* Safety: if the buffer is full and we couldn't find a header,
         * drop everything. Indicates either no radar wired up OR garbage. */
        if (filled == (int) sizeof(buf)) {
            ESP_LOGW(TAG, "buffer full without valid frame; flushing");
            filled = 0;
        }
    }
}

void rs_radar_start(void)
{
    /* 8 KB stack: 512-byte read buffer + UART driver state + rs_telemetry
     * printf paths. 4 KB overflowed on Living Room (caught via panic
     * capture: 'A stack overflow in task rs_radar has been detected').
     * Same lesson as rs_ota — be generous with stacks for tasks doing
     * UART I/O + telemetry. */
    xTaskCreate(rs_radar_task, "rs_radar", 8192, NULL, 1, NULL);
}
