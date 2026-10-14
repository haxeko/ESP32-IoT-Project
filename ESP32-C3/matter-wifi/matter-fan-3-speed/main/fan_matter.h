#pragma once

/* Matter side of the fan: Fan device type (0x002B) with the Fan Control cluster
 *   FanModeSequence Off/Low/Med/High/Auto, MultiSpeed (SpeedMax 3), Step,
 *   Rocking (RockSupport left-right) and AirflowDirection, both mapped to the oscillation relay,
 *   Auto (FanMode Auto) and Wind (WindSupport natural wind), both mapped to natural wind.
 *
 * Fan Control is a code-driven cluster in esp-matter 1.6: controller writes reach the application through the
 * FanControl::Delegate, not through the attribute callback. The delegate forwards them to the fan task
 * (fan_ctrl.h); every state of the fan task is then written back into the cluster, which also snaps the speed
 * slider to 33 / 66 / 100 %. */

#include <esp_matter.h>

/** Create the fan endpoint. Call before esp_matter::start(), after status_led_init(). */
esp_matter::endpoint_t *fan_matter_create(esp_matter::node_t *node);

/** Hooks for the node attribute callback (CHIP task): PRE_UPDATE and POST_UPDATE of controller writes. */
void fan_matter_pre_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id,
                           const esp_matter_attr_val_t *val);
void fan_matter_post_update(uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id);

/** Call right after esp_matter::start(): writes the fan state into the cluster and starts following both ways. */
void fan_matter_start();
