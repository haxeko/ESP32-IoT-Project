// When the Wi-Fi RSSI and the uptime are reported again (main/diag_report.h).
#include "sim.h"

#include "diag_report.h"

static diag_report_policy_t policy(int step_db, uint32_t refresh_min)
{
    diag_report_policy_t p = {};
    p.rssi_step_db = step_db;
    p.refresh_ticks = refresh_min;
    return p;
}

// RSSI reports over successive minutes, as "1" (reported) and "." (not reported)
static std::string rssi_reports(diag_report_policy_t &p, const std::vector<int> &rssi)
{
    std::string out;
    for (int r : rssi) {
        out += diag_report_tick(p, true, (int8_t)r).rssi ? '1' : '.';
    }
    return out;
}

int main(int argc, char **argv)
{
    printf("diag_report:\n");

    test_begin("defaults: the first RSSI is reported, then changes of 3 dB or more from the last report");
    {
        diag_report_policy_t p = policy(3, 10);
        CHECK(rssi_reports(p, { -60, -61, -62, -63, -61, -62, -66, -64 }) == "1..1..1.");
        CHECK(p.last_rssi == -66);
    }

    test_begin("defaults: the uptime and the RSSI again every 10 minutes, even unchanged");
    {
        diag_report_policy_t p = policy(3, 10);
        std::string up_time, rssi;
        for (int minute = 1; minute <= 30; minute++) {
            const diag_report_due_t due = diag_report_tick(p, true, -70);
            up_time += due.up_time ? '1' : '.';
            rssi += due.rssi ? '1' : '.';
        }
        CHECK(up_time == ".........1.........1.........1");
        CHECK(rssi == "1........1.........1.........1");
    }

    test_begin("not connected: no RSSI report; the first RSSI after reconnecting is reported");
    {
        diag_report_policy_t p = policy(3, 10);
        CHECK(diag_report_tick(p, true, -60).rssi);
        CHECK(!diag_report_tick(p, false, 0).rssi);
        CHECK(!p.has_rssi);
        CHECK(diag_report_tick(p, true, -60).rssi); // same value: what the new subscription got may be older
        CHECK(!diag_report_tick(p, true, -61).rssi);
    }

    test_begin("refresh while not connected: the uptime only");
    {
        diag_report_policy_t p = policy(3, 2);
        diag_report_due_t due = diag_report_tick(p, false, 0);
        CHECK(!due.up_time && !due.rssi);
        due = diag_report_tick(p, false, 0);
        CHECK(due.up_time && !due.rssi);
    }

    test_begin("step 0: the RSSI only with the refresh");
    {
        diag_report_policy_t p = policy(0, 5);
        CHECK(rssi_reports(p, { -50, -80, -50, -80, -60, -90, -50 }) == "....1..");
    }

    test_begin("refresh 0: no uptime report, the RSSI on changes");
    {
        diag_report_policy_t p = policy(3, 0);
        int up_time = 0;
        for (int minute = 1; minute <= 2000; minute++) {
            up_time += diag_report_tick(p, true, -70).up_time;
        }
        CHECK(up_time == 0);
        CHECK(rssi_reports(p, { -72, -73 }) == ".1");
    }

    test_begin("whole RSSI range (-120 to 0 dBm)");
    {
        diag_report_policy_t p = policy(3, 0);
        CHECK(rssi_reports(p, { -120, -118, -117, 0, -2, -3 }) == "1.11.1");
    }

    return test_end();
}
