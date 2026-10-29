# No REQUIRES: like the esp-matter examples, the main component sees every component of the build
# (esp_matter, chip, openthread, esp_driver_gpio, esp_timer, nvs_flash).
idf_component_register(SRCS              "app_main.cpp"
                                         "contact_sensor_driver.cpp"
                                         "reset_button.cpp"
                                         "status_led.cpp"
                       PRIV_INCLUDE_DIRS ".")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
