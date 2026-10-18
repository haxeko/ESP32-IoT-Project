// Host test of the radar protocol layer (components/radar_proto): the shared
// byte streams of test/vectors/hlk_vectors.txt, then the reply decoders and
// command builders against the examples printed in the Hi-Link documents.
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "hlk_frame.h"
#include "ld2410_proto.h"
#include "ld2450_proto.h"

static int g_failures;

#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            g_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)

static std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static std::string format(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

static std::string hex(const uint8_t *p, size_t len)
{
    std::string s;
    for (size_t i = 0; i < len; i++) {
        s += format("%02X", p[i]);
    }
    return s;
}

static std::string csv(const uint8_t *p, size_t len)
{
    std::string s;
    for (size_t i = 0; i < len; i++) {
        s += format(i ? ",%u" : "%u", p[i]);
    }
    return s;
}

static std::vector<uint8_t> parse_hex(const std::string &text)
{
    std::vector<uint8_t> out;
    std::string digits;
    for (char c : text) {
        if (!isspace((unsigned char)c)) {
            digits += c;
        }
    }
    for (size_t i = 0; i + 1 < digits.size(); i += 2) {
        out.push_back((uint8_t)strtoul(digits.substr(i, 2).c_str(), nullptr, 16));
    }
    if (digits.size() % 2) {
        printf("  odd number of hex digits: %s\n", text.c_str());
        g_failures++;
    }
    return out;
}

// Same text as tools/hlk_proto.py event_text()
static std::string event_text(const hlk_frame_t *f)
{
    switch (f->kind) {
    case HLK_FRAME_LD2410_REPORT: {
        ld2410_report_t r;
        const hlk_err_t err = ld2410_decode_report(f->payload, f->payload_len, &r);
        if (err != HLK_OK) {
            return format("E2410 err=%s", hlk_err_name(err));
        }
        std::string s = format("R2410 t=%u st=%u mcm=%u me=%u scm=%u se=%u dcm=%u", r.type, r.state, r.moving_cm,
                               r.moving_energy, r.still_cm, r.still_energy, r.detection_cm);
        if (r.type == LD2410_TYPE_ENGINEERING) {
            s += format(" nm=%u ns=%u m=%s s=%s", r.moving_gates, r.still_gates,
                        csv(r.moving_gate_energy, r.moving_gates).c_str(),
                        csv(r.still_gate_energy, r.still_gates).c_str());
        }
        s += format(" x=%u", r.extra_len);
        if (r.extra_len >= 2) {
            s += format(" light=%u out=%u", r.light, r.out_pin);
        }
        return s;
    }
    case HLK_FRAME_LD2450_REPORT: {
        ld2450_report_t r;
        const hlk_err_t err = ld2450_decode_report(f->payload, f->payload_len, &r);
        if (err != HLK_OK) {
            return format("E2450 err=%s", hlk_err_name(err));
        }
        std::string s = format("R2450 n=%u", r.count);
        for (int i = 0; i < LD2450_TARGETS; i++) {
            const ld2450_target_t &t = r.target[i];
            if (!t.present) {
                s += format(" t%d=-", i + 1);
            } else {
                s += format(" t%d=%d,%d,%d,%u%s", i + 1, t.x_mm, t.y_mm, t.speed_cm_s, t.resolution_mm,
                            ld2450_target_plausible(&t) ? "" : "*");
            }
        }
        return s;
    }
    case HLK_FRAME_ACK: {
        hlk_ack_t ack;
        const hlk_err_t err = hlk_decode_ack(f->payload, f->payload_len, &ack);
        if (err != HLK_OK) {
            return format("EACK err=%s", hlk_err_name(err));
        }
        return format("ACK c=%04X st=%u v=%s", ack.command, ack.status, hex(ack.value, ack.value_len).c_str());
    }
    case HLK_FRAME_COMMAND:
        return format("CMD c=%04X v=%s", hlk_u16(f->payload), hex(f->payload + 2, f->payload_len - 2).c_str());
    }
    return "?";
}

static void collect(const hlk_frame_t *f, void *ctx)
{
    static_cast<std::vector<std::string> *>(ctx)->push_back(event_text(f));
}

static std::string without_spaces(const std::string &s)
{
    std::string out;
    for (char c : s) {
        if (!isspace((unsigned char)c)) {
            out += c;
        }
    }
    return out;
}

static std::string trim(const std::string &s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    const size_t e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

static std::vector<std::string> split(const std::string &s, const std::string &sep)
{
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        const size_t pos = s.find(sep, start);
        out.push_back(trim(s.substr(start, pos == std::string::npos ? std::string::npos : pos - start)));
        if (pos == std::string::npos) {
            return out;
        }
        start = pos + sep.size();
    }
}

static void run_vectors(const char *path)
{
    std::ifstream in(path);
    if (!in) {
        printf("  cannot open %s\n", path);
        g_failures++;
        return;
    }
    int count = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (trim(line).empty() || trim(line)[0] == '#') {
            continue;
        }
        const std::vector<std::string> fields = split(line, "|");
        if (fields.size() < 3) {
            printf("  bad vector line: %s\n", line.c_str());
            g_failures++;
            continue;
        }
        const std::string &name = fields[0];
        const std::vector<uint8_t> stream = parse_hex(fields[1]);
        std::string expected;
        for (const std::string &e : split(fields[2], ";")) {
            if (!e.empty()) {
                expected += (expected.empty() ? "" : " ; ") + e;
            }
        }
        count++;

        // Whole, byte by byte, 7-byte chunks
        const size_t chunks[] = { stream.size() ? stream.size() : 1, 1, 7 };
        for (size_t chunk : chunks) {
            hlk_parser_t parser;
            hlk_parser_init(&parser);
            std::vector<std::string> events;
            for (size_t pos = 0; pos < stream.size(); pos += chunk) {
                const size_t n = stream.size() - pos < chunk ? stream.size() - pos : chunk;
                hlk_parser_feed(&parser, &stream[pos], n, collect, &events);
            }
            std::string actual;
            for (const std::string &e : events) {
                actual += (actual.empty() ? "" : " ; ") + e;
            }
            if (without_spaces(actual) != without_spaces(expected)) {
                printf("  FAIL %s (chunk %zu)\n    expected: %s\n    actual:   %s\n", name.c_str(), chunk,
                       expected.c_str(), actual.c_str());
                g_failures++;
            }
            if (fields.size() >= 4 && !fields[3].empty()) {
                const hlk_parser_stats_t &st = parser.stats;
                const std::string counters = format("frames=%u garbage=%u bad_length=%u bad_footer=%u", st.frames,
                                                    st.garbage_bytes, st.bad_length, st.bad_footer);
                if (without_spaces(counters) != without_spaces(fields[3])) {
                    printf("  FAIL %s counters (chunk %zu)\n    expected: %s\n    actual:   %s\n", name.c_str(), chunk,
                           fields[3].c_str(), counters.c_str());
                    g_failures++;
                }
            }
        }
    }
    printf("  %d stream vectors\n", count);
}

