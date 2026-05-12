idf_component_register(SRCS "rs_control.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rs_telemetry rs_ota esp_wifi_sensing lwip)
