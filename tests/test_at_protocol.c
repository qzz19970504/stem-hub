#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "app_at_protocol.h"

static void expect_bridge_command(const char *line,
                                  AppBridgeTarget expected_target,
                                  bool expected_enabled)
{
    AppAtCommand command = {0};

    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == APP_AT_COMMAND_SET_BRIDGE);
    assert(command.data.bridge.target == expected_target);
    assert(command.data.bridge.enabled == expected_enabled);
}

static void expect_query_command(const char *line, AppAtCommandType expected_type)
{
    AppAtCommand command = {0};

    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == expected_type);
}

int main(void)
{
    AppAtCommand command = {0};

    expect_bridge_command("AT+UART2=ON", APP_BRIDGE_TARGET_UART2, true);
    expect_bridge_command("AT+UART3=OFF", APP_BRIDGE_TARGET_UART3, false);
    expect_bridge_command("AT+UART2&3=ON", APP_BRIDGE_TARGET_UART23, true);

    assert(AppAtProtocol_Parse("AT+LED=OFF", &command));
    assert(command.type == APP_AT_COMMAND_SET_LED_MASTER);
    assert(command.data.led.enabled == false);

    assert(AppAtProtocol_Parse("AT+MOTOR=REV", &command));
    assert(command.type == APP_AT_COMMAND_SET_MOTOR_MODE);
    assert(command.data.motor.mode == APP_MOTOR_MODE_REVERSE);

    assert(AppAtProtocol_Parse("AT+NMOS1=ON", &command));
    assert(command.type == APP_AT_COMMAND_SET_NMOS1);
    assert(command.data.output.enabled == true);

    assert(AppAtProtocol_Parse("AT+UVLO=OFF", &command));
    assert(command.type == APP_AT_COMMAND_SET_EN_UVLO);
    assert(command.data.output.enabled == false);

    expect_query_command("AT+SENSE?", APP_AT_COMMAND_QUERY_SENSE);
    expect_query_command("AT+FAULT?", APP_AT_COMMAND_QUERY_FAULT);
    expect_query_command("AT+MOTOR?", APP_AT_COMMAND_QUERY_MOTOR);

    assert(AppAtProtocol_IsAtCommand("AT+LED=ON"));
    assert(!AppAtProtocol_IsAtCommand("payload-data"));

    assert(!AppAtProtocol_Parse("AT+UNKNOWN=ON", &command));
    assert(!AppAtProtocol_Parse("payload-data", &command));

    return 0;
}