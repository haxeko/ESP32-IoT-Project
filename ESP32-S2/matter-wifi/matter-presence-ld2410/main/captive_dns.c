/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdbool.h>
#include <string.h>

#include "captive_dns.h"

#define DNS_HEADER_LEN      12
#define DNS_MAX_LABEL_LEN   63
#define DNS_TYPE_A          1

size_t captive_dns_reply(uint8_t *buf, size_t len, size_t cap, const uint8_t ip[4])
{
    // Standard query (QR=0, OPCODE=0) with exactly one question.
    if (len < DNS_HEADER_LEN || len > cap || (buf[2] & 0xF8) != 0 || buf[4] != 0 || buf[5] != 1) {
        return 0;
    }
    // Question name: labels up to the terminating zero. Queries do not use
    // compression pointers, so a length above 63 means a malformed packet.
    size_t pos = DNS_HEADER_LEN;
    while (pos < len && buf[pos] != 0) {
        if (buf[pos] > DNS_MAX_LABEL_LEN) {
            return 0;
        }
        pos += (size_t)buf[pos] + 1;
    }
    pos += 5; // terminating zero, QTYPE, QCLASS
    if (pos > len) {
        return 0;
    }
    const uint16_t qtype = (uint16_t)((buf[pos - 4] << 8) | buf[pos - 3]);
    const bool answer = (qtype == DNS_TYPE_A);
    if (answer && pos + CAPTIVE_DNS_ANSWER_LEN > cap) {
        return 0;
    }

    buf[2] = 0x84 | (buf[2] & 0x01); // response, authoritative, keep RD
    buf[3] = 0x80;                   // recursion available, no error
    memset(&buf[6], 0, 6);           // no answer / authority / additional record yet
    if (!answer) {
        return pos;                  // the question only: drops EDNS and other records
    }
    static const uint8_t k_answer[CAPTIVE_DNS_ANSWER_LEN - 4] = {
        0xC0, 0x0C,             // name: pointer to the question
        0x00, 0x01, 0x00, 0x01, // type A, class IN
        0x00, 0x00, 0x00, 0x3C, // TTL 60 s
        0x00, 0x04,             // address length
    };
    memcpy(&buf[pos], k_answer, sizeof(k_answer));
    memcpy(&buf[pos + sizeof(k_answer)], ip, 4);
    buf[7] = 1; // ANCOUNT
    return pos + CAPTIVE_DNS_ANSWER_LEN;
}
