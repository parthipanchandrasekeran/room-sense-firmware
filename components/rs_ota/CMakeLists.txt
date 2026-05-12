idf_component_register(SRCS "rs_ota.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rs_telemetry app_update esp_http_client esp_https_ota)
