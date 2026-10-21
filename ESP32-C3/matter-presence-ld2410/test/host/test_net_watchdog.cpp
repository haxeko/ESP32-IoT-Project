// What the connection watchdog does without any controller subscription (main/net_watchdog.h).
#include "sim.h"

#include "net_watchdog.h"

static net_watchdog_policy_t policy(uint32_t readvertise_min, uint32_t reconnect_min, uint32_t restart_min)
{
    net_watchdog_policy_t p = {};
    p.readvertise_s = readvertise_min * 60;
    p.reconnect_s = reconnect_min * 60;
    p.restart_s = restart_min * 60;
    return p;
}

static const char *name(net_watchdog_action_t a)
{
    switch (a) {
    case NET_WD_READVERTISE:
        return "dnssd";
    case NET_WD_RECONNECT:
        return "wifi";
    case NET_WD_RESTART:
        return "restart";
    default:
        return "";
    }
}

// Checks every 30 s from `from` to `to` (seconds); the actions as "<minute>:<action> ..."
static std::string run(net_watchdog_policy_t &p, uint32_t from, uint32_t to, bool commissioned, bool subscribed)
{
    std::string out;
    for (uint32_t t = from; t <= to; t += 30) {
        const net_watchdog_action_t a = net_watchdog_step(p, t, commissioned, subscribed);
        if (a != NET_WD_NONE) {
            char item[32];
            snprintf(item, sizeof(item), "%s%u:%s", out.empty() ? "" : " ", (unsigned)(t / 60), name(a));
            out += item;
        }
    }
    return out;
}

int main(int argc, char **argv)
{
    printf("net_watchdog:\n");

    test_begin("defaults: DNS-SD every 5 min, Wi-Fi every 10 min, restart at 30 min without a subscription");
    {
        net_watchdog_policy_t p = policy(5, 10, 30);
        // Idle from minute 0 (first check without a subscription)
        CHECK(run(p, 0, 30 * 60, true, false) == "5:dnssd 10:wifi 15:dnssd 20:wifi 25:dnssd 30:restart");
        // Should the restart not happen, it is asked again at each check
        CHECK(net_watchdog_step(p, 30 * 60 + 30, true, false) == NET_WD_RESTART);
    }

    test_begin("a subscription stops everything; the count starts again when it is lost");
    {
        net_watchdog_policy_t p = policy(5, 10, 30);
        CHECK(run(p, 0, 9 * 60, true, false) == "5:dnssd");
        CHECK(run(p, 9 * 60 + 30, 40 * 60, true, true).empty());
        CHECK(!p.idle);
        CHECK(run(p, 40 * 60 + 30, 52 * 60, true, false) == "45:dnssd 50:wifi");
    }

    test_begin("not commissioned (no controller expected): never");
    {
        net_watchdog_policy_t p = policy(5, 10, 30);
        CHECK(run(p, 0, 120 * 60, false, false).empty());
    }

    test_begin("restart 0: DNS-SD and Wi-Fi only, for ever");
    {
        net_watchdog_policy_t p = policy(5, 10, 0);
        const std::string actions = run(p, 0, 60 * 60, true, false);
        CHECK(actions == "5:dnssd 10:wifi 15:dnssd 20:wifi 25:dnssd 30:wifi 35:dnssd 40:wifi 45:dnssd 50:wifi "
                         "55:dnssd 60:wifi");
    }

    test_begin("reconnect 0 and restart 0: DNS-SD only");
    {
        net_watchdog_policy_t p = policy(5, 0, 0);
        CHECK(run(p, 0, 16 * 60, true, false) == "5:dnssd 10:dnssd 15:dnssd");
    }

    test_begin("DNS-SD 0: Wi-Fi and restart only");
    {
        net_watchdog_policy_t p = policy(0, 10, 30);
        CHECK(run(p, 0, 30 * 60, true, false) == "10:wifi 20:wifi 30:restart");
    }

    test_begin("restart before the first reconnect when set lower");
    {
        net_watchdog_policy_t p = policy(5, 10, 8);
        CHECK(run(p, 0, 8 * 60, true, false) == "5:dnssd 8:restart");
    }

    test_begin("Matter task stuck: 3 min or more since its last check");
    {
        CHECK(!net_watchdog_stuck(1000, 1000, 180));
        CHECK(!net_watchdog_stuck(1179, 1000, 180));
        CHECK(net_watchdog_stuck(1180, 1000, 180));
        CHECK(net_watchdog_stuck(5000, 1000, 180));
    }

    return test_end();
}
