/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <driver/uart.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <sdkconfig.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "hlk_frame.h"
#include "ld2410_proto.h"
#include "ld2450_proto.h"
#include "probe_out.h"
#include "probe_radar.h"

#define RX_BUFFER_SIZE 2048
#define EVENT_QUEUE_LEN 32
#define JOB_QUEUE_LEN 2
#define TASK_STACK 6144
#define TASK_PRIO 10 // above the output and console tasks
#define READ_CHUNK 128
#define DUMP_CHUNK 64
// UART_DATA comes this many symbol times after the last byte of a burst
#define RX_TIMEOUT_SYMBOLS 3

static constexpr uint32_t kDetectMs = 2000;    // listen at boot before searching other baud rates
static constexpr uint32_t kScanListenMs = 1200; // per baud rate: about 12 reports at 10 Hz
static constexpr uint32_t kAckTimeoutMs = 300;
static constexpr uint32_t kRestartWaitMs = 2000;
static constexpr int64_t kSilentUs = 3000 * 1000;
static constexpr int64_t kSummaryStaleUs = 2000 * 1000;

typedef struct {
    probe_job_t kind;
    uint32_t arg;
    uint8_t value[PROBE_JOB_VALUE_MAX];
    uint8_t value_len;
} job_req_t;

typedef struct {
    uint16_t command;
    uint8_t value[2];
    uint8_t value_len;
} step_t;

typedef struct {
    bool running;
    int id; // UART number, names the channel in the records
    uart_port_t uart;
    int rx_gpio;
    int tx_gpio;
    uint32_t baud;
    QueueHandle_t events;
    QueueHandle_t jobs;
    hlk_parser_t parser;
    uint32_t seq;
    probe_radar_type_t type;
    bool dump;
    bool silent;
    int64_t last_frame_us;
    // Arrival of the bytes being parsed, for the frame timestamps
    int64_t read_time_us;
    size_t chunk_len;
    size_t bytes_after_chunk;
    // Counters (written by the channel task only)
    uint32_t reports;
    uint32_t acks;
    uint32_t command_echoes;
    uint32_t decode_errors;
    uint32_t implausible;
    uint32_t uart_overflows;
    uint32_t uart_errors;
    // ACK awaited by send_command()
    uint16_t awaiting;
    bool ack_seen;
    uint16_t ack_status;
    // Latest reports, for the summary (s_lock)
    ld2410_report_t last_ld2410;
    ld2450_report_t last_ld2450;
    int64_t last_report_us;
} channel_t;

static channel_t s_channels[PROBE_CHANNELS];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_frames_on = true;
static volatile bool s_engineering_wanted = CONFIG_PROBE_ENGINEERING_AT_BOOT;

static const uint8_t k_one[] = { 0x01, 0x00 };
static const uint8_t k_zero[] = { 0x00, 0x00 };

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

const char *probe_radar_type_name(probe_radar_type_t type)
{
    switch (type) {
    case PROBE_RADAR_LD2410:
        return "LD2410";
    case PROBE_RADAR_LD2450:
        return "LD2450";
    default:
        return "unknown";
    }
}

// Time of one byte (8N1) in microseconds
static int64_t byte_us(const channel_t *ch)
{
    return 10 * 1000000LL / ch->baud;
}

