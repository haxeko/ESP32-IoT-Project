#include <ir_rx.h>

#include <stdlib.h>
#include <string.h>

#include <driver/gpio.h>
#include <driver/rmt_rx.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>

#include <fan_ctrl.h>
#include <ir_decode.h>
#include <ir_table.h>
#include <status_led.h>

static const char *TAG = "ir_rx";

#if CONFIG_IR_RX_GPIO < 0

void ir_rx_init()
{
    ESP_LOGI(TAG, "IR receiver disabled (CONFIG_IR_RX_GPIO = -1)");
}
void ir_learn_start() {}
void ir_learn_skip() {}
void ir_learn_finish() {}
bool ir_learn_active()
{
    return false;
}

#else

namespace {

constexpr uint32_t k_resolution_hz = 1000 * 1000; /* 1 tick = 1 us */
/* Longest frame kept; a longer one is truncated, the same way every time, so it still gets a stable code */
constexpr size_t k_max_symbols = 128;
/* Hardware memory: 2 blocks of 48 symbols on ESP32-C3, received in ping-pong halves into s_symbols */
constexpr size_t k_mem_block_symbols = 64;

/* Task notification bits */
constexpr uint32_t k_bit_frame = 1 << 0;
constexpr uint32_t k_bit_learn_start = 1 << 1;
constexpr uint32_t k_bit_learn_skip = 1 << 2;
constexpr uint32_t k_bit_learn_finish = 1 << 3;

constexpr TickType_t k_rearm_retry_ticks = pdMS_TO_TICKS(1000);
constexpr int64_t k_learn_timeout_us = CONFIG_IR_LEARN_TIMEOUT_S * 1000000LL;
/* While learning, frames this close to the button just learned still belong to it: some remotes send a different
 * second frame for each press (e.g. Sharp: the frame, then its inverted copy), which must not be learned as the
 * next button */
constexpr int64_t k_learn_quiet_us = 600 * 1000;

/* NVS record of the learned codes. Same namespace as the fan state. Version 1 had 7 functions (no NATURAL): new
 * functions are appended, so an older record is a prefix of the table. */
struct StoredCodes {
    uint32_t version;
    ir_table_t table;
};
constexpr uint32_t k_codes_version = 2;
constexpr size_t k_codes_v1_functions = 7;
/* Longest record accepted from a newer firmware (functions appended) */
constexpr size_t k_codes_max_size = 1024;
constexpr const char *k_nvs_namespace = "fan";
constexpr const char *k_nvs_key_codes = "ir_codes";

const rmt_receive_config_t k_rx_config = {
    /* Shorter pulses are glitches (the shortest IR pulse is a few hundred us) */
    .signal_range_min_ns = 1250,
    /* The frame ends when the line stays idle this long: longer than any space inside a frame (NEC leader
     * 4.5 ms), shorter than the gap between two frames (NEC repeats every 40+ ms) */
    .signal_range_max_ns = 15 * 1000 * 1000,
    .flags = {
        .en_partial_rx = false,
    },
};

TaskHandle_t s_task;
rmt_channel_handle_t s_channel;
rmt_symbol_word_t s_symbols[k_max_symbols]; /* written by the RMT driver between rmt_receive() and the callback */
volatile size_t s_received;                 /* symbols of the last frame (RMT ISR -> IR task) */
ir_pulse_t s_pulses[k_max_symbols];         /* IR task only */
ir_table_t s_table;                         /* IR task only */
ir_press_filter_t s_filter;                 /* IR task only */
volatile bool s_learning;                   /* written by the IR task, read by any task */
int s_learn_step;                           /* IR task only */
int64_t s_learn_deadline_us;                /* IR task only */
int64_t s_learn_quiet_until_us;             /* IR task only */

} // namespace

/* RMT ISR. A task notification bit never gets lost, so the IR task always re-arms the receiver. */
static bool rx_done_cb(rmt_channel_handle_t, const rmt_rx_done_event_data_t *edata, void *)
{
    BaseType_t woken = pdFALSE;
    s_received = edata->num_symbols;
    xTaskNotifyFromISR(s_task, k_bit_frame, eSetBits, &woken);
    return woken == pdTRUE;
}

