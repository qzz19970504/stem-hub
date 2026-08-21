#include "app_at_protocol.h"

#include <stddef.h>
#include <string.h>

static bool AppAtProtocol_ValidateFrame(const char *input, size_t *body_length)
{
    size_t input_length = 0U;
    size_t index = 0U;

    if ((input == NULL) || (body_length == NULL))
    {
        return false;
    }

    input_length = strlen(input);
    if ((input_length < 5U) || (input_length >= APP_AT_PROTOCOL_MAX_LINE_LENGTH))
    {
        return false;
    }

    if ((input[input_length - 2U] != '\r') || (input[input_length - 1U] != '\n'))
    {
        return false;
    }

    for (index = 0U; index < (input_length - 2U); ++index)
    {
        char current = input[index];

        if ((current == ' ') || (current == '\t') || (current == '\r') || (current == '\n'))
        {
            return false;
        }

        if ((current >= 'a') && (current <= 'z'))
        {
            return false;
        }
    }

    *body_length = input_length - 2U;
    return true;
}

static bool AppAtProtocol_DecodeHexNibble(char value, uint8_t *nibble)
{
    if ((value >= '0') && (value <= '9'))
    {
        *nibble = (uint8_t)(value - '0');
        return true;
    }

    if ((value >= 'A') && (value <= 'F'))
    {
        *nibble = (uint8_t)(value - 'A' + 10);
        return true;
    }

    return false;
}

static bool AppAtProtocol_ParseUartPayload(const char *value,
                                           AppAtUartPayloadCommand *payload)
{
    size_t hex_length;
    size_t index;

    if ((value == NULL) || (payload == NULL))
    {
        return false;
    }

    hex_length = strlen(value);
    if ((hex_length == 0U)
        || ((hex_length % 2U) != 0U)
        || (hex_length > (APP_UART_TUNNEL_CHUNK_SIZE * 2U)))
    {
        return false;
    }

    payload->length = hex_length / 2U;
    for (index = 0U; index < payload->length; ++index)
    {
        uint8_t high;
        uint8_t low;

        if (!AppAtProtocol_DecodeHexNibble(value[index * 2U], &high)
            || !AppAtProtocol_DecodeHexNibble(value[(index * 2U) + 1U], &low))
        {
            payload->length = 0U;
            return false;
        }

        payload->bytes[index] = (uint8_t)((high << 4U) | low);
    }

    return true;
}

static bool AppAtProtocol_ParseOnOff(const char *value, bool *enabled)
{
    if ((value == NULL) || (enabled == NULL))
    {
        return false;
    }

    if (strcmp(value, "ON") == 0)
    {
        *enabled = true;
        return true;
    }

    if (strcmp(value, "OFF") == 0)
    {
        *enabled = false;
        return true;
    }

    return false;
}

static bool AppAtProtocol_ParseChargeTime(const char *value, uint32_t *seconds)
{
    const char *current;
    uint32_t parsed_seconds = 0U;

    if ((value == NULL) || (seconds == NULL) || (*value == '\0'))
    {
        return false;
    }

    for (current = value; *current != '\0'; ++current)
    {
        if ((*current < '0') || (*current > '9'))
        {
            return false;
        }

        parsed_seconds = (parsed_seconds * 10U) + (uint32_t)(*current - '0');
        if (parsed_seconds > APP_CHARGE_MAX_ON_TIME_SECONDS)
        {
            return false;
        }
    }

    if (parsed_seconds < APP_CHARGE_MIN_ON_TIME_SECONDS)
    {
        return false;
    }

    *seconds = parsed_seconds;
    return true;
}

static bool AppAtProtocol_ParseStallCurrent(const char *value,
                                            uint32_t *current_ma)
{
    const char *current;
    uint32_t parsed_current_ma = 0U;

    if ((value == NULL) || (current_ma == NULL) || (*value == '\0'))
    {
        return false;
    }

    for (current = value; *current != '\0'; ++current)
    {
        uint32_t digit;

        if ((*current < '0') || (*current > '9'))
        {
            return false;
        }

        digit = (uint32_t)(*current - '0');
        if (parsed_current_ma
            > ((APP_MOTOR_STALL_MAX_CURRENT_MA - digit) / 10U))
        {
            return false;
        }

        parsed_current_ma = (parsed_current_ma * 10U) + digit;
    }

    if (parsed_current_ma < APP_MOTOR_STALL_MIN_CURRENT_MA)
    {
        return false;
    }

    *current_ma = parsed_current_ma;
    return true;
}

static bool AppAtProtocol_MatchQuery(const char *line, AppAtCommand *out_command)
{
    if (strcmp(line, "AT+STALL_CURRENT=?") == 0)
    {
        out_command->type = APP_AT_COMMAND_QUERY_STALL_CURRENT;
        return true;
    }

    if (strcmp(line, "AT+CHARGE_TIME=?") == 0)
    {
        out_command->type = APP_AT_COMMAND_QUERY_CHARGE_TIME;
        return true;
    }

    if (strcmp(line, "AT+SENSE?") == 0)
    {
        out_command->type = APP_AT_COMMAND_QUERY_SENSE;
        return true;
    }

    if (strcmp(line, "AT+FAULT?") == 0)
    {
        out_command->type = APP_AT_COMMAND_QUERY_FAULT;
        return true;
    }

    if (strcmp(line, "AT+MOTOR?") == 0)
    {
        out_command->type = APP_AT_COMMAND_QUERY_MOTOR;
        return true;
    }

    if (strcmp(line, "AT+DIAG?") == 0)
    {
        out_command->type = APP_AT_COMMAND_QUERY_DIAG;
        return true;
    }

    if (strcmp(line, "AT+VERSION?") == 0)
    {
        out_command->type = APP_AT_COMMAND_QUERY_VERSION;
        return true;
    }

    return false;
}

