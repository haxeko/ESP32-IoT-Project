// Beacon configuration (main/presence_config.cpp): defaults, validation of every field, duplicate beacons, and
// the stored blob (round trip, corruption, unknown version).
#include "test.h"

#include "crc32.h"
#include "presence_config.h"

static presence_config_t valid_config()
{
    presence_config_t cfg;
    presence_config_set_defaults(&cfg);
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        presence_slot_config_t &s = cfg.slots[i];
        s.enabled = true;
        s.has_addr = true;
        const uint8_t addr[6] = { 0xC0, 0xFF, 0xEE, 0x00, 0x00, static_cast<uint8_t>(i + 1) };
        memcpy(s.addr, addr, 6);
        s.addr_type = i % 2 ? BEACON_ADDR_RANDOM : BEACON_ADDR_PUBLIC;
    }
    strcpy(cfg.slots[0].label, "Phòng khách"); // UTF-8
    return cfg;
}

static bool has_error(const presence_config_error_t *errs, size_t n, int slot, uint8_t field, uint8_t code)
{
    for (size_t i = 0; i < n; i++) {
        if (errs[i].slot == slot && errs[i].field == field && errs[i].code == code) {
            return true;
        }
    }
    return false;
}

static void defaults()
{
    presence_config_t cfg;
    presence_config_set_defaults(&cfg);
    CHECK(cfg.scan_interval_ms == 160 && cfg.scan_window_ms == 40);
    CHECK(cfg.scan_window_ms < cfg.scan_interval_ms); // never the whole radio time by default
    for (int i = 0; i < PRESENCE_SLOT_COUNT; i++) {
        CHECK(!cfg.slots[i].enabled && !cfg.slots[i].has_addr);
        CHECK(cfg.slots[i].timeout_s == 45);
        CHECK(!cfg.slots[i].rssi_filter);
        CHECK(!presence_slot_is_active(&cfg.slots[i]));
    }
    CHECK_STR(cfg.slots[2].label, "Presence 3");
    CHECK(presence_config_validate(&cfg, false, nullptr, 0) == 0);
}

static void valid_and_enabled_without_address()
{
    presence_config_t cfg = valid_config();
    CHECK(presence_config_validate(&cfg, false, nullptr, 0) == 0);
    // Enabled without an address is allowed: that slot simply reports "no presence"
    cfg.slots[3].has_addr = false;
    CHECK(presence_config_validate(&cfg, false, nullptr, 0) == 0);
    CHECK(!presence_slot_is_active(&cfg.slots[3]));
}

static void field_errors()
{
    presence_config_t cfg = valid_config();
    cfg.slots[0].timeout_s = 9;
    cfg.slots[1].timeout_s = 3601;
    cfg.slots[2].rssi_threshold = -101;
    cfg.slots[3].rssi_hysteresis = 21;
    cfg.slots[4].addr_type = 3;
    memset(cfg.slots[4].addr, 0, 6); // all zero
    cfg.scan_interval_ms = 19;
    presence_config_error_t errs[16];
    const size_t n = presence_config_validate(&cfg, false, errs, 16);
    CHECK(n == 7);
    CHECK(has_error(errs, n, 0, PRESENCE_FIELD_TIMEOUT, PRESENCE_ERR_TIMEOUT_RANGE));
    CHECK(has_error(errs, n, 1, PRESENCE_FIELD_TIMEOUT, PRESENCE_ERR_TIMEOUT_RANGE));
    CHECK(has_error(errs, n, 2, PRESENCE_FIELD_RSSI_THRESHOLD, PRESENCE_ERR_RSSI_RANGE));
    CHECK(has_error(errs, n, 3, PRESENCE_FIELD_RSSI_HYSTERESIS, PRESENCE_ERR_HYSTERESIS_RANGE));
    CHECK(has_error(errs, n, 4, PRESENCE_FIELD_ADDR_TYPE, PRESENCE_ERR_ADDR_TYPE));
    CHECK(has_error(errs, n, 4, PRESENCE_FIELD_ADDR, PRESENCE_ERR_ADDR_INVALID));
    CHECK(has_error(errs, n, -1, PRESENCE_FIELD_SCAN_INTERVAL, PRESENCE_ERR_SCAN_INTERVAL_RANGE));
    // Only max_errors are written, all are counted
    CHECK(presence_config_validate(&cfg, false, errs, 2) == 7);
}