static bool arm_receiver()
{
    const esp_err_t err = rmt_receive(s_channel, s_symbols, sizeof(s_symbols), &k_rx_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_receive failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

/* RMT symbols -> (mark, space) pulses, see ir_pulses_from_rmt() */
static size_t symbols_to_pulses(size_t symbols)
{
    static uint32_t raw[k_max_symbols]; /* IR task only */
    if (symbols > k_max_symbols) {
        symbols = k_max_symbols;
    }
    for (size_t i = 0; i < symbols; i++) {
        raw[i] = s_symbols[i].val;
    }
    return ir_pulses_from_rmt(raw, symbols, s_pulses, k_max_symbols);
}

static void log_code(const ir_code_t &code)
{
    if (code.proto == IR_PROTO_NEC) {
        ESP_LOGI(TAG, "IR NEC address 0x%02X command 0x%02X (0x%08lX)", (unsigned)(code.value & 0xFF),
                 (unsigned)((code.value >> 16) & 0xFF), (unsigned long)code.value);
    } else {
        ESP_LOGI(TAG, "IR code 0x%08lX (%u pulses)", (unsigned long)code.value, code.pulses);
    }
}

static void load_codes()
{
    nvs_handle_t handle;
    if (nvs_open(k_nvs_namespace, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    /* Read the whole record, whatever its length: a newer firmware may have stored more functions (e.g. before an OTA
     * rollback to this one). Functions are only appended, so the ones this firmware knows come first. */
    size_t len = 0;
    esp_err_t err = nvs_get_blob(handle, k_nvs_key_codes, NULL, &len);
    uint8_t *buf = NULL;
    if (err == ESP_OK && len >= sizeof(uint32_t) && len <= k_codes_max_size) {
        buf = (uint8_t *)malloc(len);
        err = buf ? nvs_get_blob(handle, k_nvs_key_codes, buf, &len) : ESP_ERR_NO_MEM;
    } else if (err == ESP_OK) {
        err = ESP_ERR_NVS_INVALID_LENGTH;
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Learned remote buttons not loaded: %s", esp_err_to_name(err));
        }
        free(buf);
        return;
    }
    StoredCodes stored;
    memset(&stored, 0, sizeof(stored));
    memcpy(&stored, buf, len < sizeof(stored) ? len : sizeof(stored));
    free(buf);
    const bool v1 = stored.version == 1 && len == sizeof(uint32_t) + k_codes_v1_functions * sizeof(ir_code_t);
    if (v1 || (stored.version >= k_codes_version && len >= sizeof(stored))) {
        s_table = stored.table; /* v1: the functions it did not have stay zero (not learned) */
    } else {
        ESP_LOGW(TAG, "Unknown remote buttons record (version %lu, %u bytes): ignored", (unsigned long)stored.version,
                 (unsigned)len);
    }
}

static void save_codes()
{
    StoredCodes stored;
    memset(&stored, 0, sizeof(stored));
    stored.version = k_codes_version;
    stored.table = s_table;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(k_nvs_namespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, k_nvs_key_codes, &stored, sizeof(stored));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store the remote codes: %s", esp_err_to_name(err));
    }
}

static void learn_prompt()
{
    const ir_code_t &old = s_table.codes[s_learn_step];
    ESP_LOGI(TAG, "IR learning %d/%d: press the %s button on the remote (%s; click: skip, double click: finish)",
             s_learn_step + 1, (int)IR_FUNC_COUNT, ir_func_name(s_learn_step),
             old.proto != IR_PROTO_NONE ? "already learned" : "not learned yet");
    status_led_learning((uint8_t)(s_learn_step + 1));
    s_learn_deadline_us = esp_timer_get_time() + k_learn_timeout_us;
}

static void learn_end(const char *why)
{
    s_learning = false;
    status_led_learning(0);
    save_codes();
    ESP_LOGI(TAG, "IR learning finished (%s): %d of %d remote buttons learned", why, ir_table_count(&s_table),
             (int)IR_FUNC_COUNT);
    for (int i = 0; i < IR_FUNC_COUNT; i++) {
        if (s_table.codes[i].proto != IR_PROTO_NONE) {
            ESP_LOGI(TAG, "  %-7s = 0x%08lX", ir_func_name(i), (unsigned long)s_table.codes[i].value);
        }
    }
}

static void learn_next()
{
    if (++s_learn_step >= IR_FUNC_COUNT) {
        learn_end("all buttons asked");
        return;
    }
    learn_prompt();
}

static void handle_frame(size_t symbols)
{
    const size_t count = symbols_to_pulses(symbols);
    ir_code_t code;
    memset(&code, 0, sizeof(code));
    const ir_frame_t frame = ir_decode(s_pulses, count, &code);
    const int64_t now_us = esp_timer_get_time();
    const bool new_press = ir_press_filter_accept(&s_filter, frame, &code, (uint32_t)(now_us / 1000));

    if (s_learning) {
        if (frame != IR_FRAME_NONE && now_us < s_learn_quiet_until_us) {
            s_learn_quiet_until_us = now_us + k_learn_quiet_us; /* rest of the press just learned */
            return;
        }
        if (!new_press) {
            return;
        }
        log_code(code);
        ir_table_assign(&s_table, s_learn_step, &code);
        ESP_LOGI(TAG, "Learned %s", ir_func_name(s_learn_step));
        status_led_flash();
        s_learn_quiet_until_us = now_us + k_learn_quiet_us;
        learn_next();
        return;
    }

    if (!new_press) {
        return; /* noise, or the repeat of a held button */
    }
    log_code(code);

    const int func = ir_table_find(&s_table, &code);
    if (func < 0) {
        ESP_LOGI(TAG, "Not a learned button (triple click the device button to learn the remote)");
        return;
    }
    fan_cmd_t cmd;
    if (ir_table_command(&s_table, func, &cmd) && fan_ctrl_post(&cmd, FAN_SRC_IR)) {
        ESP_LOGI(TAG, "Remote button %s", ir_func_name(func));
        status_led_flash();
    }
}

static void ir_task(void *)
{
    bool armed = arm_receiver();
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        if (!armed) {
            wait = k_rearm_retry_ticks;
        } else if (s_learning) {
            const int64_t left_us = s_learn_deadline_us - esp_timer_get_time();
            wait = left_us > 0 ? pdMS_TO_TICKS(left_us / 1000) + 1 : 0;
        }
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, wait);

        if (bits & k_bit_frame) {
            handle_frame(s_received);
            armed = false;
        }
        if (!armed) {
            armed = arm_receiver();
        }

        if (bits & k_bit_learn_start) {
            if (s_learning) {
                ESP_LOGI(TAG, "IR learning restarted");
            }
            s_learning = true;
            s_learn_step = 0;
            learn_prompt();
        }
        if ((bits & k_bit_learn_skip) && s_learning) {
            ESP_LOGI(TAG, "Skipped %s", ir_func_name(s_learn_step));
            learn_next();
        }
        if ((bits & k_bit_learn_finish) && s_learning) {
            learn_end("double click");
        }
        if (s_learning && esp_timer_get_time() >= s_learn_deadline_us) {
            learn_end("no button pressed");
        }
    }
}

