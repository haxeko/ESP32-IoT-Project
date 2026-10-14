// Host test of the HLK-LD2410 frame parser (main/drivers/ld2410.cpp).
#include "sim.h"

#include "ld2410.cpp"

// Target report frame: type 0x02 basic or 0x01 engineering (with extra data).
static std::vector<uint8_t> report(uint8_t type, uint8_t state, uint16_t moving_cm, uint16_t still_cm)
{
    std::vector<uint8_t> p = { type, 0xAA, state, (uint8_t)moving_cm, (uint8_t)(moving_cm >> 8), 60,
                               (uint8_t)still_cm, (uint8_t)(still_cm >> 8), 40, 0, 0 };
    if (type == 0x01) {
        for (int i = 0; i < 22; i++) {
            p.push_back(i);
        }
    }
    p.push_back(0x55);
    p.push_back(0x00);
    std::vector<uint8_t> f = { 0xF4, 0xF3, 0xF2, 0xF1, (uint8_t)p.size(), 0x00 };
    f.insert(f.end(), p.begin(), p.end());
    f.insert(f.end(), { 0xF8, 0xF7, 0xF6, 0xF5 });
    return f;
}

static ld2410_target_t run(const char *name, std::vector<SimChunk> chunks, bool expect_target)
{
    sim_reset(1000);
    g_sim.uart_rx = chunks;
    s_has_target = false;
    s_target = {};
    try {
        ld2410_task(NULL);
    } catch (SimEnd &) {
    }
    ld2410_target_t t;
    const bool has_target = ld2410_get_target(&t);
    if (has_target != expect_target) {
        printf("  %s:\n", name);
    }
    CHECK(has_target == expect_target);
    return t;
}

static std::vector<uint8_t> concat(std::vector<uint8_t> a, const std::vector<uint8_t> &b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

int main()
{
    printf("ld2410:\n");
    const auto frame = report(0x02, 0x03, 50, 80);
    ld2410_target_t t = run("basic frame", { { 0, frame } }, true);
    CHECK(t.moving && t.still && t.moving_cm == 50 && t.still_cm == 80);

    t = run("repeated first header byte", { { 0, concat({ 0x00, 0xF4 }, frame) } }, true);
    CHECK(t.moving_cm == 50);

    t = run("split in 3 reads", { { 0, std::vector<uint8_t>(frame.begin(), frame.begin() + 3) },
                                  { 10, std::vector<uint8_t>(frame.begin() + 3, frame.begin() + 10) },
                                  { 20, std::vector<uint8_t>(frame.begin() + 10, frame.end()) } }, true);
    CHECK(t.still_cm == 80);

    t = run("oversize length, then a frame", { { 0, concat({ 0xF4, 0xF3, 0xF2, 0xF1, 0xFF, 0x00 }, frame) } }, true);
    CHECK(t.moving_cm == 50);

    auto bad_footer = frame;
    bad_footer.back() = 0x00;
    t = run("bad footer, then a frame", { { 0, concat(bad_footer, report(0x02, 0x01, 120, 0)) } }, true);
    CHECK(t.moving && !t.still && t.moving_cm == 120);

    run("bad footer only", { { 0, bad_footer } }, false);

    t = run("engineering mode frame", { { 0, report(0x01, 0x02, 0, 95) } }, true);
    CHECK(!t.moving && t.still && t.still_cm == 95);

    t = run("command ACK, then a frame",
            { { 0, concat({ 0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF, 0x01, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01 }, frame) } },
            true);
    CHECK(t.moving_cm == 50);

    printf(g_failures ? "  %d failure(s)\n" : "  OK\n", g_failures);
    return g_failures != 0;
}
