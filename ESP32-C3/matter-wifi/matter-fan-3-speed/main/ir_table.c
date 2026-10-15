#include "ir_table.h"

#include <string.h>

int ir_table_find(const ir_table_t *table, const ir_code_t *code)
{
    for (int i = 0; i < IR_FUNC_COUNT; i++) {
        if (ir_code_equal(&table->codes[i], code)) {
            return i;
        }
    }
    return -1;
}

void ir_table_assign(ir_table_t *table, int func, const ir_code_t *code)
{
    if (func < 0 || func >= IR_FUNC_COUNT) {
        return;
    }
    for (int i = 0; i < IR_FUNC_COUNT; i++) {
        if (ir_code_equal(&table->codes[i], code)) {
            memset(&table->codes[i], 0, sizeof(table->codes[i]));
        }
    }
    table->codes[func] = *code;
}

int ir_table_count(const ir_table_t *table)
{
    int n = 0;
    for (int i = 0; i < IR_FUNC_COUNT; i++) {
        if (table->codes[i].proto != IR_PROTO_NONE) {
            n++;
        }
    }
    return n;
}

bool ir_table_command(const ir_table_t *table, int func, fan_cmd_t *cmd)
{
    memset(cmd, 0, sizeof(*cmd));
    switch (func) {
    case IR_FUNC_POWER:
        /* Remotes with separate ON and OFF buttons: ON must not turn a running fan off */
        cmd->type = table->codes[IR_FUNC_OFF].proto != IR_PROTO_NONE ? FAN_CMD_ON : FAN_CMD_TOGGLE;
        return true;
    case IR_FUNC_SPEED:
        cmd->type = FAN_CMD_SPEED_CYCLE;
        return true;
    case IR_FUNC_SWING:
        cmd->type = FAN_CMD_SWING_TOGGLE;
        return true;
    case IR_FUNC_OFF:
        cmd->type = FAN_CMD_OFF;
        return true;
    case IR_FUNC_SPEED_1:
    case IR_FUNC_SPEED_2:
    case IR_FUNC_SPEED_3:
        cmd->type = FAN_CMD_SET_SPEED;
        cmd->arg = (uint8_t)(func - IR_FUNC_SPEED_1 + 1);
        return true;
    case IR_FUNC_NATURAL:
        cmd->type = FAN_CMD_NATURAL_TOGGLE;
        return true;
    default:
        return false;
    }
}

_Static_assert(sizeof(ir_code_t) == 8 && sizeof(ir_table_t) == 8 * IR_FUNC_COUNT, "NVS record layout changed");

void ir_record_encode(const ir_table_t *table, uint8_t buf[IR_RECORD_SIZE])
{
    const uint32_t version = IR_RECORD_VERSION;
    memcpy(buf, &version, sizeof(version));
    memcpy(buf + sizeof(version), table->codes, sizeof(table->codes));
}

bool ir_record_decode(const uint8_t *buf, size_t len, ir_table_t *table)
{
    if (buf == NULL || len < sizeof(uint32_t)) {
        return false;
    }
    uint32_t version;
    memcpy(&version, buf, sizeof(version));
    size_t functions;
    if (version == 1 && len == sizeof(version) + IR_RECORD_V1_FUNCTIONS * sizeof(ir_code_t)) {
        functions = IR_RECORD_V1_FUNCTIONS;
    } else if (version >= IR_RECORD_VERSION && len >= IR_RECORD_SIZE) {
        functions = IR_FUNC_COUNT;
    } else {
        return false;
    }
    memset(table, 0, sizeof(*table));
    memcpy(table->codes, buf + sizeof(version), functions * sizeof(ir_code_t));
    return true;
}

const char *ir_func_name(int func)
{
    static const char *const names[IR_FUNC_COUNT] = {
        "POWER", "SPEED", "SWING", "OFF", "SPEED 1", "SPEED 2", "SPEED 3", "NATURAL",
    };
    return (func >= 0 && func < IR_FUNC_COUNT) ? names[func] : "?";
}