void ir_rx_init()
{
    load_codes();
    ESP_LOGI(TAG, "IR receiver on GPIO%d, %d remote buttons learned", CONFIG_IR_RX_GPIO, ir_table_count(&s_table));

    rmt_rx_channel_config_t channel_config = {};
    channel_config.gpio_num = (gpio_num_t)CONFIG_IR_RX_GPIO;
    channel_config.clk_src = RMT_CLK_SRC_DEFAULT;
    channel_config.resolution_hz = k_resolution_hz;
    channel_config.mem_block_symbols = k_mem_block_symbols;
    esp_err_t err = rmt_new_rx_channel(&channel_config, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT RX channel failed (%s): IR remote disabled", esp_err_to_name(err));
        s_channel = NULL;
        return;
    }
    /* Receiver modules have an output pull-up; this one keeps an unplugged input from floating */
    gpio_pullup_en((gpio_num_t)CONFIG_IR_RX_GPIO);

    /* The callback only fires once the IR task has armed the receiver, so s_task is set by then */
    const rmt_rx_event_callbacks_t callbacks = {
        .on_recv_done = rx_done_cb,
    };
    err = rmt_rx_register_event_callbacks(s_channel, &callbacks, NULL);
    if (err == ESP_OK) {
        err = rmt_enable(s_channel);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT RX setup failed (%s): IR remote disabled", esp_err_to_name(err));
        rmt_del_channel(s_channel);
        s_channel = NULL;
        return;
    }
    if (xTaskCreate(ir_task, "ir_rx", 4096, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create the IR task: IR remote disabled");
        s_task = NULL;
        rmt_disable(s_channel);
        rmt_del_channel(s_channel);
        s_channel = NULL;
    }
}

static void notify(uint32_t bit)
{
    if (s_task) {
        xTaskNotify(s_task, bit, eSetBits);
    }
}

void ir_learn_start()
{
    notify(k_bit_learn_start);
}

void ir_learn_skip()
{
    notify(k_bit_learn_skip);
}

void ir_learn_finish()
{
    notify(k_bit_learn_finish);
}

bool ir_learn_active()
{
    return s_learning;
}

#endif // CONFIG_IR_RX_GPIO < 0
