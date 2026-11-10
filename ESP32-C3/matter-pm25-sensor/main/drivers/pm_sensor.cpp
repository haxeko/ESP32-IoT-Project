/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// UART driver for Plantower PMS5003/PMS7003/PMSA003 and Nova Fitness SDS011.
// Both sensors stream frames at 9600 8N1 in their default (active) mode.

#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

#include "pm_sensor.h"

static const char *TAG = "pm_sensor";

#define PM_SENSOR_UART_NUM      UART_NUM_1
#define PM_SENSOR_BAUD_RATE     9600
#define PM_SENSOR_RX_BUF_SIZE   256
#define PM_SENSOR_TASK_STACK    4096
#define PM_SENSOR_TASK_PRIO     5

#if CONFIG_PM_SENSOR_MODEL_PMS

// 32-byte frame: 42 4D LEN(2)=28 DATA(13 x uint16 BE) CHECKSUM(2)
#define FRAME_LEN               32
// The fan needs ~30 s after power-up before readings are stable.
#define WARMUP_MS               30000
// Sensor sends a frame every 0.2-2.3 s.
#define DATA_TIMEOUT_MS         30000

#if CONFIG_PM_SENSOR_PMS_PERIOD_SEC > 0
// Duty cycle: wake up, warm up, sample for a few seconds, then put the sensor
// to sleep (fan and laser off) until the next period. Extends sensor life.
#define DUTY_CYCLE              1
#define DUTY_SAMPLE_MS          5000
#define DUTY_PERIOD_MS          ((CONFIG_PM_SENSOR_PMS_PERIOD_SEC < 60 ? 60 : CONFIG_PM_SENSOR_PMS_PERIOD_SEC) * 1000)
// A sleeping sensor sends nothing. Frames that still come this long after the
// sleep command mean it did not take it (command lost on the wire, sensor
// plugged in or restarted meanwhile): the command is sent again, this often.
#define SLEEP_RETRY_MS          5000
#else
#define DUTY_CYCLE              0
#endif

static const uint8_t k_frame_header[] = { 0x42, 0x4D };

// A frame with another length is not a measurement: the answer to a command
// (4 bytes) or noise that looked like a header.
static bool is_measurement_frame(const uint8_t *f)
{
    return ((f[2] << 8) | f[3]) == FRAME_LEN - 4;
}

static void send_command(uint8_t cmd, uint16_t data)
{
    uint8_t frame[7] = { 0x42, 0x4D, cmd, (uint8_t)(data >> 8), (uint8_t)data, 0, 0 };
    uint16_t sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += frame[i];
    }
    frame[5] = sum >> 8;
    frame[6] = sum & 0xFF;
    uart_write_bytes(PM_SENSOR_UART_NUM, frame, sizeof(frame));
}

static void sensor_setup()
{
    send_command(0xE4, 0x0001);     // wake up
    vTaskDelay(pdMS_TO_TICKS(100));
    send_command(0xE1, 0x0001);     // active (streaming) mode
}

#if DUTY_CYCLE
static void sensor_sleep()
{
    send_command(0xE4, 0x0000);
}
#endif

static bool parse_frame(const uint8_t *f, pm_sensor_data_t *out)
{
    uint16_t len = (f[2] << 8) | f[3];
    if (len != FRAME_LEN - 4) {
        return false;
    }
    uint16_t sum = 0;
    for (int i = 0; i < FRAME_LEN - 2; i++) {
        sum += f[i];
    }
    if (sum != ((f[FRAME_LEN - 2] << 8) | f[FRAME_LEN - 1])) {
        return false;
    }
    // Use the "atmospheric environment" values (data 4..6), not the CF=1 ones.
    out->pm1_0 = (f[10] << 8) | f[11];
    out->pm2_5 = (f[12] << 8) | f[13];
    out->pm10 = (f[14] << 8) | f[15];
    return true;
}

#elif CONFIG_PM_SENSOR_MODEL_SDS011

// 10-byte frame: AA C0 PM25(2, LE) PM10(2, LE) ID(2) CHECKSUM AB
#define FRAME_LEN               10
#define WARMUP_MS               0
#define DATA_TIMEOUT_MS         ((CONFIG_PM_SENSOR_SDS011_WORK_PERIOD_MIN * 60 + 60) * 1000)
#define DUTY_CYCLE              0

static const uint8_t k_frame_header[] = { 0xAA, 0xC0 };

static bool is_measurement_frame(const uint8_t *f)
{
    return f[FRAME_LEN - 1] == 0xAB;
}

static void send_command(uint8_t cmd, uint8_t set, uint8_t value)
{
    // AA B4 DATA1..DATA13 FF FF CHECKSUM AB; FF FF addresses all devices.
    uint8_t frame[19] = { 0xAA, 0xB4, cmd, set, value };
    frame[15] = 0xFF;
    frame[16] = 0xFF;
    uint8_t sum = 0;
    for (int i = 2; i <= 16; i++) {
        sum += frame[i];
    }
    frame[17] = sum;
    frame[18] = 0xAB;
    uart_write_bytes(PM_SENSOR_UART_NUM, frame, sizeof(frame));
}