// Decodes the single ACK frame in hex
static bool ack_of(const char *hex_text, std::vector<uint8_t> &storage, hlk_ack_t *ack)
{
    storage = parse_hex(hex_text);
    struct Ctx {
        hlk_ack_t *ack;
        bool found;
    } ctx = { ack, false };
    hlk_parser_t parser;
    hlk_parser_init(&parser);
    hlk_parser_feed(&parser, storage.data(), storage.size(),
                    [](const hlk_frame_t *f, void *c) {
                        Ctx *x = static_cast<Ctx *>(c);
                        x->found = f->kind == HLK_FRAME_ACK && hlk_decode_ack(f->payload, f->payload_len, x->ack) == HLK_OK;
                    },
                    &ctx);
    // The value points into the parser buffer, which the frame left: copy it into storage
    if (ctx.found) {
        const size_t value_pos = storage.size() - 4 - ack->value_len;
        ack->value = &storage[value_pos];
    }
    return ctx.found;
}

static bool command_is(uint16_t command, const uint8_t *value, size_t value_len, const char *expected_hex)
{
    uint8_t frame[HLK_MAX_FRAME];
    const size_t n = hlk_build_command(command, value, value_len, frame, sizeof(frame));
    const std::vector<uint8_t> expected = parse_hex(expected_hex);
    const bool same = n == expected.size() && memcmp(frame, expected.data(), n) == 0;
    if (!same) {
        printf("    command %04X: %s\n    expected:      %s\n", command, hex(frame, n).c_str(),
               hex(expected.data(), expected.size()).c_str());
    }
    return same;
}

