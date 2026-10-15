#pragma once

/* Matter side of the fan: Fan device type (0x002B) with the Fan Control cluster
 *   FanModeSequence Off/Low/Med/High/Auto, MultiSpeed (SpeedMax 3), Step,
 *   Rocking (RockSupport left-right) for the oscillation relay (optional, CONFIG_FAN_MATTER_ROCKING),
 *   Auto (FanMode Auto, Apple Home's Auto switch) and Wind (WindSupport natural wind), both mapped to natural wind
 *   (optional, CONFIG_FAN_MATTER_AUTO_NATURAL / CONFIG_FAN_MATTER_WIND_NATURAL).
 *
 * Fan Control is a code-driven cluster in esp-matter 1.6: it applies the specification rules between FanMode,
 * PercentSetting and SpeedSetting itself, and reports controller changes to the application through the
 * FanControl::Delegate. The delegate forwards them to the fan task (fan_ctrl.h), which runs the motor at
 * SpeedSetting; the fan task's state is then written back into the cluster (settings changed locally, and the
 * actual speed in PercentCurrent / SpeedCurrent). */

#include <esp_matter.h>

/** Create the fan endpoint. Call before esp_matter::start(), after status_led_init(). */
esp_matter::endpoint_t *fan_matter_create(esp_matter::node_t *node);

/** Call right after esp_matter::start(): writes the fan state into the cluster and starts following both ways. */
void fan_matter_start();
