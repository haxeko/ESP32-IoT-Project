/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Radar probe (Phase 1): reads an HLK-LD2410 and an HLK-LD2450 on two UARTs of
// an ESP32-S3 and writes every frame, with its arrival time, to the USB serial
// port as text records (probe_out.h). tools/capture.py stores them; decode.py
// and report.py turn them into CSV files and the Phase 1 measurements. Commands
// typed on the same port (help) query or configure the radars.

#include <driver/gpio.h>
#include <esp_app_desc.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_idf_version.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <inttypes.h>
#include <sdkconfig.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include <driver/usb_serial_jtag.h>
#include <driver/usb_serial_jtag_vfs.h>
#else
#include <driver/uart.h>
#include <driver/uart_vfs.h>
#endif

#include "hlk_frame.h"
#include "ld2450_proto.h"
#include "probe_out.h"
#include "probe_radar.h"

#define CONSOLE_TASK_STACK 6144
#define CONSOLE_TASK_PRIO 4
#define AUX_TASK_STACK 4096
#define AUX_TASK_PRIO 5
#define AUX_PERIOD_MS 10
#define COMMAND_MAX 160

typedef struct {
    int uart;
    int rx;
    int tx;
    int power;
} channel_pins_t;

static const channel_pins_t k_channels[PROBE_CHANNELS] = {
    { 1, CONFIG_PROBE_CH1_RX_GPIO, CONFIG_PROBE_CH1_TX_GPIO, CONFIG_PROBE_CH1_POWER_GPIO },
    { 2, CONFIG_PROBE_CH2_RX_GPIO, CONFIG_PROBE_CH2_TX_GPIO, CONFIG_PROBE_CH2_POWER_GPIO },
};

static volatile bool s_summary_on = CONFIG_PROBE_SUMMARY;

static int64_t now_us(void)
{
    return esp_timer_get_time();
}

// Pins the module or the console already use: SPI flash (26-32), octal
// flash/PSRAM of the R8 / N16R8 modules (33-37), USB (19, 20) or the UART0
// console (43, 44). Strapping pins (0, 3, 45, 46) work but are better avoided.
static const char *pin_problem(int gpio)
{
    if (gpio < 0 || gpio > 48 || (gpio > 21 && gpio < 26)) {
        return "no such GPIO";
    }
    if (gpio >= 26 && gpio <= 32) {
        return "used by the SPI flash";
    }
    if (gpio >= 33 && gpio <= 37) {
        return "used by the octal PSRAM/flash of R8 and N16R8 modules";
    }
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    if (gpio == 19 || gpio == 20) {
        return "USB D-/D+ of the console";
    }
#else
    if (gpio == 43 || gpio == 44) {
        return "UART0 console";
    }
#endif
    return nullptr;
}

static void set_power(int index, bool on)
{
    const int gpio = k_channels[index].power;
    if (gpio >= 0) {
        gpio_set_level((gpio_num_t)gpio, on == (bool)CONFIG_PROBE_POWER_ACTIVE_HIGH);
        probe_out(PROBE_REC_EVENT, "%" PRId64 ",u%d,power %s", now_us(), k_channels[index].uart, on ? "on" : "off");
    }
}

