# Sources of one network only: Wi-Fi (ESP32-C3) or Thread (ESP32-H2)
if(CONFIG_OPENTHREAD_ENABLED)
    set(network_excluded "diag_report.cpp" "net_watchdog.cpp" "c3_temperature.cpp")
else()
    set(network_excluded "thread_diag.cpp")
endif()

idf_component_register(SRC_DIRS          "." "drivers"
                       EXCLUDE_SRCS      ${network_excluded}
                       PRIV_INCLUDE_DIRS "." "drivers")

set_property(TARGET ${COMPONENT_LIB} PROPERTY CXX_STANDARD 17)
target_compile_options(${COMPONENT_LIB} PRIVATE "-DCHIP_HAVE_CONFIG_H")
