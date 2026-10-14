/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <string.h>

#include "ld2410_parser.h"

#define HEADER_LEN  4
#define LENGTH_LEN  2
#define FOOTER_LEN  4

static const uint8_t k_header[HEADER_LEN] = { 0xF4, 0xF3, 0xF2, 0xF1 };
static const uint8_t k_footer[FOOTER_LEN] = { 0xF8, 0xF7, 0xF6, 0xF5 };

void ld2410_parser_init(ld2410_parser_t *parser)
{
    memset(parser, 0, sizeof(*parser));
}

static bool decode_payload(const uint8_t *p, size_t len, ld2410_report_t *report)
{
    // TYPE (0x01 engineering / 0x02 basic), 0xAA head, then the basic target data.
    if (len < 11 || (p[0] != 0x01 && p[0] != 0x02) || p[1] != 0xAA) {
        return false;
    }
    report->moving = (p[2] & 0x01) != 0;
    report->still = (p[2] & 0x02) != 0;
    report->moving_cm = (uint16_t)(p[3] | (p[4] << 8));
    report->still_cm = (uint16_t)(p[6] | (p[7] << 8));
    report->detection_cm = (uint16_t)(p[9] | (p[10] << 8));
    return true;
}

bool ld2410_parser_feed(ld2410_parser_t *parser, uint8_t byte, ld2410_report_t *report)
{
    if (parser->pos < HEADER_LEN) {
        if (byte != k_header[parser->pos]) {
            // Resynchronize: this byte may start a new header.
            parser->pos = 0;
            if (byte == k_header[0]) {
                parser->buf[parser->pos++] = byte;
            }
            return false;
        }
        parser->buf[parser->pos++] = byte;
        return false;
    }

    parser->buf[parser->pos++] = byte;
    if (parser->pos == HEADER_LEN + LENGTH_LEN) {
        const size_t payload_len = parser->buf[4] | (parser->buf[5] << 8);
        if (payload_len > LD2410_MAX_PAYLOAD) {
            parser->pos = 0;
            return false;
        }
        parser->frame_len = HEADER_LEN + LENGTH_LEN + payload_len + FOOTER_LEN;
    }
    if (parser->pos < HEADER_LEN + LENGTH_LEN || parser->pos < parser->frame_len) {
        return false;
    }

    // Complete frame: start over with the next byte whatever its content.
    parser->pos = 0;
    if (memcmp(&parser->buf[parser->frame_len - FOOTER_LEN], k_footer, FOOTER_LEN) != 0) {
        return false;
    }
    return decode_payload(&parser->buf[HEADER_LEN + LENGTH_LEN],
                          parser->frame_len - HEADER_LEN - LENGTH_LEN - FOOTER_LEN, report);
}