// snprintf at buf[*n] that never writes past len, however long the text gets
static void appendf(char *buf, size_t len, int *n, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
static void appendf(char *buf, size_t len, int *n, const char *fmt, ...)
{
    if (*n < 0 || (size_t)*n >= len) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    const int added = vsnprintf(&buf[*n], len - *n, fmt, args);
    va_end(args);
    if (added > 0) {
        *n = (size_t)(*n + added) < len ? *n + added : (int)len - 1;
    }
}

static void set_type(channel_t *ch, probe_radar_type_t type, int64_t t)
{
    if (ch->type != type) {
        ch->type = type;
        probe_out(PROBE_REC_INFO, "%d,%" PRId64 ",radar,%s", ch->id, t, probe_radar_type_name(type));
    }
}

// Decoded text of a reply, as an @I record
static void describe_ack(channel_t *ch, const hlk_ack_t *ack, int64_t t)
{
    char text[200];
    const char *key = "reply";
    if (ack->status != 0) {
        snprintf(text, sizeof(text), "command 0x%04X refused (status %u)", ack->command, ack->status);
    } else {
        text[0] = '\0';
        switch (ack->command) {
        case HLK_CMD_ENABLE_CONFIG: {
            hlk_config_info_t info;
            if (hlk_decode_enable_config(ack, &info) == HLK_OK) {
                key = "config";
                snprintf(text, sizeof(text), "protocol %u, buffer %u", info.protocol_version, info.buffer_size);
            }
            break;
        }
        case HLK_CMD_READ_VERSION: {
            hlk_version_t version;
            if (hlk_decode_version(ack, &version) == HLK_OK) {
                char v[24];
                hlk_format_version(&version, v, sizeof(v));
                key = "version";
                snprintf(text, sizeof(text), "%s (type %u)", v, version.type);
            }
            break;
        }
        case HLK_CMD_READ_MAC: {
            uint8_t mac[HLK_MAC_LEN];
            bool on = false;
            if (hlk_decode_mac(ack, mac, &on) == HLK_OK) {
                key = "bluetooth";
                if (on) {
                    snprintf(text, sizeof(text), "on, MAC %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
                             mac[3], mac[4], mac[5]);
                } else {
                    snprintf(text, sizeof(text), "off");
                }
            }
            break;
        }
        case LD2410_CMD_READ_PARAMS: {
            ld2410_params_t p;
            if (ld2410_decode_params(ack, &p) == HLK_OK) {
                key = "params";
                int n = 0;
                appendf(text, sizeof(text), &n, "max gate moving %u still %u, no-one %u s, thresholds moving",
                        p.max_moving_gate, p.max_still_gate, p.no_one_duration_s);
                for (int g = 0; g < p.gates; g++) {
                    appendf(text, sizeof(text), &n, " %u", p.moving_threshold[g]);
                }
                appendf(text, sizeof(text), &n, " still");
                for (int g = 0; g < p.gates; g++) {
                    appendf(text, sizeof(text), &n, " %u", p.still_threshold[g]);
                }
            }
            break;
        }
        case LD2410_CMD_READ_RESOLUTION: {
            uint16_t gate_cm = 0;
            if (ld2410_decode_resolution(ack, &gate_cm) == HLK_OK) {
                key = "resolution";
                snprintf(text, sizeof(text), "%u cm per gate", gate_cm);
            }
            break;
        }
        case LD2410_CMD_READ_AUX_CONTROL: {
            ld2410_aux_control_t aux;
            if (ld2410_decode_aux_control(ack, &aux) == HLK_OK) {
                key = "aux_control";
                snprintf(text, sizeof(text), "light mode %u, threshold %u, OUT idle level %u", aux.light_mode,
                         aux.light_threshold, aux.out_idle_level);
            }
            break;
        }
        case LD2410_CMD_NOISE_STATUS: {
            uint16_t status = 0;
            if (ld2410_decode_noise_status(ack, &status) == HLK_OK) {
                key = "noise_detection";
                snprintf(text, sizeof(text), "%s", status == 0 ? "idle" : status == 1 ? "running" : "done");
            }
            break;
        }
        case LD2450_CMD_READ_TRACKING: {
            uint16_t mode = 0;
            if (ld2450_decode_tracking(ack, &mode) == HLK_OK) {
                key = "tracking";
                snprintf(text, sizeof(text), "%s target", mode == LD2450_TRACKING_SINGLE ? "single" : "multi");
            }
            break;
        }
        case LD2450_CMD_READ_ZONES: {
            ld2450_zones_t z;
            if (ld2450_decode_zones(ack, &z) == HLK_OK) {
                key = "zones";
                snprintf(text, sizeof(text), "type %u: (%d,%d)-(%d,%d) (%d,%d)-(%d,%d) (%d,%d)-(%d,%d) mm", z.type,
                         z.zone[0].x1_mm, z.zone[0].y1_mm, z.zone[0].x2_mm, z.zone[0].y2_mm, z.zone[1].x1_mm,
                         z.zone[1].y1_mm, z.zone[1].x2_mm, z.zone[1].y2_mm, z.zone[2].x1_mm, z.zone[2].y1_mm,
                         z.zone[2].x2_mm, z.zone[2].y2_mm);
            }
            break;
        }
        default:
            break;
        }
        if (text[0] == '\0') {
            snprintf(text, sizeof(text), "command 0x%04X ok, %u value bytes", ack->command, (unsigned)ack->value_len);
        }
    }
    probe_out(PROBE_REC_INFO, "%d,%" PRId64 ",%s,%s", ch->id, t, key, text);
}

static void on_frame(const hlk_frame_t *f, void *arg)
{
    channel_t *ch = (channel_t *)arg;
    // The last byte of the frame arrived this long before the bytes read last
    const size_t after = ch->chunk_len - f->input_offset + ch->bytes_after_chunk;
    const int64_t t = ch->read_time_us - (int64_t)(after + RX_TIMEOUT_SYMBOLS) * byte_us(ch);
    if (ch->silent) {
        ch->silent = false;
        probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,radar back after %" PRId64 " ms", t, ch->id,
                  (t - ch->last_frame_us) / 1000);
    }
    ch->last_frame_us = t;

    char head[48];
    snprintf(head, sizeof(head), "%d,%" PRIu32 ",%" PRId64, ch->id, ch->seq++, t);
    switch (f->kind) {
    case HLK_FRAME_LD2410_REPORT: {
        ld2410_report_t r;
        if (ld2410_decode_report(f->payload, f->payload_len, &r) == HLK_OK) {
            ch->reports++;
            set_type(ch, PROBE_RADAR_LD2410, t);
            taskENTER_CRITICAL(&s_lock);
            ch->last_ld2410 = r;
            ch->last_report_us = t;
            taskEXIT_CRITICAL(&s_lock);
        } else {
            ch->decode_errors++;
        }
        if (s_frames_on) {
            probe_out_hex(PROBE_REC_DATA, head, f->frame, f->frame_len);
        }
        break;
    }
    case HLK_FRAME_LD2450_REPORT: {
        ld2450_report_t r;
        if (ld2450_decode_report(f->payload, f->payload_len, &r) == HLK_OK) {
            ch->reports++;
            for (const ld2450_target_t &target : r.target) {
                ch->implausible += ld2450_target_plausible(&target) ? 0 : 1;
            }
            set_type(ch, PROBE_RADAR_LD2450, t);
            taskENTER_CRITICAL(&s_lock);
            ch->last_ld2450 = r;
            ch->last_report_us = t;
            taskEXIT_CRITICAL(&s_lock);
        } else {
            ch->decode_errors++;
        }
        if (s_frames_on) {
            probe_out_hex(PROBE_REC_DATA, head, f->frame, f->frame_len);
        }
        break;
    }
    case HLK_FRAME_ACK: {
        ch->acks++;
        probe_out_hex(PROBE_REC_ACK, head, f->frame, f->frame_len);
        hlk_ack_t ack;
        if (hlk_decode_ack(f->payload, f->payload_len, &ack) == HLK_OK) {
            if (ch->awaiting && ack.command == ch->awaiting) {
                ch->ack_seen = true;
                ch->ack_status = ack.status;
            }
            describe_ack(ch, &ack, t);
        } else {
            ch->decode_errors++;
        }
        break;
    }
    case HLK_FRAME_COMMAND:
        ch->command_echoes++;
        probe_out_hex(PROBE_REC_ACK, head, f->frame, f->frame_len);
        break;
    }
}

