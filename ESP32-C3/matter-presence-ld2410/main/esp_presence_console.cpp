/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

// Console commands of the ESP32 presence engine, on the ESP-IDF REPL (esp_console) of the configured console:
// USB-Serial-JTAG (default, as the logs) or UART. The commands only queue a request for the engine task
// (esp_presence_command()), so the console never waits for the engine; its answers come as log lines.

#include <sdkconfig.h>

#if CONFIG_PRESENCE_ESP_ENGINE

#include <esp_console.h>
#include <esp_log.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_presence.h"

#if CONFIG_PRESENCE_ENGINE_CONSOLE

static const char *TAG = "esp_presence";

static int cmd_learn(int argc, char **argv)
{
    return esp_presence_command(ESP_PRESENCE_LEARN) ? 0 : 1;
}

static int cmd_learn_cancel(int argc, char **argv)
{
    return esp_presence_command(ESP_PRESENCE_LEARN_CANCEL) ? 0 : 1;
}

static int cmd_status(int argc, char **argv)
{
    return esp_presence_command(ESP_PRESENCE_PRINT_STATUS) ? 0 : 1;
}

static int cmd_diag(int argc, char **argv)
{
    return esp_presence_command(ESP_PRESENCE_PRINT_DIAG) ? 0 : 1;
}

static int cmd_diag_stream(int argc, char **argv)
{
    if (argc != 2 || (strcasecmp(argv[1], "on") != 0 && strcasecmp(argv[1], "off") != 0)) {
        printf("Usage: DIAG_STREAM on|off\n");
        return 1;
    }
    const bool on = strcasecmp(argv[1], "on") == 0;
    return esp_presence_command(on ? ESP_PRESENCE_DIAG_STREAM_ON : ESP_PRESENCE_DIAG_STREAM_OFF) ? 0 : 1;
}

static int cmd_profile(int argc, char **argv)
{
    return esp_presence_command(ESP_PRESENCE_PRINT_PROFILE) ? 0 : 1;
}

static int cmd_profile_reset(int argc, char **argv)
{
    // Erases the learned thresholds: asks for a confirmation word so a typo cannot do it
    if (argc != 2 || strcmp(argv[1], "YES") != 0) {
        printf("Erases the learned thresholds (back to the built-in ones). Type: PROFILE_RESET YES\n");
        return 1;
    }
    return esp_presence_command(ESP_PRESENCE_PROFILE_RESET) ? 0 : 1;
}

struct Command {
    const char *name;
    const char *alias; // lower case, not listed by help
    const char *hint;
    const char *help;
    esp_console_cmd_func_t func;
};

static const Command kCommands[] = {
    { "LEARN_EMPTY", "learn_empty", nullptr,
      "Learn the empty room: leave within the delay, stay out while it records; new thresholds only if it succeeds",
      cmd_learn },
    { "LEARN_CANCEL", "learn_cancel", nullptr, "Stop the learning, the current thresholds stay", cmd_learn_cancel },
    { "STATUS", "status", nullptr, "Presence, countdown, sensor, learning, thresholds in use", cmd_status },
    { "DIAG", "diag", nullptr, "Energy and thresholds of every gate (moving and still)", cmd_diag },
    { "DIAG_STREAM", "diag_stream", "on|off", "DIAG every second (for the calibration)", cmd_diag_stream },
    { "PROFILE", "profile", nullptr, "Learned noise statistics and thresholds of every gate", cmd_profile },
    { "PROFILE_RESET", "profile_reset", "YES", "Erase the learned thresholds (built-in ones until the next learning)",
      cmd_profile_reset },
};

esp_err_t esp_presence_console_start(void)
{
    esp_console_repl_t *repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "presence>";
    repl_config.max_history_len = 8;
    repl_config.max_cmdline_length = 64;
    repl_config.task_stack_size = 3584;
    esp_err_t err;
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    err = esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl);
#elif CONFIG_ESP_CONSOLE_UART_DEFAULT || CONFIG_ESP_CONSOLE_UART_CUSTOM
    esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    err = esp_console_new_repl_uart(&hw_config, &repl_config, &repl);
#else
    ESP_LOGW(TAG, "No console for the presence engine commands (console output off or USB CDC)");
    return ESP_ERR_NOT_SUPPORTED;
#endif
    if (err != ESP_OK) {
        return err;
    }
    for (const Command &c : kCommands) {
        esp_console_cmd_t cmd = {};
        cmd.command = c.name;
        cmd.help = c.help;
        cmd.hint = c.hint;
        cmd.func = c.func;
        err = esp_console_cmd_register(&cmd);
        if (err == ESP_OK) {
            esp_console_cmd_t alias = {};
            alias.command = c.alias;
            alias.func = c.func;
            err = esp_console_cmd_register(&alias);
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    ESP_LOGI(TAG, "Console: LEARN_EMPTY, LEARN_CANCEL, STATUS, DIAG, DIAG_STREAM on|off, PROFILE, PROFILE_RESET, help");
    return esp_console_start_repl(repl);
}

#else

esp_err_t esp_presence_console_start(void)
{
    return ESP_OK;
}

#endif // CONFIG_PRESENCE_ENGINE_CONSOLE

#endif // CONFIG_PRESENCE_ESP_ENGINE
