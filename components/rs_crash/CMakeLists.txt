idf_component_register(SRCS "rs_crash.c"
                       INCLUDE_DIRS "include"
                       REQUIRES rs_telemetry esp_http_client espcoredump esp_partition)