static void read_bytes(channel_t *ch, size_t size)
{
    uint8_t buf[READ_CHUNK];
    ch->read_time_us = now_us();
    while (size > 0) {
        const int got = uart_read_bytes(ch->uart, buf, size < sizeof(buf) ? size : sizeof(buf), 0);
        if (got <= 0) {
            break;
        }
        size = size > (size_t)got ? size - got : 0;
        if (ch->dump) {
            for (int pos = 0; pos < got; pos += DUMP_CHUNK) {
                char head[32];
                snprintf(head, sizeof(head), "%d,%" PRId64, ch->id, ch->read_time_us);
                probe_out_hex(PROBE_REC_RAW, head, &buf[pos], got - pos < DUMP_CHUNK ? got - pos : DUMP_CHUNK);
            }
        }
        ch->chunk_len = (size_t)got;
        ch->bytes_after_chunk = size;
        hlk_parser_feed(&ch->parser, buf, (size_t)got, on_frame, ch);
    }
}

static void handle_event(channel_t *ch, const uart_event_t *ev)
{
    switch (ev->type) {
    case UART_DATA:
        read_bytes(ch, ev->size);
        break;
    case UART_FIFO_OVF:
    case UART_BUFFER_FULL:
        ch->uart_overflows++;
        uart_flush_input(ch->uart);
        xQueueReset(ch->events);
        break;
    case UART_FRAME_ERR:
    case UART_PARITY_ERR:
    case UART_BREAK:
        ch->uart_errors++; // often a baud rate mismatch
        break;
    default:
        break;
    }
}

