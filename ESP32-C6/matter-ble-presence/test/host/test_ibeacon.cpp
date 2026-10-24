// iBeacon parser (main/ibeacon_parser.cpp): valid frames, frames that are not iBeacons, and truncated or
// malformed advertising data that must never be read past its end (run under AddressSanitizer by run.sh).
#include "test.h"

#include <stdlib.h>
#include <vector>

#include "ibeacon_parser.h"

using Bytes = std::vector<uint8_t>;

static const uint8_t kUuid[16] = { 0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2,
                                   0xB0, 0x60, 0xD0, 0xF5, 0xA7, 0x10, 0x96, 0xE0 };

// Flags + iBeacon structure, as most beacons send them
static Bytes ibeacon_frame(uint16_t major, uint16_t minor, int8_t tx, bool with_flags = true)
{
    Bytes b;
    if (with_flags) {
        b.insert(b.end(), { 0x02, 0x01, 0x06 });
    }
    b.insert(b.end(), { 0x1A, 0xFF, 0x4C, 0x00, 0x02, 0x15 });
    b.insert(b.end(), kUuid, kUuid + 16);
    b.push_back(static_cast<uint8_t>(major >> 8));
    b.push_back(static_cast<uint8_t>(major));
    b.push_back(static_cast<uint8_t>(minor >> 8));
    b.push_back(static_cast<uint8_t>(minor));
    b.push_back(static_cast<uint8_t>(tx));
    return b;
}

// Parses a heap copy of exactly len bytes, so AddressSanitizer catches any read past the end.
static bool parse_exact(const Bytes &b, size_t len, ibeacon_t *out)
{
    uint8_t *copy = static_cast<uint8_t *>(malloc(len ? len : 1));
    if (len > 0) {
        memcpy(copy, b.data(), len);
    }
    const bool ok = ibeacon_parse(copy, len, out);
    free(copy);
    return ok;
}

static void valid_frame()
{
    const Bytes b = ibeacon_frame(0x0102, 0xA0B0, -59);
    CHECK(b.size() == 30);
    ibeacon_t ib;
    CHECK(parse_exact(b, b.size(), &ib));
    CHECK(memcmp(ib.uuid, kUuid, 16) == 0);
    CHECK(ib.major == 0x0102); // big endian on air
    CHECK(ib.minor == 0xA0B0);
    CHECK(ib.tx_power == -59);
    char uuid[IBEACON_UUID_STR_SIZE];
    ibeacon_uuid_format(ib.uuid, uuid);
    CHECK_STR(uuid, "E2C56DB5-DFFB-48D2-B060-D0F5A71096E0");
}

static void valid_without_flags_and_with_trailing_name()
{
    Bytes b = ibeacon_frame(1, 2, -70, false);
    ibeacon_t ib;
    CHECK(parse_exact(b, b.size(), &ib));
    CHECK(ib.major == 1 && ib.minor == 2 && ib.tx_power == -70);

    // A name structure before the iBeacon structure (scan response style)
    Bytes c = { 0x04, 0x09, 'A', 'b', 'c' };
    c.insert(c.end(), b.begin(), b.end());
    CHECK(parse_exact(c, c.size(), &ib));
}

static void not_an_ibeacon()
{
    ibeacon_t ib;
    memset(&ib, 0x5A, sizeof(ib));
    const ibeacon_t untouched = ib;

    Bytes other_company = ibeacon_frame(1, 2, -59);
    other_company[5] = 0x59; // Nordic 0x0059
    CHECK(!parse_exact(other_company, other_company.size(), &ib));

    Bytes wrong_type = ibeacon_frame(1, 2, -59);
    wrong_type[7] = 0x03;
    CHECK(!parse_exact(wrong_type, wrong_type.size(), &ib));

    Bytes wrong_inner_len = ibeacon_frame(1, 2, -59);
    wrong_inner_len[8] = 0x14;
    CHECK(!parse_exact(wrong_inner_len, wrong_inner_len.size(), &ib));

    // Manufacturer data of 24 bytes (one short): not the iBeacon layout even though the prefix matches
    Bytes short_md = { 0x02, 0x01, 0x06, 0x19, 0xFF, 0x4C, 0x00, 0x02, 0x15 };
    short_md.insert(short_md.end(), kUuid, kUuid + 16);
    short_md.insert(short_md.end(), { 0x00, 0x01, 0x00 });
    CHECK(!parse_exact(short_md, short_md.size(), &ib));

    // Eddystone (service data 0xFEAA)
    Bytes eddystone = { 0x02, 0x01, 0x06, 0x03, 0x03, 0xAA, 0xFE, 0x05, 0x16, 0xAA, 0xFE, 0x10, 0x00 };
    CHECK(!parse_exact(eddystone, eddystone.size(), &ib));

    CHECK(memcmp(&ib, &untouched, sizeof(ib)) == 0);
}