static void reply(bool ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void reply(bool ok, const char *fmt, ...)
{
    char text[120];
    va_list args;
    va_start(args, fmt);
    vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    probe_out(PROBE_REC_REPLY, "%" PRId64 ",%s %s", now_us(), ok ? "ok" : "error", text);
}

static bool parse_on_off(const char *word, bool *on)
{
    if (word && strcasecmp(word, "on") == 0) {
        *on = true;
        return true;
    }
    if (word && strcasecmp(word, "off") == 0) {
        *on = false;
        return true;
    }
    return false;
}

// Channel named by word, or the only channel with a radar of type when word is null
static int channel_arg(const char *word, probe_radar_type_t type)
{
    if (word) {
        return probe_radar_find(word);
    }
    for (int i = 0; i < PROBE_CHANNELS; i++) {
        if (probe_radar_type(i) == type) {
            return i;
        }
    }
    return -1;
}

static void queue_job(int index, probe_job_t job, uint32_t arg, const uint8_t *value, size_t value_len,
                      const char *what)
{
    if (index < 0) {
        reply(false, "%s: no such radar channel", what);
    } else if (!probe_radar_job(index, job, arg, value, value_len)) {
        reply(false, "%s: channel busy, try again", what);
    } else {
        reply(true, "%s queued on u%d", what, k_channels[index].uart);
    }
}

static const char *k_help[] = {
    "info [ch|all]          read-only queries (version, settings, Bluetooth); all = both radar types",
    "eng [ch] on|off        LD2410 engineering mode (energy of every gate)",
    "track single|multi     LD2450 tracking mode (stored in the radar)",
    "bt <ch> on|off         radar Bluetooth (stored, the radar restarts)",
    "restart <ch>           restart the radar",
    "scan <ch>              look for the radar on every baud rate",
    "baud <ch> <rate>       ESP32 UART rate only",
    "setbaud <ch> <rate>    store a baud rate in the radar (restarts it), then follow it",
    "cmd <ch> <word> [hex]  any command between enable/end configuration (experts)",
    "raw on|off             @D/@A frame records (ACKs always)",
    "dump <ch> on|off       raw UART bytes as @R records",
    "hum on|off             1 s summary @H",
    "stats                  counters now (@S)",
    "mark <text>            event marker for labelling experiments",
    "sync <pc_unix_ms>      PC clock, answered with @T",
    "pwr <ch> on|off|cycle  radar power switch (CONFIG_PROBE_CHx_POWER_GPIO)",
    "ch: UART number (1, 2) or radar (2410, 2450)",
};

static void handle_command(char *line, int64_t received_us)
{
    char *save = nullptr;
    char *cmd = strtok_r(line, " \t", &save);
    if (!cmd) {
        return;
    }
    char *a1 = strtok_r(nullptr, " \t", &save);
    char *a2 = strtok_r(nullptr, " \t", &save);
    char *a3 = strtok_r(nullptr, " \t", &save);
    bool on = false;

    if (strcmp(cmd, "help") == 0) {
        for (const char *h : k_help) {
            probe_out(PROBE_REC_REPLY, "%" PRId64 ",ok %s", now_us(), h);
        }
    } else if (strcmp(cmd, "info") == 0) {
        if (a1 && strcmp(a1, "all") == 0) {
            for (int i = 0; i < PROBE_CHANNELS; i++) {
                queue_job(i, PROBE_JOB_INFO_ALL, 0, nullptr, 0, "info all");
            }
        } else if (a1) {
            queue_job(probe_radar_find(a1), PROBE_JOB_INFO, 0, nullptr, 0, "info");
        } else {
            for (int i = 0; i < PROBE_CHANNELS; i++) {
                queue_job(i, PROBE_JOB_INFO, 0, nullptr, 0, "info");
            }
        }
    } else if (strcmp(cmd, "eng") == 0) {
        // "eng on" or "eng 1 on"
        const bool with_channel = a2 != nullptr;
        if (!parse_on_off(with_channel ? a2 : a1, &on)) {
            reply(false, "usage: eng [ch] on|off");
            return;
        }
        probe_radar_want_engineering(on);
        queue_job(channel_arg(with_channel ? a1 : nullptr, PROBE_RADAR_LD2410), PROBE_JOB_ENGINEERING, on, nullptr, 0,
                  on ? "engineering on" : "engineering off");
    } else if (strcmp(cmd, "track") == 0 && a1 && (strcmp(a1, "single") == 0 || strcmp(a1, "multi") == 0)) {
        queue_job(channel_arg(nullptr, PROBE_RADAR_LD2450), PROBE_JOB_TRACKING,
                  strcmp(a1, "single") == 0 ? LD2450_TRACKING_SINGLE : LD2450_TRACKING_MULTI, nullptr, 0, "tracking");
    } else if (strcmp(cmd, "bt") == 0 && a1 && parse_on_off(a2, &on)) {
        queue_job(probe_radar_find(a1), PROBE_JOB_BLUETOOTH, on, nullptr, 0, on ? "bluetooth on" : "bluetooth off");
    } else if (strcmp(cmd, "restart") == 0 && a1) {
        queue_job(probe_radar_find(a1), PROBE_JOB_RESTART, 0, nullptr, 0, "restart");
    } else if (strcmp(cmd, "scan") == 0 && a1) {
        queue_job(probe_radar_find(a1), PROBE_JOB_SCAN, 0, nullptr, 0, "scan");
    } else if ((strcmp(cmd, "baud") == 0 || strcmp(cmd, "setbaud") == 0) && a1 && a2) {
        const uint32_t baud = (uint32_t)strtoul(a2, nullptr, 10);
        const int index = probe_radar_find(a1);
        if (hlk_baud_index(baud) == 0) {
            reply(false, "the radars offer 9600 19200 38400 57600 115200 230400 256000 460800");
        } else if (index < 0) {
            reply(false, "no such radar channel");
        } else if (strcmp(cmd, "baud") == 0) {
            probe_radar_set_baud(index, baud);
            reply(true, "u%d at %" PRIu32 " baud", k_channels[index].uart, baud);
        } else {
            queue_job(index, PROBE_JOB_SET_BAUD, baud, nullptr, 0, "set baud");
        }
    } else if (strcmp(cmd, "cmd") == 0 && a1 && a2) {
        uint8_t value[PROBE_JOB_VALUE_MAX];
        size_t len = 0;
        const char *hex = a3 ? a3 : "";
        bool ok = strlen(hex) % 2 == 0 && strlen(hex) / 2 <= sizeof(value);
        for (size_t i = 0; ok && hex[i]; i += 2) {
            char byte[3] = { hex[i], hex[i + 1], 0 };
            char *end = nullptr;
            value[len++] = (uint8_t)strtoul(byte, &end, 16);
            ok = end == &byte[2];
        }
        if (!ok) {
            reply(false, "usage: cmd <ch> <word, e.g. 0x00A0> [value hex, e.g. 0100]");
            return;
        }
        queue_job(probe_radar_find(a1), PROBE_JOB_COMMAND, (uint32_t)strtoul(a2, nullptr, 0) & 0xFFFF, value, len,
                  "command");
    } else if (strcmp(cmd, "raw") == 0 && parse_on_off(a1, &on)) {
        probe_radar_set_frames(on);
        reply(true, "frame records %s", on ? "on" : "off");
    } else if (strcmp(cmd, "dump") == 0 && a1 && parse_on_off(a2, &on)) {
        const int index = probe_radar_find(a1);
        probe_radar_set_dump(index, on);
        reply(index >= 0, "dump %s", on ? "on" : "off");
    } else if (strcmp(cmd, "hum") == 0 && parse_on_off(a1, &on)) {
        s_summary_on = on;
        reply(true, "summary %s", on ? "on" : "off");
    } else if (strcmp(cmd, "stats") == 0) {
        for (int i = 0; i < PROBE_CHANNELS; i++) {
            probe_radar_stats(i);
        }
    } else if (strcmp(cmd, "mark") == 0) {
        // The rest of the line, commas replaced so the record keeps its fields
        char text[COMMAND_MAX];
        snprintf(text, sizeof(text), "%s%s%s%s%s", a1 ? a1 : "", a2 ? " " : "", a2 ? a2 : "", a3 ? " " : "",
                 a3 ? a3 : "");
        if (save && *save) {
            strncat(text, " ", sizeof(text) - strlen(text) - 1);
            strncat(text, save, sizeof(text) - strlen(text) - 1);
        }
        for (char *c = text; *c; c++) {
            *c = (*c == ',' || *c == '*') ? ';' : *c;
        }
        probe_out(PROBE_REC_EVENT, "%" PRId64 ",mark,%s", received_us, text);
    } else if (strcmp(cmd, "sync") == 0 && a1) {
        probe_out(PROBE_REC_SYNC, "%" PRId64 ",%s", received_us, a1);
    } else if (strcmp(cmd, "pwr") == 0 && a1 && a2) {
        const int index = probe_radar_find(a1);
        if (index < 0 || k_channels[index].power < 0) {
            reply(false, "no power switch on this channel");
        } else if (strcmp(a2, "cycle") == 0) {
            set_power(index, false);
            vTaskDelay(pdMS_TO_TICKS(1000));
            set_power(index, true);
            probe_radar_job(index, PROBE_JOB_POWERED, 0, nullptr, 0);
            reply(true, "power cycled");
        } else if (parse_on_off(a2, &on)) {
            set_power(index, on);
            if (on) {
                probe_radar_job(index, PROBE_JOB_POWERED, 0, nullptr, 0);
            }
            reply(true, "power %s", on ? "on" : "off");
        } else {
            reply(false, "usage: pwr <ch> on|off|cycle");
        }
    } else {
        reply(false, "unknown command, try help");
    }
}

static void console_task(void *arg)
{
    char line[COMMAND_MAX];
    size_t len = 0;
    int64_t first_char_us = 0;
    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (len == 0) {
            first_char_us = now_us();
        }
        if (c == '\r' || c == '\n') {
            if (len > 0) {
                line[len] = '\0';
                handle_command(line, first_char_us);
                len = 0;
            }
        } else if (len < sizeof(line) - 1) {
            line[len++] = c;
        }
    }
}

