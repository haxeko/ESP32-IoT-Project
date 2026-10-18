#pragma once

#include <stddef.h>

#include <esp_matter.h>

/** Register the Basic Information provider (VendorName / ProductName shown as Manufacturer / Model).
 *  Must be called before esp_matter::start(). */
void device_info_init(void);

/** Set the default device name (NodeLabel) in the node config. */
void device_info_set_name(esp_matter::node::config_t *node_config);

/** Add the ProductLabel and SerialNumber attributes to Basic Information. */
void device_info_add_attributes(esp_matter::node_t *node);

/** Serial number: the Wi-Fi MAC address, e.g. "8ED0B2A8594B". */
void device_info_get_serial(char *buf, size_t size);

/** Log what tells units and pairings apart: serial number, unique ID, vendor/product IDs, discriminator, and per
 *  fabric the node ID, compressed fabric ID, admin vendor and subscriptions. Compare these lines between units: the
 *  serial number, unique ID and node IDs must differ. Matter thread only (reads the fabric table). */
void device_info_log_identity(void);
