// Kconfig values of the host tests (main/Kconfig.projbuild defaults). A test build changes them with -D; a bool
// option that is off stays undefined, as in the sdkconfig.h of ESP-IDF.
#pragma once
#define CONFIG_SENSOR_I2C_SDA_GPIO 5
#define CONFIG_SENSOR_I2C_SCL_GPIO 6
#define CONFIG_SENSOR_PERIOD_SEC 10
#ifndef CONFIG_SENSOR_TEMPERATURE_OFFSET_TENTHS
#define CONFIG_SENSOR_TEMPERATURE_OFFSET_TENTHS 0
#endif
