#include "app_at_protocol.h"

#include <stddef.h>
#include <string.h>

#define APP_AT_PROTOCOL_MAX_LINE_LENGTH 48U

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

    if (AppAtProtocol_MatchAssignment("AT+UART2=", command_body, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_BRIDGE;
        out_command->data.bridge.target = APP_BRIDGE_TARGET_UART2;
        out_command->data.bridge.enabled = enabled;
        return true;
    }

    if (AppAtProtocol_MatchAssignment("AT+UART3=", command_body, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_BRIDGE;
        out_command->data.bridge.target = APP_BRIDGE_TARGET_UART3;
        out_command->data.bridge.enabled = enabled;
        return true;
    }

    if ((AppAtProtocol_MatchAssignment("AT+UART2&3=", command_body, &value)
         || AppAtProtocol_MatchAssignment("AT+UART23=", command_body, &value))
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_BRIDGE;
        out_command->data.bridge.target = APP_BRIDGE_TARGET_UART23;
        out_command->data.bridge.enabled = enabled;
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

    if (AppAtProtocol_MatchAssignment("AT+UVLO=", command_body, &value)
        && AppAtProtocol_ParseOnOff(value, &enabled))
    {
        out_command->type = APP_AT_COMMAND_SET_EN_UVLO;
        out_command->data.output.enabled = enabled;
        return true;
    }

    return false;
}