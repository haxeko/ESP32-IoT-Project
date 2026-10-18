/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

// Records of the probe's serial stream, read by tools/capture.py: one line per
// record, "@<type>,<fields>*<checksum>\n", the checksum being the XOR of the
// characters between '@' and '*' in two hex digits (as in NMEA). Lines without
// '@' (ESP-IDF log, boot ROM) are not records. Writers never block: a record
// that does not fit in the output buffer is dropped and counted, so a slow or
// absent USB host cannot stall the radar tasks.

#include <esp_err.h>
#include <stddef.h>
#include <stdint.h>

// Record types
#define PROBE_REC_BOOT 'B'    // @B,<t_us>,<key>=<value>,...
#define PROBE_REC_CHANNEL 'C' // @C,<ch>,<t_us>,uart=..,rx=..,tx=..,baud=..
#define PROBE_REC_DATA 'D'    // @D,<ch>,<seq>,<t_us>,<frame hex>   radar report
#define PROBE_REC_ACK 'A'     // @A,<ch>,<seq>,<t_us>,<frame hex>   ACK (or our command looped back)
#define PROBE_REC_TX 'X'      // @X,<ch>,<t_us>,<frame hex>         command sent to the radar
#define PROBE_REC_RAW 'R'     // @R,<ch>,<t_us>,<hex>               raw UART bytes (dump mode)
#define PROBE_REC_INFO 'I'    // @I,<ch>,<t_us>,<key>,<text>        decoded reply / detection result
#define PROBE_REC_EVENT 'E'   // @E,<t_us>,<source>,<text>          marker, button, job, radar silent...
#define PROBE_REC_STATS 'S'   // @S,<ch>,<t_us>,<key>=<value>,...   counters (ch 0: the probe itself)
#define PROBE_REC_HUMAN 'H'   // @H,<t_us>,<text>                   1 s summary for people
#define PROBE_REC_SYNC 'T'    // @T,<t_us>,<pc_ms>                  answer to "sync <pc_ms>"
#define PROBE_REC_REPLY 'K'   // @K,<t_us>,ok|error <text>          answer to a console command

esp_err_t probe_out_init(void);

// Writes "@<type>," followed by the formatted fields.
void probe_out(char type, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

// Writes "@<type>,<head>," followed by data in hex.
void probe_out_hex(char type, const char *head, const uint8_t *data, size_t len);

// Records dropped because the output buffer was full
uint32_t probe_out_dropped(void);
