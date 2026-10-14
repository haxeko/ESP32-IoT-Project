/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// DNS answers of the Wi-Fi setup portal. Plain C, also built by the host tests.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Room needed after the question for the answer record.
#define CAPTIVE_DNS_ANSWER_LEN 16

// Turns the DNS query in buf[0..len) into its reply, in place. An A question
// is answered with ip (IPv4 address, most significant byte first), so phones
// open the setup page for any name; other question types get an empty
// NOERROR answer. buf has room for cap bytes.
// Returns the reply length, or 0 when the packet must be dropped: not a
// standard query with exactly one question, malformed, or no room.
size_t captive_dns_reply(uint8_t *buf, size_t len, size_t cap, const uint8_t ip[4]);

#ifdef __cplusplus
}
#endif