// Button markers, LD2410 OUT pin changes, counters and the 1 s summary
static void aux_task(void *arg)
{
    int button_level = 1;
    int button_count = 0;
    int out_level = -1;
    int64_t next_summary = now_us() + 1000 * 1000;
    int64_t next_stats = now_us() + (int64_t)CONFIG_PROBE_STATS_PERIOD_SEC * 1000 * 1000;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(AUX_PERIOD_MS));
        const int64_t t = now_us();
        if (CONFIG_PROBE_BUTTON_GPIO >= 0) {
            const int level = gpio_get_level((gpio_num_t)CONFIG_PROBE_BUTTON_GPIO);
            if (level != button_level) {
                button_level = level;
                if (level == 0) {
                    probe_out(PROBE_REC_EVENT, "%" PRId64 ",button,press %d", t, ++button_count);
                }
            }
        }
        if (CONFIG_PROBE_LD2410_OUT_GPIO >= 0) {
            const int level = gpio_get_level((gpio_num_t)CONFIG_PROBE_LD2410_OUT_GPIO);
            if (level != out_level) {
                out_level = level;
                probe_out(PROBE_REC_EVENT, "%" PRId64 ",out_pin,%d", t, level);
            }
        }
        if (s_summary_on && t >= next_summary) {
            next_summary += 1000 * 1000;
            char a[200];
            char b[120];
            probe_radar_summary(0, a, sizeof(a));
            probe_radar_summary(1, b, sizeof(b));
            probe_out(PROBE_REC_HUMAN, "%" PRId64 ",%s | %s", t, a, b);
        }
        if (CONFIG_PROBE_STATS_PERIOD_SEC > 0 && t >= next_stats) {
            next_stats += (int64_t)CONFIG_PROBE_STATS_PERIOD_SEC * 1000 * 1000;
            for (int i = 0; i < PROBE_CHANNELS; i++) {
                probe_radar_stats(i);
            }
            probe_out(PROBE_REC_STATS, "0,%" PRId64 ",out_drops=%" PRIu32 ",heap=%" PRIu32 ",min_heap=%" PRIu32, t,
                      probe_out_dropped(), esp_get_free_heap_size(), esp_get_minimum_free_heap_size());
        }
    }
}

