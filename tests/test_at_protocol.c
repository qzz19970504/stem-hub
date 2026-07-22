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

    expect_bridge_command("AT+UART2=ON\r\n", APP_BRIDGE_TARGET_UART2, true);
    expect_bridge_command("AT+UART3=OFF\r\n", APP_BRIDGE_TARGET_UART3, false);
    expect_bridge_command("AT+UART2&3=ON\r\n", APP_BRIDGE_TARGET_UART23, true);

    assert(AppAtProtocol_Parse("AT+LED=OFF\r\n", &command));
    assert(command.type == APP_AT_COMMAND_SET_LED_MASTER);
    assert(command.data.led.enabled == false);

    assert(AppAtProtocol_Parse("AT+MOTOR=REV\r\n", &command));
    assert(command.type == APP_AT_COMMAND_SET_MOTOR_MODE);
    assert(command.data.motor.mode == APP_MOTOR_MODE_REVERSE);

    assert(AppAtProtocol_Parse("AT+NMOS1=ON\r\n", &command));
    assert(command.type == APP_AT_COMMAND_SET_NMOS1);
    assert(command.data.output.enabled == true);

    assert(AppAtProtocol_Parse("AT+LM51770=OFF\r\n", &command));
    assert(command.type == APP_AT_COMMAND_SET_LM51770);
    assert(command.data.output.enabled == false);

    expect_query_command("AT+SENSE?\r\n", APP_AT_COMMAND_QUERY_SENSE);
    expect_query_command("AT+FAULT?\r\n", APP_AT_COMMAND_QUERY_FAULT);
    expect_query_command("AT+MOTOR?\r\n", APP_AT_COMMAND_QUERY_MOTOR);
    expect_query_command("AT+DIAG?\r\n", APP_AT_COMMAND_QUERY_DIAG);
    expect_query_command("AT+VERSION?\r\n", APP_AT_COMMAND_QUERY_VERSION);

    assert(AppAtProtocol_IsAtCommand("AT+LED=ON\r\n"));
    assert(!AppAtProtocol_IsAtCommand("payload-data"));
    assert(!AppAtProtocol_IsAtCommand("AT+LED=ON"));
    assert(!AppAtProtocol_IsAtCommand("AT+LED=ON\n"));
    assert(!AppAtProtocol_IsAtCommand("AT+LED=ON\r"));
    assert(!AppAtProtocol_IsAtCommand("AT +LED=ON\r\n"));
    assert(!AppAtProtocol_IsAtCommand("AT+LED =ON\r\n"));
    assert(!AppAtProtocol_IsAtCommand("at+LED=ON\r\n"));

    assert(!AppAtProtocol_Parse("AT+UNKNOWN=ON\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+LED=ON", &command));
    assert(!AppAtProtocol_Parse("AT +LED=ON\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+LED=ON \r\n", &command));
    assert(!AppAtProtocol_Parse("at+LED=ON\r\n", &command));
    assert(!AppAtProtocol_Parse("payload-data", &command));

    return 0;
}