#ifndef APP_AT_PROTOCOL_H
#define APP_AT_PROTOCOL_H

#include <stdbool.h>

typedef enum
{
    APP_BRIDGE_TARGET_UART2 = 0,
    APP_BRIDGE_TARGET_UART3,
    APP_BRIDGE_TARGET_UART23
} AppBridgeTarget;

typedef enum
{
    APP_MOTOR_MODE_SLEEP = 0,
    APP_MOTOR_MODE_WAKE,
    APP_MOTOR_MODE_FORWARD,
    APP_MOTOR_MODE_REVERSE,
    APP_MOTOR_MODE_BRAKE,
    APP_MOTOR_MODE_STOP
} AppMotorMode;

typedef enum
{
    APP_AT_COMMAND_NONE = 0,
    APP_AT_COMMAND_SET_BRIDGE,
    APP_AT_COMMAND_SET_LED_MASTER,
    APP_AT_COMMAND_SET_MOTOR_MODE,
    APP_AT_COMMAND_SET_NMOS1,
    APP_AT_COMMAND_SET_NMOS2,
    APP_AT_COMMAND_SET_EN_UVLO,
    APP_AT_COMMAND_QUERY_SENSE,
    APP_AT_COMMAND_QUERY_FAULT,
    APP_AT_COMMAND_QUERY_MOTOR
} AppAtCommandType;

typedef struct
{
    AppBridgeTarget target;
    bool enabled;
} AppAtBridgeCommand;

typedef struct
{
    bool enabled;
} AppAtBooleanCommand;

typedef struct
{
    AppMotorMode mode;
} AppAtMotorCommand;

typedef struct
{
    AppAtCommandType type;
    union
    {
        AppAtBridgeCommand bridge;
        AppAtBooleanCommand led;
        AppAtBooleanCommand output;
        AppAtMotorCommand motor;
    } data;
} AppAtCommand;

bool AppAtProtocol_IsAtCommand(const char *line);
bool AppAtProtocol_Parse(const char *line, AppAtCommand *out_command);

#endif