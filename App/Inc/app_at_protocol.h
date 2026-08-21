#ifndef APP_AT_PROTOCOL_H
#define APP_AT_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

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
    APP_POWER_MODE_OFF = 0,
    APP_POWER_MODE_CHARGE,
    APP_POWER_MODE_DRIVE
} AppPowerMode;

typedef enum
{
    APP_AT_COMMAND_NONE = 0,
    APP_AT_COMMAND_START_TRANSPARENT,
    APP_AT_COMMAND_SET_LED_MASTER,
    APP_AT_COMMAND_SET_MOTOR_MODE,
    APP_AT_COMMAND_SET_NMOS1,
    APP_AT_COMMAND_SET_NMOS2,
    APP_AT_COMMAND_SET_POWER_MODE,
    APP_AT_COMMAND_SEND_UART,
    APP_AT_COMMAND_QUERY_SENSE,
    APP_AT_COMMAND_QUERY_FAULT,
    APP_AT_COMMAND_QUERY_MOTOR,
    APP_AT_COMMAND_QUERY_DIAG,
    APP_AT_COMMAND_QUERY_VERSION,
    APP_AT_COMMAND_SET_CHARGE_TIME,
    APP_AT_COMMAND_QUERY_CHARGE_TIME,
    APP_AT_COMMAND_SET_STALL_CURRENT,
    APP_AT_COMMAND_QUERY_STALL_CURRENT
} AppAtCommandType;

typedef struct
{
    AppBridgeTarget target;
} AppAtTransparentCommand;

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
    AppPowerMode mode;
} AppAtPowerCommand;

typedef struct
{
    uint32_t seconds;
} AppAtChargeTimeCommand;

typedef struct
{
    uint32_t current_ma;
} AppAtStallCurrentCommand;

typedef struct
{
    uint8_t bytes[APP_UART_TUNNEL_CHUNK_SIZE];
    size_t length;
} AppAtUartPayloadCommand;

typedef struct
{
    AppAtCommandType type;
    union
    {
        AppAtTransparentCommand transparent;
        AppAtBooleanCommand led;
        AppAtBooleanCommand output;
        AppAtMotorCommand motor;
        AppAtPowerCommand power;
        AppAtChargeTimeCommand charge_time;
        AppAtStallCurrentCommand stall_current;
        AppAtUartPayloadCommand uart_payload;
    } data;
} AppAtCommand;

bool AppAtProtocol_IsAtCommand(const char *line);
bool AppAtProtocol_Parse(const char *line, AppAtCommand *out_command);

#endif
