// Host tests of the setup portal DNS answers (main/captive_dns.c)
#include <stdint.h>
#include <string.h>

#include "captive_dns.h"
#include "check.h"

static const uint8_t k_ip[4] = { 192, 168, 4, 1 };

// Query for "captive.apple.com", type and class given, ID 0x1234, RD set.
static size_t make_query(uint8_t *buf, uint16_t qtype)
{
    static const uint8_t header[] = { 0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uint8_t name[] = { 7, 'c', 'a', 'p', 't', 'i', 'v', 'e', 5, 'a', 'p', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0 };
    size_t len = 0;
    memcpy(buf, header, sizeof(header));
    len += sizeof(header);
    memcpy(&buf[len], name, sizeof(name));
    len += sizeof(name);
    buf[len++] = (uint8_t)(qtype >> 8);
    buf[len++] = (uint8_t)qtype;
    buf[len++] = 0x00;
    buf[len++] = 0x01; // class IN
    return len;
}

static void test_a_query_answered(void)
{
    uint8_t buf[512];
    const size_t qlen = make_query(buf, 1);
    const size_t len = captive_dns_reply(buf, qlen, sizeof(buf), k_ip);
    CHECK_EQ(len, qlen + CAPTIVE_DNS_ANSWER_LEN);
    CHECK_EQ(buf[0], 0x12);         // ID kept
    CHECK_EQ(buf[1], 0x34);
    CHECK_EQ(buf[2], 0x85);         // response, authoritative, RD kept
    CHECK_EQ(buf[3], 0x80);         // RA, NOERROR
    CHECK_EQ(buf[5], 1);            // QDCOUNT
    CHECK_EQ(buf[7], 1);            // ANCOUNT
    CHECK_EQ(buf[9], 0);            // NSCOUNT
    CHECK_EQ(buf[11], 0);           // ARCOUNT
    CHECK_EQ(buf[qlen], 0xC0);      // name pointer to offset 12
    CHECK_EQ(buf[qlen + 1], 0x0C);
    CHECK_EQ(buf[qlen + 3], 1);     // type A
    CHECK_EQ(buf[qlen + 11], 4);    // RDLENGTH
    CHECK(memcmp(&buf[qlen + 12], k_ip, 4) == 0);
}

static void test_other_types_get_empty_answer(void)
{
    const uint16_t types[] = { 28 /* AAAA */, 65 /* HTTPS */, 16 /* TXT */ };
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        uint8_t buf[512];
        const size_t qlen = make_query(buf, types[i]);
        CHECK_EQ(captive_dns_reply(buf, qlen, sizeof(buf), k_ip), qlen);
        CHECK_EQ(buf[2] & 0x80, 0x80);
        CHECK_EQ(buf[3] & 0x0F, 0);  // NOERROR (no data)
        CHECK_EQ(buf[7], 0);
    }
}

static void test_additional_records_dropped(void)
{
    // EDNS OPT record after the question: ARCOUNT 1 in, the reply has none.
    uint8_t buf[512];
    const size_t qlen = make_query(buf, 1);
    buf[11] = 1;
    const uint8_t opt[] = { 0x00, 0x00, 0x29, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    memcpy(&buf[qlen], opt, sizeof(opt));
    const size_t len = captive_dns_reply(buf, qlen + sizeof(opt), sizeof(buf), k_ip);
    CHECK_EQ(len, qlen + CAPTIVE_DNS_ANSWER_LEN);
    CHECK_EQ(buf[11], 0);
    CHECK(memcmp(&buf[qlen + 12], k_ip, 4) == 0);
}

static void test_not_a_query_dropped(void)
{
    uint8_t buf[512];
    size_t qlen = make_query(buf, 1);
    buf[2] |= 0x80; // already a response
    CHECK_EQ(captive_dns_reply(buf, qlen, sizeof(buf), k_ip), 0);

    qlen = make_query(buf, 1);
    buf[2] |= 0x28; // opcode 5 (update)
    CHECK_EQ(captive_dns_reply(buf, qlen, sizeof(buf), k_ip), 0);

    qlen = make_query(buf, 1);
    buf[5] = 2; // two questions
    CHECK_EQ(captive_dns_reply(buf, qlen, sizeof(buf), k_ip), 0);

    qlen = make_query(buf, 1);
    buf[4] = 1; // 257 questions
    CHECK_EQ(captive_dns_reply(buf, qlen, sizeof(buf), k_ip), 0);
}

static void test_malformed_dropped(void)
{
    uint8_t buf[512];
    // Shorter than a header
    make_query(buf, 1);
    CHECK_EQ(captive_dns_reply(buf, 11, sizeof(buf), k_ip), 0);

    // Name runs past the end of the packet
    size_t qlen = make_query(buf, 1);
    CHECK_EQ(captive_dns_reply(buf, qlen - 5, sizeof(buf), k_ip), 0);

    // Label length points far beyond the packet
    qlen = make_query(buf, 1);
    buf[12] = 60;
    CHECK_EQ(captive_dns_reply(buf, qlen, sizeof(buf), k_ip), 0);

    // Compression pointer in the question
    qlen = make_query(buf, 1);
    buf[12] = 0xC0;
    CHECK_EQ(captive_dns_reply(buf, qlen, sizeof(buf), k_ip), 0);

    // Length larger than the buffer
    qlen = make_query(buf, 1);
    CHECK_EQ(captive_dns_reply(buf, qlen, qlen - 1, k_ip), 0);
}

static void test_no_room_for_answer(void)
{
    uint8_t buf[512];
    const size_t qlen = make_query(buf, 1);
    CHECK_EQ(captive_dns_reply(buf, qlen, qlen + CAPTIVE_DNS_ANSWER_LEN - 1, k_ip), 0);
    CHECK_EQ(captive_dns_reply(buf, qlen, qlen + CAPTIVE_DNS_ANSWER_LEN, k_ip), qlen + CAPTIVE_DNS_ANSWER_LEN);
}

static void test_root_name(void)
{
    // Query for "." (root): header, empty name, type A, class IN
    uint8_t buf[64] = { 0xAB, 0xCD, 0x00, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0x01, 0x00, 0x01 };
    CHECK_EQ(captive_dns_reply(buf, 17, sizeof(buf), k_ip), 17 + CAPTIVE_DNS_ANSWER_LEN);
    CHECK_EQ(buf[2], 0x84); // RD was clear
}

int main(void)
{
    test_a_query_answered();
    test_other_types_get_empty_answer();
    test_additional_records_dropped();
    test_not_a_query_dropped();
    test_malformed_dropped();
    test_no_room_for_answer();
    test_root_name();
    return test_result("captive_dns");
}