static void truncated_and_malformed()
{
    ibeacon_t ib;
    const Bytes b = ibeacon_frame(1, 2, -59);
    // Every truncation of a valid frame is rejected without reading past the end
    for (size_t len = 0; len < b.size(); len++) {
        CHECK(!parse_exact(b, len, &ib));
    }
    CHECK(!ibeacon_parse(nullptr, 30, &ib));

    // A length byte pointing past the data
    Bytes past_end = { 0x02, 0x01, 0x06, 0xFF, 0xFF, 0x4C, 0x00 };
    CHECK(!parse_exact(past_end, past_end.size(), &ib));

    // Zero length structure ends the data: an iBeacon after it is padding, not data
    Bytes zero = { 0x00 };
    const Bytes frame = ibeacon_frame(1, 2, -59, false);
    zero.insert(zero.end(), frame.begin(), frame.end());
    CHECK(!parse_exact(zero, zero.size(), &ib));

    // Length 1 structure (type only) followed by the frame is fine
    Bytes type_only = { 0x01, 0x0A };
    type_only.insert(type_only.end(), frame.begin(), frame.end());
    CHECK(parse_exact(type_only, type_only.size(), &ib));
}

static void two_manufacturer_structures()
{
    // A first manufacturer structure of another company, then the iBeacon
    Bytes b = { 0x05, 0xFF, 0x59, 0x00, 0x01, 0x02 };
    const Bytes frame = ibeacon_frame(7, 8, -60, false);
    b.insert(b.end(), frame.begin(), frame.end());
    ibeacon_t ib;
    CHECK(parse_exact(b, b.size(), &ib));
    CHECK(ib.major == 7 && ib.minor == 8);
}

static void names()
{
    char name[32];
    Bytes complete = { 0x02, 0x01, 0x06, 0x05, 0x09, 'B', 'e', 'a', 'n' };
    CHECK(adv_parse_name(complete.data(), complete.size(), name, sizeof(name)) == 4);
    CHECK_STR(name, "Bean");

    // Shortened name used when there is no complete name; complete name preferred when both are there
    Bytes shortened = { 0x04, 0x08, 'A', 'B', 'C' };
    CHECK(adv_parse_name(shortened.data(), shortened.size(), name, sizeof(name)) == 3);
    CHECK_STR(name, "ABC");
    Bytes both = { 0x04, 0x08, 'A', 'B', 'C', 0x05, 0x09, 'L', 'o', 'n', 'g' };
    adv_parse_name(both.data(), both.size(), name, sizeof(name));
    CHECK_STR(name, "Long");

    // No name: empty, not an error
    const Bytes frame = ibeacon_frame(1, 2, -59);
    CHECK(adv_parse_name(frame.data(), frame.size(), name, sizeof(name)) == 0);
    CHECK_STR(name, "");

    // Control characters and invalid UTF-8 become '?', valid UTF-8 (Vietnamese) is kept
    Bytes weird = { 0x08, 0x09, 'a', 0x01, 0xFF, 'b', 0xC3, 0xA2, '<' };
    adv_parse_name(weird.data(), weird.size(), name, sizeof(name));
    CHECK_STR(name, "a??b\xC3\xA2<"); // '<' is escaped later by json_escape()

    // Truncated to the buffer, never in the middle of a UTF-8 sequence
    char small[4];
    Bytes viet = { 0x07, 0x09, 'x', 0xE1, 0xBA, 0xA1, 'y', 'z' }; // "xạyz"
    adv_parse_name(viet.data(), viet.size(), small, sizeof(small));
    CHECK_STR(small, "x");

    // Name structure running past the end: no name
    Bytes bad = { 0x09, 0x09, 'a', 'b' };
    CHECK(adv_parse_name(bad.data(), bad.size(), name, sizeof(name)) == 0);
}

static void fuzz()
{
    // Random data and random mutations of a valid frame: no crash, no out of bounds read (ASan), and a frame that
    // parses always has the iBeacon prefix somewhere in it.
    srand(12345);
    const Bytes frame = ibeacon_frame(1, 2, -59);
    for (int iter = 0; iter < 200000; iter++) {
        Bytes b;
        if (iter % 2) {
            const size_t len = rand() % 40;
            for (size_t i = 0; i < len; i++) {
                b.push_back(static_cast<uint8_t>(rand()));
            }
        } else {
            b = frame;
            const int flips = 1 + rand() % 3;
            for (int f = 0; f < flips; f++) {
                b[rand() % b.size()] = static_cast<uint8_t>(rand());
            }
            b.resize(rand() % (b.size() + 1));
        }
        ibeacon_t ib;
        char name[32];
        const bool ok = parse_exact(b, b.size(), &ib);
        uint8_t *copy = static_cast<uint8_t *>(malloc(b.size() ? b.size() : 1));
        if (!b.empty()) {
            memcpy(copy, b.data(), b.size());
        }
        adv_parse_name(copy, b.size(), name, sizeof(name));
        free(copy);
        CHECK(strlen(name) < sizeof(name));
        if (ok) {
            bool found = false;
            for (size_t i = 0; i + 5 < b.size() && !found; i++) {
                found = b[i] == 0xFF && b[i + 1] == 0x4C && b[i + 2] == 0x00 && b[i + 3] == 0x02 && b[i + 4] == 0x15;
            }
            CHECK(found);
        }
    }
}

int main()
{
    RUN(valid_frame);
    RUN(valid_without_flags_and_with_trailing_name);
    RUN(not_an_ibeacon);
    RUN(truncated_and_malformed);
    RUN(two_manufacturer_structures);
    RUN(names);
    RUN(fuzz);
    return test_summary("ibeacon_parser");
}
