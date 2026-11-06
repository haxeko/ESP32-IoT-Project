idf_component_register(SRC_DIRS          "."
                       PRIV_INCLUDE_DIRS  "." "${ESP_MATTER_PATH}/examples/common/utils")

target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")

# The 802.15.4 driver starts at +20 dBm: apply CONFIG_THREAD_TX_POWER_DBM right after it initialises (main/power.cpp),
# before OpenThread can send its first frame
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_ieee802154_enable")
