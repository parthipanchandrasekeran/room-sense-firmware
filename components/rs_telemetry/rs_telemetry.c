/*
 * Room Sense — UDP telemetry transport.
 *
 * Ported from the previous fork's csi_component.h (udp_csi_init,
 * udp_csi_send, udp_send_typed) but reshaped as a proper component with a
 * public API and menuconfig-driven destination.
 */
#include "rs_telemetry.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "esp_log.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#ifndef CONFIG_RS_TELEMETRY_HOST_IP
#define CONFIG_RS_TELEMETRY_HOST_IP "10.0.0.70"
#endif
#ifndef CONFIG_RS_TELEMETRY_HOST_PORT
#define CONFIG_RS_TELEMETRY_HOST_PORT 5051
#endif

static const char *TAG = "rs_telemetry";

static int s_sock = -1;
static char s_board_mac[18] = {0};
static struct sockaddr_in s_dst = {0};

int rs_telemetry_init(void)
{
    if (s_sock >= 0) return 0;  // already initialised

    uint8_t mac[6];
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != 0) {
        ESP_LOGE(TAG, "esp_wifi_get_mac failed: %d", err);
        return -1;
    }
    snprintf(s_board_mac, sizeof(s_board_mac),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    s_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sock < 0) {
        ESP_LOGE(TAG, "socket() failed");
        return -1;
    }

    s_dst.sin_family = AF_INET;
    s_dst.sin_port = htons(CONFIG_RS_TELEMETRY_HOST_PORT);
    inet_aton(CONFIG_RS_TELEMETRY_HOST_IP, &s_dst.sin_addr);

    ESP_LOGI(TAG, "ready: board=%s -> %s:%d",
             s_board_mac, CONFIG_RS_TELEMETRY_HOST_IP,
             (int) CONFIG_RS_TELEMETRY_HOST_PORT);
    return 0;
}

bool rs_telemetry_ready(void)
{
    return s_sock >= 0;
}

void rs_telemetry_send(const char *type, const char *fmt, ...)
{
    if (s_sock < 0) return;
    char buf[2048];
    int prefix = snprintf(buf, sizeof(buf), "%s %s ", s_board_mac, type);
    if (prefix < 0 || prefix >= (int) sizeof(buf)) return;

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + prefix, sizeof(buf) - prefix, fmt, ap);
    va_end(ap);

    if (n > 0 && prefix + n < (int) sizeof(buf)) {
        sendto(s_sock, buf, prefix + n, 0,
               (struct sockaddr *) &s_dst, sizeof(s_dst));
    }
}