static void sensor_setup()
{
    send_command(0x06, 0x01, 0x01);     // work (wake up)
    vTaskDelay(pdMS_TO_TICKS(200));
    send_command(0x02, 0x01, 0x00);     // active reporting mode
    vTaskDelay(pdMS_TO_TICKS(200));
    send_command(0x08, 0x01, CONFIG_PM_SENSOR_SDS011_WORK_PERIOD_MIN);
}

static bool parse_frame(const uint8_t *f, pm_sensor_data_t *out)
{
    if (f[FRAME_LEN - 1] != 0xAB) {
        return false;
    }
    uint8_t sum = 0;
    for (int i = 2; i <= 7; i++) {
        sum += f[i];
    }
    if (sum != f[8]) {
        return false;
    }
    out->pm1_0 = NAN;
    out->pm2_5 = ((f[3] << 8) | f[2]) / 10.0f;
    out->pm10 = ((f[5] << 8) | f[4]) / 10.0f;
    return true;
}

#else
#error "No PM sensor model selected"
#endif

static pm_sensor_config_t s_config;
// Written by the sensor task only; read by pm_sensor_get_stats() from any task.
static pm_sensor_stats_t s_stats;

void pm_sensor_get_stats(pm_sensor_stats_t *stats)
{
    *stats = s_stats;
}

// Running average of the samples received since the last report.
typedef struct {
    float pm1_0;
    float pm2_5;
    float pm10;
    uint32_t count;
} pm_accumulator_t;

static void report_average(pm_accumulator_t *acc)
{
    // Whole µg/m³: the average would otherwise have long fractions (e.g.
    // 2.754654) that controllers such as Home Assistant show as is.
    pm_sensor_data_t data = {
        .valid = true,
        .pm1_0 = roundf(acc->pm1_0 / acc->count),
        .pm2_5 = roundf(acc->pm2_5 / acc->count),
        .pm10 = roundf(acc->pm10 / acc->count),
    };
    *acc = {};
    s_stats.reports++;
    ESP_LOGI(TAG, "PM1.0=%.0f PM2.5=%.0f PM10=%.0f ug/m3", data.pm1_0, data.pm2_5, data.pm10);
    s_config.cb(&data, s_config.user_data);
}

static void report_invalid()
{
    s_stats.invalid_reports++;
    ESP_LOGW(TAG, "No data from sensor, check wiring (RX=GPIO%d, TX=GPIO%d)", s_config.rx_pin, s_config.tx_pin);
    pm_sensor_data_t data = { .valid = false, .pm1_0 = NAN, .pm2_5 = NAN, .pm10 = NAN };
    s_config.cb(&data, s_config.user_data);
}

