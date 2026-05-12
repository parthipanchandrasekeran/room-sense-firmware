idf_component_register(SRCS "rs_radar.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rs_telemetry driver esp_timer)