typedef bool (*done_fn_t)(const channel_t *ch);

static bool ack_arrived(const channel_t *ch)
{
    return ch->ack_seen;
}

// Processes the UART for up to ms, or until done() says so.
static void pump(channel_t *ch, uint32_t ms, done_fn_t done)
{
    const int64_t end = now_us() + (int64_t)ms * 1000;
    for (int64_t t = now_us(); t < end; t = now_us()) {
        if (done && done(ch)) {
            return;
        }
        TickType_t wait = pdMS_TO_TICKS((end - t + 999) / 1000);
        uart_event_t ev;
        if (xQueueReceive(ch->events, &ev, wait ? wait : 1) == pdTRUE) {
            handle_event(ch, &ev);
        }
        if (!ch->silent && !ch->awaiting && ch->last_frame_us && now_us() - ch->last_frame_us > kSilentUs) {
            ch->silent = true;
            probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,radar silent", now_us(), ch->id);
        }
    }
}

static void apply_baud(channel_t *ch, uint32_t baud)
{
    ch->baud = baud;
    uart_set_baudrate(ch->uart, baud);
    uart_flush_input(ch->uart);
    xQueueReset(ch->events);
    hlk_parser_init(&ch->parser);
    probe_out(PROBE_REC_CHANNEL, "%d,%" PRId64 ",uart=%d,rx=%d,tx=%d,baud=%" PRIu32, ch->id, now_us(), ch->id,
              ch->rx_gpio, ch->tx_gpio, baud);
}

// Sends a command and waits for its ACK: 0 accepted, 1 refused, -1 no answer.
static int send_command(channel_t *ch, uint16_t command, const uint8_t *value, size_t value_len, int attempts,
                        bool quiet)
{
    uint8_t frame[HLK_MAX_FRAME];
    const size_t n = hlk_build_command(command, value, value_len, frame, sizeof(frame));
    if (n == 0) {
        return -1;
    }
    for (int attempt = 0; attempt < attempts; attempt++) {
        ch->awaiting = command;
        ch->ack_seen = false;
        char head[32];
        snprintf(head, sizeof(head), "%d,%" PRId64, ch->id, now_us());
        probe_out_hex(PROBE_REC_TX, head, frame, n);
        uart_write_bytes(ch->uart, frame, n);
        pump(ch, kAckTimeoutMs, ack_arrived);
        if (ch->ack_seen) {
            ch->awaiting = 0;
            return ch->ack_status == 0 ? 0 : 1;
        }
    }
    ch->awaiting = 0;
    if (!quiet) {
        probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,no answer to command 0x%04X", now_us(), ch->id, command);
    }
    return -1;
}