static void test_replies()
{
    std::vector<uint8_t> buf;
    hlk_ack_t ack;
    char text[32];

    // LD2410 doc 2.2.8 (the document prints V1.07.22091615; the bytes say 22091516)
    CHECK(ack_of("FDFCFBFA 0C00 A001 0000 0001 0701 16150922 04030201", buf, &ack));
    hlk_version_t version;
    CHECK(hlk_decode_version(&ack, &version) == HLK_OK);
    hlk_format_version(&version, text, sizeof(text));
    CHECK(strcmp(text, "V1.07.22091516") == 0);
    // LD2450 doc 2.2.6
    CHECK(ack_of("FDFCFBFA 0C00 A001 0000 0000 0201 16240622 04030201", buf, &ack));
    CHECK(hlk_decode_version(&ack, &version) == HLK_OK && version.type == 0);
    hlk_format_version(&version, text, sizeof(text));
    CHECK(strcmp(text, "V1.02.22062416") == 0);
    // LD2410B firmware of the background noise detection note
    version = { 1, 0x0244, 0x24073110 };
    hlk_format_version(&version, text, sizeof(text));
    CHECK(strcmp(text, "V2.44.24073110") == 0);

    CHECK(ack_of("FDFCFBFA 0800 FF01 0000 0100 4000 04030201", buf, &ack));
    hlk_config_info_t config;
    CHECK(hlk_decode_enable_config(&ack, &config) == HLK_OK && config.protocol_version == 1 &&
          config.buffer_size == 0x40);

    // LD2410 doc 2.2.4: all moving thresholds 20, still 25, no-one duration 5 s
    CHECK(ack_of("FDFCFBFA 1C00 6101 0000 AA080808 141414141414141414 191919191919191919 0500 04030201", buf, &ack));
    ld2410_params_t params;
    CHECK(ld2410_decode_params(&ack, &params) == HLK_OK);
    CHECK(params.gates == 9 && params.max_moving_gate == 8 && params.max_still_gate == 8);
    CHECK(params.moving_threshold[0] == 20 && params.moving_threshold[8] == 20);
    CHECK(params.still_threshold[0] == 25 && params.still_threshold[8] == 25);
    CHECK(params.no_one_duration_s == 5);
    ack.value_len = 10;
    CHECK(ld2410_decode_params(&ack, &params) == HLK_ERR_SHORT);

    uint16_t gate_cm = 0;
    CHECK(ack_of("FDFCFBFA 0600 AB01 0000 0100 04030201", buf, &ack));
    CHECK(ld2410_decode_resolution(&ack, &gate_cm) == HLK_OK && gate_cm == 20);
    CHECK(ack_of("FDFCFBFA 0600 AB01 0000 0000 04030201", buf, &ack));
    CHECK(ld2410_decode_resolution(&ack, &gate_cm) == HLK_OK && gate_cm == 75);
    CHECK(ack_of("FDFCFBFA 0600 AB01 0000 0700 04030201", buf, &ack));
    CHECK(ld2410_decode_resolution(&ack, &gate_cm) == HLK_ERR_VALUE);

    // LD2410B doc 2.2.19: light < 0x60 enables OUT, OUT high without a target
    CHECK(ack_of("FDFCFBFA 0800 AE01 0000 01600100 04030201", buf, &ack));
    ld2410_aux_control_t aux;
    CHECK(ld2410_decode_aux_control(&ack, &aux) == HLK_OK && aux.light_mode == 1 && aux.light_threshold == 0x60 &&
          aux.out_idle_level == 1);

    uint16_t status = 9;
    CHECK(ack_of("FDFCFBFA 0600 1B01 0000 0100 04030201", buf, &ack));
    CHECK(ld2410_decode_noise_status(&ack, &status) == HLK_OK && status == 1);

    uint8_t mac[HLK_MAC_LEN];
    bool bt = false;
    CHECK(ack_of("FDFCFBFA 0A00 A501 0000 8F272EB80F65 04030201", buf, &ack));
    CHECK(hlk_decode_mac(&ack, mac, &bt) == HLK_OK && bt && mac[0] == 0x8F && mac[5] == 0x65);
    CHECK(ack_of("FDFCFBFA 0A00 A501 0000 080504030201 04030201", buf, &ack));
    CHECK(hlk_decode_mac(&ack, mac, &bt) == HLK_OK && !bt);

    uint16_t mode = 0;
    CHECK(ack_of("FDFCFBFA 0600 9101 0000 0100 04030201", buf, &ack));
    CHECK(ld2450_decode_tracking(&ack, &mode) == HLK_OK && mode == LD2450_TRACKING_SINGLE);

    // LD2450 doc 2.2.12: only the rectangle (1000, 1000) - (-1000, 5000)
    CHECK(ack_of("FDFCFBFA 1E00 C101 0000 0100 E803E80318FC8813 0000000000000000 0000000000000000 04030201", buf,
                 &ack));
    ld2450_zones_t zones;
    CHECK(ld2450_decode_zones(&ack, &zones) == HLK_OK);
    CHECK(zones.type == LD2450_ZONE_ONLY);
    CHECK(zones.zone[0].x1_mm == 1000 && zones.zone[0].y1_mm == 1000);
    CHECK(zones.zone[0].x2_mm == -1000 && zones.zone[0].y2_mm == 5000);
    CHECK(zones.zone[1].x1_mm == 0 && zones.zone[2].y2_mm == 0);

    // A failed command
    CHECK(ack_of("FDFCFBFA 0400 6401 0100 04030201", buf, &ack));
    CHECK(ack.command == LD2410_CMD_SET_SENSITIVITY && ack.status == 1 && ack.value_len == 0);
}

