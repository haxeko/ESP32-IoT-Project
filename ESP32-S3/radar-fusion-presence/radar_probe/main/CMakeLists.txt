idf_component_register(SRCS "probe_main.cpp" "probe_radar.cpp" "probe_out.cpp"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES radar_proto esp_driver_uart esp_driver_gpio esp_driver_usb_serial_jtag
                                     esp_timer esp_app_format esp_ringbuf spi_flash vfs)
