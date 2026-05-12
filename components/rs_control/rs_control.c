/*
 * Room Sense — remote control UDP listener.
 *
 * Lives in its own FreeRTOS task; receives one datagram, dispatches by
 * keyword, sends a reply via rs_telemetry. No fancy parser — the wire
 * grammar is "<verb> [<arg>]" so a single strtok pass is enough.
 */
#include "rs_control.h"
#include "rs_telemetry.h"
#include "rs_ota.h"

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <sys/socket.h>
#include <netinet/in.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"

#ifndef CONFIG_RS_CONTROL_PORT
#define CONFIG_RS_CONTROL_PORT 5052
#endif

static const char *TAG = "rs_control";

static esp_wifi_sensing_fsm_handle_t s_fsm = NULL;
static const rs_control_peer_t *s_peers = NULL;
static int s_peer_count = 0;

void rs_control_set_peers(const rs_control_peer_t *peers, int count)
{
    s_peers = peers;
    s_peer_count = count;
}

/* Resolve a peer alias ("AP", "MAC_1") OR full MAC string to a 6-byte MAC.
 * Returns true on success. */
static bool resolve_peer(const char *name, uint8_t out[6])
{
    if (!name || !*name) return false;
    for (int i = 0; i < s_peer_count; i++) {
        if (strcasecmp(name, s_peers[i].name) == 0) {
            memcpy(out, s_peers[i].mac, 6);
            return true;
        }
    }
    /* fall back to parsing as full MAC like AA:BB:CC:DD:EE:FF */
    unsigned int b[6];
    if (sscanf(name, "%2x:%2x:%2x:%2x:%2x:%2x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; i++) out[i] = (uint8_t) b[i];
        return true;
    }
    return false;
}

static void handle_train_start(const char *peer_name)
{
    if (!s_fsm) {
        rs_telemetry_send("CTRL", "TRAIN_START err fsm-not-ready");
        return;
    }
    uint8_t mac[6];
    if (!resolve_peer(peer_name, mac)) {
        rs_telemetry_send("CTRL", "TRAIN_START err peer-not-found");
        return;
    }
    esp_err_t err = esp_wifi_sensing_fsm_train_start(s_fsm, mac);
    rs_telemetry_send("CTRL", "TRAIN_START %s peer=%s",
                      err == ESP_OK ? "ok" : "err", peer_name);
}

static void handle_train_stop(const char *peer_name)
{
    if (!s_fsm) {
        rs_telemetry_send("CTRL", "TRAIN_STOP err fsm-not-ready");
        return;
    }
    uint8_t mac[6];
    if (!resolve_peer(peer_name, mac)) {
        rs_telemetry_send("CTRL", "TRAIN_STOP err peer-not-found");
        return;
    }
    float wander = 0.0f, jitter = 0.0f;
    esp_err_t err = esp_wifi_sensing_fsm_train_stop(s_fsm, mac, &wander, &jitter);
    if (err == ESP_OK) {
        rs_telemetry_send("CTRL", "TRAIN_STOP ok peer=%s wander=%.4f jitter=%.4f",
                          peer_name, wander, jitter);
    } else {
        rs_telemetry_send("CTRL", "TRAIN_STOP err code=0x%x", err);
    }
}

static void handle_train_remove(const char *peer_name)
{
    if (!s_fsm) return;
    uint8_t mac[6];
    if (!resolve_peer(peer_name, mac)) {
        rs_telemetry_send("CTRL", "TRAIN_REMOVE err peer-not-found");
        return;
    }
    esp_err_t err = esp_wifi_sensing_fsm_train_remove(s_fsm, mac);
    rs_telemetry_send("CTRL", "TRAIN_REMOVE %s peer=%s",
                      err == ESP_OK ? "ok" : "err", peer_name);
}

static void handle_ota_now(void)
{
    rs_telemetry_send("CTRL", "OTA_NOW ok triggering");
    rs_ota_trigger();
}

static void handle_reboot(void)
{
    rs_telemetry_send("CTRL", "REBOOT ok in-2-sec");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();
}

static void dispatch(const char *line)
{
    /* Make a mutable copy for strtok. */
    char buf[160];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Strip trailing whitespace including CR/LF that some clients append. */
    int len = (int) strlen(buf);
    while (len > 0 && isspace((unsigned char) buf[len - 1])) buf[--len] = '\0';

    if (len == 0) return;

    char *saveptr = NULL;
    char *verb = strtok_r(buf, " ", &saveptr);
    if (!verb) return;
    char *arg = strtok_r(NULL, " ", &saveptr);

    ESP_LOGI(TAG, "cmd: %s %s", verb, arg ? arg : "");

    if (strcasecmp(verb, "TRAIN_START") == 0) {
        handle_train_start(arg);
    } else if (strcasecmp(verb, "TRAIN_STOP") == 0) {
        handle_train_stop(arg);
    } else if (strcasecmp(verb, "TRAIN_REMOVE") == 0) {
        handle_train_remove(arg);
    } else if (strcasecmp(verb, "OTA_NOW") == 0) {
        handle_ota_now();
    } else if (strcasecmp(verb, "REBOOT") == 0) {
        handle_reboot();
    } else if (strcasecmp(verb, "PING") == 0) {
        rs_telemetry_send("CTRL", "PING pong");
    } else {
        rs_telemetry_send("CTRL", "UNKNOWN verb=%s", verb);
    }
}

static void rs_control_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {0};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(CONFIG_RS_CONTROL_PORT);
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr)) != 0) {
        ESP_LOGE(TAG, "bind on %d failed", CONFIG_RS_CONTROL_PORT);
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "listening on UDP %d", CONFIG_RS_CONTROL_PORT);
    rs_telemetry_send("CTRL", "ready port=%d", CONFIG_RS_CONTROL_PORT);

    char buf[256];
    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);

    while (1) {
        int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr *) &src, &src_len);
        if (n <= 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        buf[n] = '\0';
        dispatch(buf);
    }
}

void rs_control_start(esp_wifi_sensing_fsm_handle_t fsm)
{
    s_fsm = fsm;
    /* 6 KB stack: handlers may chain into FSM APIs that themselves allocate.
     * Conservative; the listener itself is small. */
    xTaskCreate(rs_control_task, "rs_control", 6144, NULL, 2, NULL);
}