static void test_commands()
{
    static const uint8_t one[] = { 0x01, 0x00 };
    CHECK(command_is(HLK_CMD_ENABLE_CONFIG, one, sizeof(one), "FDFCFBFA 0400 FF00 0100 04030201"));
    CHECK(command_is(HLK_CMD_END_CONFIG, nullptr, 0, "FDFCFBFA 0200 FE00 04030201"));
    CHECK(command_is(HLK_CMD_READ_MAC, one, sizeof(one), "FDFCFBFA 0400 A500 0100 04030201"));
    CHECK(command_is(HLK_CMD_BLUETOOTH, one, sizeof(one), "FDFCFBFA 0400 A400 0100 04030201"));
    CHECK(command_is(LD2410_CMD_ENGINEERING_ON, nullptr, 0, "FDFCFBFA 0200 6200 04030201"));
    CHECK(command_is(LD2450_CMD_SINGLE_TARGET, nullptr, 0, "FDFCFBFA 0200 8000 04030201"));
    CHECK(command_is(LD2450_CMD_READ_ZONES, nullptr, 0, "FDFCFBFA 0200 C100 04030201"));

    uint8_t baud[2];
    hlk_put_u16(baud, hlk_baud_index(256000));
    CHECK(command_is(HLK_CMD_SET_BAUD, baud, sizeof(baud), "FDFCFBFA 0400 A100 0700 04030201"));
    CHECK(hlk_baud_index(115200) == 5 && hlk_baud_index(12345) == 0);
    CHECK(hlk_baud_rate(8) == 460800 && hlk_baud_rate(0) == 0 && hlk_baud_rate(9) == 0);

    // LD2410 doc 2.2.3: farthest gates 8 / 8, no-one duration 5 s
    uint8_t value[26];
    size_t n = ld2410_value_max_gates(8, 8, 5, value);
    CHECK(command_is(LD2410_CMD_SET_MAX_GATES, value, n,
                     "FDFCFBFA 1400 6000 0000 08000000 0100 08000000 0200 05000000 04030201"));
    CHECK(ld2410_value_max_gates(1, 8, 5, value) == 0 && ld2410_value_max_gates(8, 9, 5, value) == 0);
    // LD2410 doc 2.2.7: gate 3 at 40 / 40, then every gate at 40 / 40
    n = ld2410_value_sensitivity(3, 40, 40, value);
    CHECK(command_is(LD2410_CMD_SET_SENSITIVITY, value, n,
                     "FDFCFBFA 1400 6400 0000 03000000 0100 28000000 0200 28000000 04030201"));
    n = ld2410_value_sensitivity(0xFFFF, 40, 40, value);
    CHECK(command_is(LD2410_CMD_SET_SENSITIVITY, value, n,
                     "FDFCFBFA 1400 6400 0000 FFFF0000 0100 28000000 0200 28000000 04030201"));
    CHECK(ld2410_value_sensitivity(9, 40, 40, value) == 0 && ld2410_value_sensitivity(1, 101, 40, value) == 0);
    // LD2410B doc 2.2.20: background noise detection for 10 s
    uint8_t seconds[2];
    hlk_put_u16(seconds, 10);
    CHECK(command_is(LD2410_CMD_NOISE_DETECTION, seconds, sizeof(seconds), "FDFCFBFA 0400 0B00 0A00 04030201"));

    // LD2450 doc 2.2.13: ignore targets in the rectangle (1000, 1000) - (-1000, 5000)
    ld2450_zones_t zones = { LD2450_ZONE_EXCLUDE, { { 1000, 1000, -1000, 5000 }, {}, {} } };
    n = ld2450_value_zones(&zones, value);
    CHECK(n == 26);
    CHECK(command_is(LD2450_CMD_SET_ZONES, value, n,
                     "FDFCFBFA 1C00 C200 0200 E803E80318FC8813 0000000000000000 0000000000000000 04030201"));
    zones.type = 3;
    CHECK(ld2450_value_zones(&zones, value) == 0);

    uint8_t small[8];
    CHECK(hlk_build_command(HLK_CMD_END_CONFIG, nullptr, 0, small, sizeof(small)) == 0);
}

