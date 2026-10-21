// Serial number and device name (main/device_name.cpp).
#include "sim.h"

#include <string.h>

#include "device_name.h"

int main(int argc, char **argv)
{
    printf("device_name:\n");
    char serial[kDeviceSerialSize];
    char name[kDeviceNameSize];

    test_begin("serial number: 12 upper-case hex characters of the 6-byte eFuse MAC");
    const uint8_t mac[6] = { 0x58, 0xCF, 0x79, 0xA1, 0xB2, 0xC4 };
    device_format_serial(mac, serial, sizeof(serial));
    CHECK(strcmp(serial, "58CF79A1B2C4") == 0);
    const uint8_t mac2[6] = { 0x0A, 0x00, 0x0B, 0x01, 0x0C, 0x0F };
    device_format_serial(mac2, serial, sizeof(serial));
    CHECK(strcmp(serial, "0A000B010C0F") == 0);

    test_begin("device name: prefix + last 4 serial characters");
    device_format_name("Presence-", "Presence Sensor", "58CF79A1B2C4", name, sizeof(name));
    CHECK(strcmp(name, "Presence-B2C4") == 0);
    device_format_name("Kitchen-", "Presence Sensor", "58CF79A1B2C4", name, sizeof(name));
    CHECK(strcmp(name, "Kitchen-B2C4") == 0);
    device_format_name("ElevenChars", "Presence Sensor", "58CF79A1B2C4", name, sizeof(name));
    CHECK(strcmp(name, "ElevenCharsB2C4") == 0 && strlen(name) < 16); // BLE name shorter than 16 characters

    test_begin("device name: empty prefix gives the display name");
    device_format_name("", "Presence Sensor", "58CF79A1B2C4", name, sizeof(name));
    CHECK(strcmp(name, "Presence Sensor") == 0);

    return test_end();
}