static void scan_window_limits()
{
    presence_config_t cfg = valid_config();
    cfg.scan_window_ms = cfg.scan_interval_ms + 1;
    presence_config_error_t errs[4];
    size_t n = presence_config_validate(&cfg, false, errs, 4);
    CHECK(n == 1 && has_error(errs, n, -1, PRESENCE_FIELD_SCAN_WINDOW, PRESENCE_ERR_SCAN_WINDOW_ABOVE_INTERVAL));
    cfg.scan_window_ms = cfg.scan_interval_ms; // allowed when chosen explicitly
    CHECK(presence_config_validate(&cfg, false, nullptr, 0) == 0);
    cfg.scan_window_ms = 9;
    n = presence_config_validate(&cfg, false, errs, 4);
    CHECK(n == 1 && has_error(errs, n, -1, PRESENCE_FIELD_SCAN_WINDOW, PRESENCE_ERR_SCAN_WINDOW_RANGE));
}

static void labels()
{
    presence_config_t cfg = valid_config();
    strcpy(cfg.slots[1].label, "bad\x01name");
    strcpy(cfg.slots[2].label, "\xC3\x28"); // invalid UTF-8
    memset(cfg.slots[3].label, 'x', PRESENCE_LABEL_MAX_BYTES);
    cfg.slots[3].label[PRESENCE_LABEL_MAX_BYTES] = '\0'; // exactly the maximum: fine
    presence_config_error_t errs[8];
    const size_t n = presence_config_validate(&cfg, false, errs, 8);
    CHECK(n == 2);
    CHECK(has_error(errs, n, 1, PRESENCE_FIELD_LABEL, PRESENCE_ERR_LABEL_INVALID));
    CHECK(has_error(errs, n, 2, PRESENCE_FIELD_LABEL, PRESENCE_ERR_LABEL_INVALID));
    // No terminating NUL within the buffer: too long
    memset(cfg.slots[1].label, 'y', sizeof(cfg.slots[1].label));
    strcpy(cfg.slots[2].label, "ok");
    CHECK(presence_config_validate(&cfg, false, errs, 8) == 1);
    CHECK(errs[0].code == PRESENCE_ERR_LABEL_TOO_LONG);
}

static void duplicates()
{
    presence_config_t cfg = valid_config();
    memcpy(cfg.slots[3].addr, cfg.slots[1].addr, 6);
    cfg.slots[3].addr_type = cfg.slots[1].addr_type;
    presence_config_error_t errs[4];
    size_t n = presence_config_validate(&cfg, false, errs, 4);
    CHECK(n == 1);
    CHECK(errs[0].slot == 3 && errs[0].other_slot == 1 && errs[0].code == PRESENCE_ERR_ADDR_DUPLICATE);
    CHECK(presence_config_validate(&cfg, true, errs, 4) == 0); // allowed by Kconfig option

    // "any" conflicts with both types; public vs random with the same address bytes are different devices
    cfg.slots[3].addr_type = BEACON_ADDR_ANY;
    CHECK(presence_config_validate(&cfg, false, errs, 4) == 1);
    cfg.slots[1].addr_type = BEACON_ADDR_PUBLIC;
    cfg.slots[3].addr_type = BEACON_ADDR_RANDOM;
    CHECK(presence_config_validate(&cfg, false, errs, 4) == 0);

    // A disabled slot may keep the same address
    cfg.slots[3].addr_type = BEACON_ADDR_PUBLIC;
    cfg.slots[3].enabled = false;
    CHECK(presence_config_validate(&cfg, false, errs, 4) == 0);
}