static void pm_sensor_task(void *arg)
{
    uint8_t rx[64];
    uint8_t frame[FRAME_LEN];
    size_t frame_pos = 0;
    pm_accumulator_t acc = {};

    // Start of the current measurement (power-up, or wake-up in duty cycle mode).
    int64_t cycle_start_ms = esp_timer_get_time() / 1000;
    int64_t last_frame_ms = -1;     // last valid frame since the sensor woke up, -1 = none yet
#if DUTY_CYCLE
    bool sleeping = false;
    int64_t sleep_sent_ms = 0;      // last sleep command
    uint32_t sleep_retries = 0;     // of the current sleep period
#else
    int64_t last_report_ms = cycle_start_ms;
    int64_t last_sample_ms = cycle_start_ms;
    int64_t last_setup_ms = cycle_start_ms;
    bool reported_invalid = false;
#endif

    sensor_setup();
    s_stats.wakeups++;

    // Restart the device if this task ever stops (stale values are worse).
    esp_task_wdt_add(NULL);

    while (true) {
        esp_task_wdt_reset();
        int len = uart_read_bytes(PM_SENSOR_UART_NUM, rx, sizeof(rx), pdMS_TO_TICKS(200));
        int64_t now_ms = esp_timer_get_time() / 1000;

        if (len > 0) {
            s_stats.rx_bytes += len;
        }
        for (int i = 0; i < len; i++) {
            uint8_t b = rx[i];
            if (frame_pos < sizeof(k_frame_header) && b != k_frame_header[frame_pos]) {
                // Resynchronize: this byte may be the start of a new header.
                s_stats.dropped_bytes += frame_pos + (b == k_frame_header[0] ? 0 : 1);
                frame_pos = (b == k_frame_header[0]) ? 1 : 0;
                if (frame_pos) {
                    frame[0] = b;
                }
                continue;
            }
            frame[frame_pos++] = b;
            if (frame_pos < FRAME_LEN) {
                continue;
            }
            frame_pos = 0;

            pm_sensor_data_t sample;
            if (!parse_frame(frame, &sample)) {
                if (is_measurement_frame(frame)) {
                    s_stats.checksum_errors++;
                } else {
                    s_stats.other_frames++;
                }
                ESP_LOGD(TAG, "Invalid frame");
                continue;
            }
            s_stats.frames++;
#if DUTY_CYCLE
            if (sleeping) {
                // Still awake: the fan would run until the next period.
                if (now_ms - sleep_sent_ms >= SLEEP_RETRY_MS) {
                    sleep_sent_ms = now_ms;
                    s_stats.sleep_retries++;
                    // Logged for the first one, then once a minute.
                    if (sleep_retries++ % (60000 / SLEEP_RETRY_MS) == 0) {
                        ESP_LOGW(TAG, "Sensor still sends data after the sleep command, sending it again "
                                 "(if this repeats, check TX=GPIO%d to the sensor RX)", s_config.tx_pin);
                    }
                    sensor_sleep();
                }
                continue;
            }
#else
            if (now_ms - last_sample_ms > DATA_TIMEOUT_MS) {
                // First frame after a silence: the sensor was (re)started, so
                // skip its warm-up readings again.
                cycle_start_ms = now_ms;
            }
            last_sample_ms = now_ms;
#endif
            if (last_frame_ms >= 0 && now_ms - last_frame_ms > s_stats.max_frame_gap_ms) {
                s_stats.max_frame_gap_ms = (uint32_t)(now_ms - last_frame_ms);
            }
            last_frame_ms = now_ms;
            if (now_ms - cycle_start_ms < WARMUP_MS) {
                continue;
            }
            ESP_LOGD(TAG, "PM1.0=%.1f PM2.5=%.1f PM10=%.1f", sample.pm1_0, sample.pm2_5, sample.pm10);
            acc.pm1_0 += sample.pm1_0;
            acc.pm2_5 += sample.pm2_5;
            acc.pm10 += sample.pm10;
            acc.count++;
        }

#if DUTY_CYCLE
        if (!sleeping && now_ms - cycle_start_ms >= WARMUP_MS + DUTY_SAMPLE_MS) {
            if (acc.count > 0) {
                report_average(&acc);
            } else {
                report_invalid();
            }
            sensor_sleep();
            sleeping = true;
            sleep_sent_ms = now_ms;
            sleep_retries = 0;
        } else if (sleeping && now_ms - cycle_start_ms >= DUTY_PERIOD_MS) {
            cycle_start_ms = now_ms;
            sleeping = false;
            last_frame_ms = -1;
            sensor_setup();
            s_stats.wakeups++;
        }
#else
        if (now_ms - last_report_ms < s_config.report_interval_ms) {
            continue;
        }
        last_report_ms = now_ms;

        if (acc.count > 0) {
            reported_invalid = false;
            report_average(&acc);
        } else if (now_ms - last_sample_ms > DATA_TIMEOUT_MS) {
            if (!reported_invalid) {
                reported_invalid = true;
                report_invalid();
            }
            // The sensor may have been unplugged, reset or left asleep: keep
            // sending the setup commands while it is silent.
            if (now_ms - last_setup_ms >= DATA_TIMEOUT_MS) {
                last_setup_ms = now_ms;
                last_frame_ms = -1;
                sensor_setup();
                s_stats.wakeups++;
            }
        }
#endif
    }
}

esp_err_t pm_sensor_init(const pm_sensor_config_t *config)
{
    if (config == NULL || config->cb == NULL || config->report_interval_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_config = *config;

    const uart_config_t uart_config = {
        .baud_rate = PM_SENSOR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t err = uart_driver_install(PM_SENSOR_UART_NUM, PM_SENSOR_RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    err = uart_param_config(PM_SENSOR_UART_NUM, &uart_config);
    if (err == ESP_OK) {
        err = uart_set_pin(PM_SENSOR_UART_NUM, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE,
                           UART_PIN_NO_CHANGE);
    }
    if (err == ESP_OK) {
        // uart_set_pin() leaves the RX pad without a pull: with the sensor
        // unplugged it would float and receive noise.
        err = gpio_set_pull_mode((gpio_num_t)config->rx_pin, GPIO_PULLUP_ONLY);
    }
    if (err == ESP_OK &&
        xTaskCreate(pm_sensor_task, "pm_sensor", PM_SENSOR_TASK_STACK, NULL, PM_SENSOR_TASK_PRIO, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    if (err != ESP_OK) {
        uart_driver_delete(PM_SENSOR_UART_NUM);
        return err;
    }
    ESP_LOGI(TAG, "PM sensor on UART%d, RX=GPIO%d TX=GPIO%d", PM_SENSOR_UART_NUM, config->rx_pin, config->tx_pin);
    return ESP_OK;
}
