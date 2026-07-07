#include "app_at_protocol.h"

#include <stddef.h>
#include <string.h>

#define APP_AT_PROTOCOL_MAX_LINE_LENGTH 48U

static size_t AppAtProtocol_Normalize(const char *input, char *output, size_t output_size)
{
    size_t input_index = 0U;
    size_t output_index = 0U;

    if ((input == NULL) || (output == NULL) || (output_size == 0U))
    {
        return 0U;
    }

    while ((input[input_index] != '\0') && (output_index + 1U < output_size))
    {
        char current = input[input_index++];

        if ((current == '\r') || (current == '\n') || (current == ' ') || (current == '\t'))
        {
            continue;
        }

        output[output_index++] = current;
    }

    output[output_index] = '\0';
    return output_index;
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

static bool AppAtProtocol_MatchQuery(const char *line, AppAtCommand *out_command)
{
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
    char normalized[APP_AT_PROTOCOL_MAX_LINE_LENGTH];

    return AppAtProtocol_Normalize(line, normalized, sizeof(normalized)) >= 3U
        && strncmp(normalized, "AT+", 3U) == 0;
}

bool AppAtProtocol_Parse(const char *line, AppAtCommand *out_command)
{
    char normalized[APP_AT_PROTOCOL_MAX_LINE_LENGTH];
    const char *value = NULL;
    bool enabled = false;

    if ((out_command == NULL) || !AppAtProtocol_IsAtCommand(line))
    {
        return false;
    }

    (void)memset(out_command, 0, sizeof(*out_command));
    (void)AppAtProtocol_Normalize(line, normalized, sizeof(normalized));

    if (AppAtProtocol_MatchQuery(normalized, out_command))
    {
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+UART2=", normalized, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_BRIDGE;
        out_command->data.bridge.target = APP_BRIDGE_TARGET_UART2;
        out_command->data.bridge.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+UART3=", normalized, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_BRIDGE;
        out_command->data.bridge.target = APP_BRIDGE_TARGET_UART3;
        out_command->data.bridge.enabled = enabled;
        return true;
    }

    if ((AppAtProtocol_MatchAssignment("AT+UART2&3=", normalized, &value)
         || AppAtProtocol_MatchAssignment("AT+UART23=", normalized, &value))
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_BRIDGE;
        out_command->data.bridge.target = APP_BRIDGE_TARGET_UART23;
        out_command->data.bridge.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+LED=", normalized, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_LED_MASTER;
        out_command->data.led.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+MOTOR=", normalized, &value)
        && AppAtProtocol_MatchMotorMode(value, &out_command->data.motor.mode))
    {
        out_command->type = APP_AT_COMMAND_SET_MOTOR_MODE;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+NMOS1=", normalized, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_NMOS1;
        out_command->data.output.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+NMOS2=", normalized, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_NMOS2;
        out_command->data.output.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+UVLO=", normalized, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_EN_UVLO;
        out_command->data.output.enabled = enabled;
        return true;
    }

    return false;
}