// After the radar restarts or is powered on: engineering mode is off again
static void after_restart(channel_t *ch)
{
    pump(ch, kRestartWaitMs, NULL);
    if (ch->type == PROBE_RADAR_LD2410 && s_engineering_wanted) {
        if (send_command(ch, HLK_CMD_ENABLE_CONFIG, k_one, sizeof(k_one), 3, false) == 0) {
            send_command(ch, LD2410_CMD_ENGINEERING_ON, NULL, 0, 2, false);
            send_command(ch, HLK_CMD_END_CONFIG, NULL, 0, 3, false);
        }
    }
}

// Runs the steps between enable and end configuration. restarts: the last
// step restarts the radar, which leaves the configuration mode by itself.
static void run_steps(channel_t *ch, const char *name, const step_t *steps, int count, bool restarts)
{
    probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,job %s start", now_us(), ch->id, name);
    if (send_command(ch, HLK_CMD_ENABLE_CONFIG, k_one, sizeof(k_one), 3, false) != 0) {
        send_command(ch, HLK_CMD_END_CONFIG, NULL, 0, 1, true); // in case the radar did enter it
        probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,job %s failed: no configuration mode", now_us(), ch->id, name);
        return;
    }
    int accepted = 0;
    int refused = 0;
    int unanswered = 0;
    for (int i = 0; i < count; i++) {
        const int r = send_command(ch, steps[i].command, steps[i].value, steps[i].value_len, 2, false);
        accepted += r == 0;
        refused += r == 1;
        unanswered += r < 0;
    }
    if (!restarts) {
        send_command(ch, HLK_CMD_END_CONFIG, NULL, 0, 3, false);
    }
    probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,job %s done: %d accepted, %d refused, %d unanswered", now_us(),
              ch->id, name, accepted, refused, unanswered);
}

static void run_info(channel_t *ch, bool all)
{
    // Read-only commands; "all" also sends those of the other radar type
    static const step_t k_common[] = {
        { HLK_CMD_READ_VERSION, {}, 0 },
        { HLK_CMD_READ_MAC, { 0x01, 0x00 }, 2 },
    };
    static const step_t k_ld2410[] = {
        { LD2410_CMD_READ_PARAMS, {}, 0 },
        { LD2410_CMD_READ_RESOLUTION, {}, 0 },
        { LD2410_CMD_READ_AUX_CONTROL, {}, 0 },
        { LD2410_CMD_NOISE_STATUS, {}, 0 },
    };
    static const step_t k_ld2450[] = {
        { LD2450_CMD_READ_TRACKING, {}, 0 },
        { LD2450_CMD_READ_ZONES, {}, 0 },
    };
    step_t steps[8];
    int n = 0;
    for (const step_t &s : k_common) {
        steps[n++] = s;
    }
    const bool unknown = ch->type == PROBE_RADAR_UNKNOWN;
    if (all || unknown || ch->type == PROBE_RADAR_LD2410) {
        for (const step_t &s : k_ld2410) {
            steps[n++] = s;
        }
    }
    if (all || unknown || ch->type == PROBE_RADAR_LD2450) {
        for (const step_t &s : k_ld2450) {
            steps[n++] = s;
        }
    }
    run_steps(ch, all ? "info_all" : "info", steps, n, false);
}

static void scan(channel_t *ch)
{
    static const uint32_t k_rates[] = { 256000, 115200, 460800, 230400, 57600, 38400, 19200, 9600 };
    probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,baud scan start", now_us(), ch->id);
    for (uint32_t rate : k_rates) {
        apply_baud(ch, rate);
        const uint32_t before = ch->reports;
        pump(ch, kScanListenMs, NULL);
        bool found = ch->reports - before >= 2;
        // A radar left in configuration mode sends no reports but answers commands
        if (!found && send_command(ch, HLK_CMD_ENABLE_CONFIG, k_one, sizeof(k_one), 1, true) == 0) {
            send_command(ch, HLK_CMD_END_CONFIG, NULL, 0, 2, true);
            found = true;
        }
        if (found) {
            probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,baud scan: radar at %" PRIu32, now_us(), ch->id, rate);
            return;
        }
    }
    apply_baud(ch, CONFIG_PROBE_BAUD);
    probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,baud scan: no radar answered", now_us(), ch->id);
}