static void console_init(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&config));
    usb_serial_jtag_vfs_use_driver();
#else
    ESP_ERROR_CHECK(uart_driver_install((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, 1024, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#endif
    setvbuf(stdin, NULL, _IONBF, 0);
}

static void input_pin(int gpio, bool pull_up)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_up ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
}

extern "C" void app_main(void)
{
    console_init();
    ESP_ERROR_CHECK(probe_out_init());

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    const esp_app_desc_t *app = esp_app_get_description();
    probe_out(PROBE_REC_BOOT, "%" PRId64 ",probe=%s,idf=%s,chip=%s rev %u.%u cores %u,flash=%" PRIu32 "MB,reset=%d",
              now_us(), app->version, esp_get_idf_version(), CONFIG_IDF_TARGET, chip.revision / 100,
              chip.revision % 100, chip.cores, flash_size >> 20, (int)esp_reset_reason());

    if (CONFIG_PROBE_BUTTON_GPIO >= 0) {
        input_pin(CONFIG_PROBE_BUTTON_GPIO, true);
    }
    if (CONFIG_PROBE_LD2410_OUT_GPIO >= 0) {
        input_pin(CONFIG_PROBE_LD2410_OUT_GPIO, false);
    }
    for (int i = 0; i < PROBE_CHANNELS; i++) {
        const channel_pins_t &c = k_channels[i];
        if (c.power >= 0) {
            gpio_reset_pin((gpio_num_t)c.power);
            gpio_set_direction((gpio_num_t)c.power, GPIO_MODE_OUTPUT);
            set_power(i, true);
        }
    }
    if (CONFIG_PROBE_CH1_POWER_GPIO >= 0 || CONFIG_PROBE_CH2_POWER_GPIO >= 0) {
        vTaskDelay(pdMS_TO_TICKS(500)); // let the radars start
    }

    for (int i = 0; i < PROBE_CHANNELS; i++) {
        const channel_pins_t &c = k_channels[i];
        const char *problem = pin_problem(c.rx);
        problem = problem ? problem : pin_problem(c.tx);
        if (problem) {
            probe_out(PROBE_REC_EVENT, "%" PRId64 ",config,u%d not started: pin %s", now_us(), c.uart, problem);
            continue;
        }
        const esp_err_t err = probe_radar_start(i, c.uart, c.rx, c.tx, CONFIG_PROBE_BAUD);
        if (err != ESP_OK) {
            probe_out(PROBE_REC_EVENT, "%" PRId64 ",config,u%d not started: %s", now_us(), c.uart,
                      esp_err_to_name(err));
        }
    }
    xTaskCreate(console_task, "console", CONSOLE_TASK_STACK, NULL, CONSOLE_TASK_PRIO, NULL);
    xTaskCreate(aux_task, "aux", AUX_TASK_STACK, NULL, AUX_TASK_PRIO, NULL);
}