static bool AppAtProtocol_MatchMotorMode(const char *value, AppMotorMode *mode)
{
    if ((value == NULL) || (mode == NULL))
    {
        return false;
    }

    if (strcmp(value, "SLEEP") == 0)
    {
        *mode = APP_MOTOR_MODE_SLEEP;
        return true;
    }

    if (strcmp(value, "WAKE") == 0)
    {
        *mode = APP_MOTOR_MODE_WAKE;
        return true;
    }

    if (strcmp(value, "FWD") == 0)
    {
        *mode = APP_MOTOR_MODE_FORWARD;
        return true;
    }

    if (strcmp(value, "REV") == 0)
    {
        *mode = APP_MOTOR_MODE_REVERSE;
        return true;
    }

    if (strcmp(value, "BRAKE") == 0)
    {
        *mode = APP_MOTOR_MODE_BRAKE;
        return true;
    }

    if (strcmp(value, "STOP") == 0)
    {
        *mode = APP_MOTOR_MODE_STOP;
        return true;
    }

    return false;
}

static bool AppAtProtocol_MatchAssignment(const char *prefix,
                                          const char *line,
                                          const char **value)
{
    size_t prefix_length;

    if ((prefix == NULL) || (line == NULL) || (value == NULL))
    {
        return false;
    }

    prefix_length = strlen(prefix);
    if (strncmp(line, prefix, prefix_length) != 0)
    {
        return false;
    }

    *value = line + prefix_length;
    return true;
}

bool AppAtProtocol_IsAtCommand(const char *line)
{
    size_t body_length = 0U;

    return AppAtProtocol_ValidateFrame(line, &body_length)
        && (body_length >= 3U)
        && (strncmp(line, "AT+", 3U) == 0);
}

bool AppAtProtocol_Parse(const char *line, AppAtCommand *out_command)
{
    char command_body[APP_AT_PROTOCOL_MAX_LINE_LENGTH];
    const char *value = NULL;
    bool enabled = false;
    size_t body_length = 0U;

    if ((out_command == NULL) || !AppAtProtocol_ValidateFrame(line, &body_length))
    {
        return false;
    }

    if ((body_length < 3U) || (strncmp(line, "AT+", 3U) != 0))
    {
        return false;
    }

    (void)memset(out_command, 0, sizeof(*out_command));
    (void)memcpy(command_body, line, body_length);
    command_body[body_length] = '\0';

    if (AppAtProtocol_MatchQuery(command_body, out_command))
    {
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+TRANS=", command_body, &value))
    {
        out_command->type = APP_AT_COMMAND_START_TRANSPARENT;
        if (strcmp(value, "1") == 0)
        {
            out_command->data.transparent.target = APP_BRIDGE_TARGET_UART2;
        }
        else if (strcmp(value, "2") == 0)
        {
            out_command->data.transparent.target = APP_BRIDGE_TARGET_UART3;
        }
        else if (strcmp(value, "1&2") == 0)
        {
            out_command->data.transparent.target = APP_BRIDGE_TARGET_UART23;
        }
        else
        {
            return false;
        }
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+LED=", command_body, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_LED_MASTER;
        out_command->data.led.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+MOTOR=", command_body, &value)
        && AppAtProtocol_MatchMotorMode(value, &out_command->data.motor.mode))
    {
        out_command->type = APP_AT_COMMAND_SET_MOTOR_MODE;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+NMOS1=", command_body, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_NMOS1;
        out_command->data.output.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+NMOS2=", command_body, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_NMOS2;
        out_command->data.output.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+CHARGE=", command_body, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_POWER_MODE;
        out_command->data.power.mode = enabled ? APP_POWER_MODE_CHARGE : APP_POWER_MODE_OFF;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+CHARGE_TIME=", command_body, &value)
        && AppAtProtocol_ParseChargeTime(value, &out_command->data.charge_time.seconds))
    {
        out_command->type = APP_AT_COMMAND_SET_CHARGE_TIME;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+STALL_CURRENT=", command_body, &value)
        && AppAtProtocol_ParseStallCurrent(
            value,
            &out_command->data.stall_current.current_ma))
    {
        out_command->type = APP_AT_COMMAND_SET_STALL_CURRENT;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+DRIVE=", command_body, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_POWER_MODE;
        out_command->data.power.mode = enabled ? APP_POWER_MODE_DRIVE : APP_POWER_MODE_OFF;
        return true;
    }

    if (strcmp(command_body, "AT+POWER=OFF") == 0)
    {
        out_command->type = APP_AT_COMMAND_SET_POWER_MODE;
        out_command->data.power.mode = APP_POWER_MODE_OFF;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+UARTTX=", command_body, &value)
        && AppAtProtocol_ParseUartPayload(value, &out_command->data.uart_payload))
    {
        out_command->type = APP_AT_COMMAND_SEND_UART;
        return true;
    }

    return false;
}