static void run_job(channel_t *ch, const job_req_t *job)
{
    step_t step = {};
    switch (job->kind) {
    case PROBE_JOB_INFO:
    case PROBE_JOB_INFO_ALL:
        run_info(ch, job->kind == PROBE_JOB_INFO_ALL);
        break;
    case PROBE_JOB_ENGINEERING:
        step.command = job->arg ? LD2410_CMD_ENGINEERING_ON : LD2410_CMD_ENGINEERING_OFF;
        run_steps(ch, job->arg ? "engineering_on" : "engineering_off", &step, 1, false);
        break;
    case PROBE_JOB_TRACKING: {
        const step_t steps[] = {
            { (uint16_t)(job->arg == LD2450_TRACKING_SINGLE ? LD2450_CMD_SINGLE_TARGET : LD2450_CMD_MULTI_TARGET), {},
              0 },
            { LD2450_CMD_READ_TRACKING, {}, 0 },
        };
        run_steps(ch, "tracking", steps, 2, false);
        break;
    }
    case PROBE_JOB_BLUETOOTH: {
        step_t steps[2] = { { HLK_CMD_BLUETOOTH, {}, 2 }, { HLK_CMD_RESTART, {}, 0 } };
        memcpy(steps[0].value, job->arg ? k_one : k_zero, 2);
        run_steps(ch, job->arg ? "bluetooth_on" : "bluetooth_off", steps, 2, true);
        after_restart(ch);
        break;
    }
    case PROBE_JOB_RESTART:
        step.command = HLK_CMD_RESTART;
        run_steps(ch, "restart", &step, 1, true);
        after_restart(ch);
        break;
    case PROBE_JOB_SCAN:
        scan(ch);
        break;
    case PROBE_JOB_SET_BAUD: {
        step_t steps[2] = { { HLK_CMD_SET_BAUD, {}, 2 }, { HLK_CMD_RESTART, {}, 0 } };
        hlk_put_u16(steps[0].value, hlk_baud_index(job->arg));
        run_steps(ch, "set_baud", steps, 2, true);
        apply_baud(ch, job->arg);
        after_restart(ch);
        break;
    }
    case PROBE_JOB_POWERED:
        after_restart(ch);
        break;
    case PROBE_JOB_COMMAND: {
        // Value up to PROBE_JOB_VALUE_MAX, so not through step_t
        probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,job command 0x%04" PRIX32 " start", now_us(), ch->id, job->arg);
        if (send_command(ch, HLK_CMD_ENABLE_CONFIG, k_one, sizeof(k_one), 3, false) == 0) {
            const int r = send_command(ch, (uint16_t)job->arg, job->value, job->value_len, 2, false);
            if (job->arg == HLK_CMD_RESTART && r == 0) {
                after_restart(ch);
            } else {
                send_command(ch, HLK_CMD_END_CONFIG, NULL, 0, 3, false);
            }
            probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,job command 0x%04" PRIX32 " done: %s", now_us(), ch->id,
                      job->arg, r == 0 ? "accepted" : r == 1 ? "refused" : "unanswered");
        } else {
            send_command(ch, HLK_CMD_END_CONFIG, NULL, 0, 1, true);
            probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,job command 0x%04" PRIX32 " failed: no configuration mode",
                      now_us(), ch->id, job->arg);
        }
        break;
    }
    }
}

