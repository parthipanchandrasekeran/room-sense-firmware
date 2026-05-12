set(reqs esp_wifi_sensing
         protocol_examples_common
         driver
         esp_driver_ledc
         led_strip
         rs_telemetry
         rs_temp
         rs_wifi_neighbor
         rs_ble_scanner
         rs_ota
         rs_control
         rs_presence
         rs_crash)

idf_component_register(SRCS "app_main.c"
                            "led_control.c"
                            "web_serial_monitor.c"
                       INCLUDE_DIRS "."
                       REQUIRES ${reqs}
                       PRIV_REQUIRES esp_driver_usb_serial_jtag)