static void test_details()
{
    // Sign-magnitude values of the LD2450 reports
    const uint8_t plus_zero[] = { 0x00, 0x80 }, minus_zero[] = { 0x00, 0x00 };
    const uint8_t plus_max[] = { 0xFF, 0xFF }, minus_max[] = { 0xFF, 0x7F };
    CHECK(ld2450_decode_signed(plus_zero) == 0 && ld2450_decode_signed(minus_zero) == 0);
    CHECK(ld2450_decode_signed(plus_max) == 32767 && ld2450_decode_signed(minus_max) == -32767);

    // input_offset tells where in the input each frame ended (for timestamps)
    const std::vector<uint8_t> stream = parse_hex("00 FDFCFBFA 0400 FE01 0000 04030201 0000 "
                                                  "FDFCFBFA 0400 A301 0000 04030201 00");
    std::vector<size_t> offsets;
    hlk_parser_t parser;
    hlk_parser_init(&parser);
    hlk_parser_feed(&parser, stream.data(), stream.size(),
                    [](const hlk_frame_t *f, void *ctx) {
                        static_cast<std::vector<size_t> *>(ctx)->push_back(f->input_offset);
                    },
                    &offsets);
    CHECK(offsets.size() == 2 && offsets[0] == 15 && offsets[1] == 31);
    CHECK(parser.stats.garbage_bytes == 4);

    // A stream of random bytes never overruns the parser and keeps the counters consistent
    uint32_t seed = 12345;
    uint8_t noise[4096];
    for (uint8_t &b : noise) {
        seed = seed * 1103515245 + 12345;
        const uint8_t pick = (uint8_t)(seed >> 16);
        // Many header bytes, so the parser often locks onto false frames
        static const uint8_t k_bytes[] = { 0xF4, 0xF3, 0xF2, 0xF1, 0xAA, 0xFF, 0x03, 0x00, 0xFD, 0xFC, 0xFB, 0xFA };
        b = (pick & 1) ? k_bytes[(pick >> 1) % sizeof(k_bytes)] : (uint8_t)(seed >> 8);
    }
    hlk_parser_init(&parser);
    hlk_parser_feed(&parser, noise, sizeof(noise), nullptr, nullptr);
    CHECK(parser.len < HLK_MAX_FRAME);
    CHECK(parser.stats.garbage_bytes + parser.len <= sizeof(noise));
}

int main()
{
    printf("radar_proto:\n");
    run_vectors("../vectors/hlk_vectors.txt");
    test_replies();
    test_commands();
    test_details();
    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