static void channel_task(void *arg)
{
    channel_t *ch = (channel_t *)arg;
    pump(ch, kDetectMs, [](const channel_t *c) { return c->reports > 0; });
    if (ch->reports == 0 && CONFIG_PROBE_BAUD_SCAN) {
        scan(ch);
    }
    if (CONFIG_PROBE_INFO_AT_BOOT) {
        run_info(ch, false);
    }
    if (ch->type == PROBE_RADAR_LD2410 && s_engineering_wanted) {
        step_t step = { LD2410_CMD_ENGINEERING_ON, {}, 0 };
        run_steps(ch, "engineering_on", &step, 1, false);
    }
    for (;;) {
        job_req_t job;
        if (xQueueReceive(ch->jobs, &job, 0) == pdTRUE) {
            run_job(ch, &job);
        }
        pump(ch, 50, NULL);
    }
}

esp_err_t probe_radar_start(int index, int uart, int rx_gpio, int tx_gpio, uint32_t baud)
{
    if (index < 0 || index >= PROBE_CHANNELS) {
        return ESP_ERR_INVALID_ARG;
    }
    channel_t *ch = &s_channels[index];
    memset(ch, 0, sizeof(*ch));
    ch->id = uart;
    ch->uart = (uart_port_t)uart;
    ch->rx_gpio = rx_gpio;
    ch->tx_gpio = tx_gpio;
    ch->baud = baud;
    hlk_parser_init(&ch->parser);

    uart_config_t config = {};
    config.baud_rate = (int)baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    ch->jobs = xQueueCreate(JOB_QUEUE_LEN, sizeof(job_req_t));
    if (!ch->jobs) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = uart_driver_install(ch->uart, RX_BUFFER_SIZE, 0, EVENT_QUEUE_LEN, &ch->events, 0);
    if (err == ESP_OK) {
        err = uart_param_config(ch->uart, &config);
    }
    if (err == ESP_OK) {
        err = uart_set_pin(ch->uart, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        err = uart_set_rx_timeout(ch->uart, RX_TIMEOUT_SYMBOLS);
    }
    if (err == ESP_OK) {
        probe_out(PROBE_REC_CHANNEL, "%d,%" PRId64 ",uart=%d,rx=%d,tx=%d,baud=%" PRIu32, ch->id, now_us(), uart,
                  rx_gpio, tx_gpio, baud);
        char name[16];
        snprintf(name, sizeof(name), "radar_u%d", uart);
        ch->running = xTaskCreate(channel_task, name, TASK_STACK, ch, TASK_PRIO, NULL) == pdPASS;
        err = ch->running ? ESP_OK : ESP_ERR_NO_MEM;
    }
    return err;
}

bool probe_radar_job(int index, probe_job_t job, uint32_t arg, const uint8_t *value, size_t value_len)
{
    if (index < 0 || index >= PROBE_CHANNELS || !s_channels[index].running || value_len > PROBE_JOB_VALUE_MAX) {
        return false;
    }
    job_req_t req = {};
    req.kind = job;
    req.arg = arg;
    if (value_len > 0) {
        memcpy(req.value, value, value_len);
    }
    req.value_len = (uint8_t)value_len;
    return xQueueSend(s_channels[index].jobs, &req, 0) == pdTRUE;
}

int probe_radar_find(const char *name)
{
    for (int i = 0; i < PROBE_CHANNELS; i++) {
        const channel_t *ch = &s_channels[i];
        if (!ch->running) {
            continue;
        }
        char id[8];
        snprintf(id, sizeof(id), "%d", ch->id);
        const char *type = probe_radar_type_name(ch->type);
        if (strcmp(name, id) == 0 || strcasecmp(name, type) == 0 ||
            (ch->type != PROBE_RADAR_UNKNOWN && strcmp(name, type + 2) == 0)) { // "2410" for "LD2410"
            return i;
        }
    }
    return -1;
}

probe_radar_type_t probe_radar_type(int index)
{
    return index >= 0 && index < PROBE_CHANNELS ? s_channels[index].type : PROBE_RADAR_UNKNOWN;
}

void probe_radar_want_engineering(bool on)
{
    s_engineering_wanted = on;
}

void probe_radar_set_baud(int index, uint32_t baud)
{
    // Changed by the console task while the channel task reads: the same as a
    // line glitch, the parser resynchronises.
    if (index >= 0 && index < PROBE_CHANNELS && s_channels[index].running) {
        channel_t *ch = &s_channels[index];
        ch->baud = baud;
        uart_set_baudrate(ch->uart, baud);
        probe_out(PROBE_REC_CHANNEL, "%d,%" PRId64 ",uart=%d,rx=%d,tx=%d,baud=%" PRIu32, ch->id, now_us(), ch->id,
                  ch->rx_gpio, ch->tx_gpio, baud);
    }
}

void probe_radar_set_dump(int index, bool on)
{
    if (index >= 0 && index < PROBE_CHANNELS) {
        s_channels[index].dump = on;
    }
}

void probe_radar_set_frames(bool on)
{
    s_frames_on = on;
}

void probe_radar_stats(int index)
{
    if (index < 0 || index >= PROBE_CHANNELS || !s_channels[index].running) {
        return;
    }
    const channel_t *ch = &s_channels[index];
    const hlk_parser_stats_t &ps = ch->parser.stats;
    const int64_t t = now_us();
    probe_out(PROBE_REC_STATS,
              "%d,%" PRId64 ",radar=%s,baud=%" PRIu32 ",frames=%" PRIu32 ",reports=%" PRIu32 ",acks=%" PRIu32
              ",cmd_echo=%" PRIu32 ",garbage=%" PRIu32 ",bad_len=%" PRIu32 ",bad_footer=%" PRIu32
              ",decode_err=%" PRIu32 ",implausible=%" PRIu32 ",uart_ovf=%" PRIu32 ",uart_err=%" PRIu32
              ",age_ms=%" PRId64,
              ch->id, t, probe_radar_type_name(ch->type), ch->baud, ps.frames, ch->reports, ch->acks,
              ch->command_echoes, ps.garbage_bytes, ps.bad_length, ps.bad_footer, ch->decode_errors, ch->implausible,
              ch->uart_overflows, ch->uart_errors, ch->last_frame_us ? (t - ch->last_frame_us) / 1000 : -1);
}

void probe_radar_summary(int index, char *buf, size_t len)
{
    buf[0] = '\0';
    if (index < 0 || index >= PROBE_CHANNELS || !s_channels[index].running) {
        return;
    }
    channel_t *ch = &s_channels[index];
    taskENTER_CRITICAL(&s_lock);
    const ld2410_report_t a = ch->last_ld2410;
    const ld2450_report_t b = ch->last_ld2450;
    const int64_t at = ch->last_report_us;
    taskEXIT_CRITICAL(&s_lock);

    int n = 0;
    appendf(buf, len, &n, "u%d %s", ch->id, probe_radar_type_name(ch->type));
    if (at == 0 || now_us() - at > kSummaryStaleUs) {
        appendf(buf, len, &n, " no data");
        return;
    }
    if (ch->type == PROBE_RADAR_LD2410) {
        appendf(buf, len, &n, " st=%u mov %ucm/%u still %ucm/%u", a.state, a.moving_cm, a.moving_energy, a.still_cm,
                a.still_energy);
        if (a.type == LD2410_TYPE_ENGINEERING) {
            appendf(buf, len, &n, " m:");
            for (int g = 0; g < a.moving_gates; g++) {
                appendf(buf, len, &n, " %u", a.moving_gate_energy[g]);
            }
            appendf(buf, len, &n, " s:");
            for (int g = 0; g < a.still_gates; g++) {
                appendf(buf, len, &n, " %u", a.still_gate_energy[g]);
            }
        }
    } else if (ch->type == PROBE_RADAR_LD2450) {
        appendf(buf, len, &n, " n=%u", b.count);
        for (const ld2450_target_t &t : b.target) {
            if (t.present) {
                appendf(buf, len, &n, " (%d,%d,%d)", t.x_mm, t.y_mm, t.speed_cm_s);
            }
        }
    }
}
