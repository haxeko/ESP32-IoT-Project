// Matter SDK overrides, included by CHIPConfig.h through CONFIG_CHIP_PROJECT_CONFIG.

#pragma once

#include "sdkconfig.h"

// The ICD manager already has 3 observers (report scheduler, DNS-SD server, ICD Management cluster); the LIT/SIT
// policy of main/power.cpp registers a 4th one
#define CHIP_CONFIG_ICD_OBSERVERS_POOL_SIZE 4
