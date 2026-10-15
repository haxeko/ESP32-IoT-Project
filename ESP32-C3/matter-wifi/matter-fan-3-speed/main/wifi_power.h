#pragma once

/* Wi-Fi power save of a mains-powered device (menuconfig -> Wi-Fi Power Save -> WIFI_NO_POWER_SAVE).
 *
 * ESP-IDF starts Wi-Fi in modem sleep (WIFI_PS_MIN_MODEM): the radio sleeps between the router's DTIM beacons, so a
 * packet from a controller waits in the router until the next beacon and multicast (mDNS) is only received after
 * DTIM beacons. A device on mains power does not need it: once Bluetooth is off the radio is kept on (WIFI_PS_NONE).
 * While Bluetooth runs (BLE commissioning) it shares the radio with Wi-Fi and ESP-IDF requires modem sleep, so nothing
 * changes before that. Bluetooth does not come back without a restart (CONFIG_USE_BLE_ONLY_FOR_COMMISSIONING releases
 * its memory), and a restart starts again in modem sleep. */

/** CHIP task: call on kBLEDeinitialized and kInterfaceIpAddressChanged. Turns power save off once Bluetooth is off;
 *  does nothing before that, when it is already off, or with the option disabled. */
void wifi_power_update();