static void blob_round_trip()
{
    presence_config_t cfg = valid_config();
    cfg.slots[2].rssi_filter = true;
    cfg.slots[2].rssi_threshold = -77;
    cfg.slots[2].rssi_hysteresis = 4;
    cfg.slots[4].timeout_s = 3600;
    cfg.scan_interval_ms = 300;
    cfg.scan_window_ms = 30;
    uint8_t blob[PRESENCE_CONFIG_BLOB_MAX_SIZE];
    const size_t len = presence_config_encode(&cfg, blob, sizeof(blob));
    CHECK(len > 0);
    presence_config_t out;
    memset(&out, 0, sizeof(out));
    CHECK(presence_config_decode(blob, len, &out) == PRESENCE_DECODE_OK);
    CHECK(presence_config_equal(&cfg, &out));
    CHECK(presence_config_encode(&cfg, blob, len - 1) == 0); // buffer too small
}

static void blob_corruption()
{
    presence_config_t cfg = valid_config();
    uint8_t blob[PRESENCE_CONFIG_BLOB_MAX_SIZE];
    const size_t len = presence_config_encode(&cfg, blob, sizeof(blob));
    presence_config_t out;
    presence_config_set_defaults(&out);
    const presence_config_t before = out;

    // Any single flipped bit is caught
    for (size_t i = 0; i < len; i++) {
        uint8_t copy[PRESENCE_CONFIG_BLOB_MAX_SIZE];
        memcpy(copy, blob, len);
        copy[i] ^= 0x10;
        CHECK(presence_config_decode(copy, len, &out) != PRESENCE_DECODE_OK);
    }
    CHECK(presence_config_equal(&out, &before)); // never partially written

    CHECK(presence_config_decode(blob, 5, &out) == PRESENCE_DECODE_TRUNCATED);
    CHECK(presence_config_decode(blob, len - 1, &out) == PRESENCE_DECODE_BAD_LENGTH);
    CHECK(presence_config_decode(nullptr, len, &out) == PRESENCE_DECODE_TRUNCATED);
}

// Rewrites a field of an encoded blob and fixes its CRC, so only the value check can catch it
static void patch(uint8_t *blob, size_t len, size_t offset, uint8_t value)
{
    blob[offset] = value;
    const uint32_t crc = crc32_ieee(blob, len - 4);
    for (int i = 0; i < 4; i++) {
        blob[len - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
}

static void blob_values_checked_and_versions()
{
    presence_config_t cfg = valid_config();
    uint8_t blob[PRESENCE_CONFIG_BLOB_MAX_SIZE];
    const size_t len = presence_config_encode(&cfg, blob, sizeof(blob));
    presence_config_t out;

    uint8_t copy[PRESENCE_CONFIG_BLOB_MAX_SIZE];
    memcpy(copy, blob, len);
    patch(copy, len, 8 + 6 + 1, 9); // slot 0 address type 9
    CHECK(presence_config_decode(copy, len, &out) == PRESENCE_DECODE_BAD_VALUE);

    memcpy(copy, blob, len);
    patch(copy, len, 8 + 6 + 12, PRESENCE_LABEL_MAX_BYTES + 1); // slot 0 label length too big
    CHECK(presence_config_decode(copy, len, &out) == PRESENCE_DECODE_BAD_VALUE);

    memcpy(copy, blob, len);
    patch(copy, len, 4, 2); // format version 2, written by a newer firmware
    CHECK(presence_config_decode(copy, len, &out) == PRESENCE_DECODE_UNSUPPORTED_VERSION);

    memcpy(copy, blob, len);
    patch(copy, len, 0, 'X');
    CHECK(presence_config_decode(copy, len, &out) == PRESENCE_DECODE_BAD_MAGIC);
}

static void error_texts()
{
    for (int code = PRESENCE_ERR_LABEL_TOO_LONG; code <= PRESENCE_ERR_TYPE; code++) {
        CHECK(strcmp(presence_error_text(code), "Lỗi không xác định") != 0);
    }
    CHECK_STR(presence_field_name(PRESENCE_FIELD_ADDR), "mac");
}

int main()
{
    RUN(defaults);
    RUN(valid_and_enabled_without_address);
    RUN(field_errors);
    RUN(scan_window_limits);
    RUN(labels);
    RUN(duplicates);
    RUN(blob_round_trip);
    RUN(blob_corruption);
    RUN(blob_values_checked_and_versions);
    RUN(error_texts);
    return test_summary("presence_config");
}
