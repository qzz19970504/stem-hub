#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
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

static void expect_power_command(const char *line, AppPowerMode expected_mode)
{
    AppAtCommand command = {0};

    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == APP_AT_COMMAND_SET_POWER_MODE);
    assert(command.data.power.mode == expected_mode);
}

static void expect_boolean_command(const char *line,
                                   AppAtCommandType expected_type,
                                   bool expected_enabled)
{
    AppAtCommand command = {0};

    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == expected_type);
    assert(command.data.output.enabled == expected_enabled);
}

static void expect_charge_time_command(const char *line, uint32_t expected_seconds)
{
    AppAtCommand command = {0};

    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == APP_AT_COMMAND_SET_CHARGE_TIME);
    assert(command.data.charge_time.seconds == expected_seconds);
}

static void expect_stall_current_command(const char *line,
                                         uint32_t expected_current_ma)
{
    AppAtCommand command = {0};

    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == APP_AT_COMMAND_SET_STALL_CURRENT);
    assert(command.data.stall_current.current_ma == expected_current_ma);
}

static void build_maximum_length_charge_time_line(char *line, const char *value)
{
    static const char prefix[] = "AT+CHARGE_TIME=";
    static const char terminator[] = "\r\n";
    size_t prefix_length = strlen(prefix);
    size_t value_length = strlen(value);
    size_t leading_zero_count = APP_AT_PROTOCOL_MAX_LINE_LENGTH
        - 1U
        - prefix_length
        - value_length
        - strlen(terminator);

    memcpy(line, prefix, prefix_length);
    memset(line + prefix_length, '0', leading_zero_count);
    memcpy(line + prefix_length + leading_zero_count, value, value_length);
    memcpy(line + prefix_length + leading_zero_count + value_length,
           terminator,
           sizeof(terminator));
}

static void expect_uart_payload(const char *line,
                                const uint8_t *expected_payload,
                                size_t expected_length)
{
    AppAtCommand command = {0};

    assert(AppAtProtocol_Parse(line, &command));
    assert(command.type == APP_AT_COMMAND_SEND_UART);
    assert(command.data.uart_payload.length == expected_length);
    assert(memcmp(command.data.uart_payload.bytes, expected_payload, expected_length) == 0);
}

int main(void)
{
    AppAtCommand command = {0};
    char maximum_length_charge_time_60[APP_AT_PROTOCOL_MAX_LINE_LENGTH];
    char maximum_length_charge_time_61[APP_AT_PROTOCOL_MAX_LINE_LENGTH];
    static const uint8_t binary_payload[] = {0x00U, 0xFFU, 0x10U};
    static const uint8_t maximum_payload[32] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU
    };

    expect_bridge_command("AT+UART2=ON\r\n", APP_BRIDGE_TARGET_UART2, true);
    expect_bridge_command("AT+UART3=OFF\r\n", APP_BRIDGE_TARGET_UART3, false);
    expect_bridge_command("AT+UART2&3=ON\r\n", APP_BRIDGE_TARGET_UART23, true);

    assert(AppAtProtocol_Parse("AT+LED=OFF\r\n", &command));
    assert(command.type == APP_AT_COMMAND_SET_LED_MASTER);
    assert(command.data.led.enabled == false);

    assert(AppAtProtocol_Parse("AT+MOTOR=REV\r\n", &command));
    assert(command.type == APP_AT_COMMAND_SET_MOTOR_MODE);
    assert(command.data.motor.mode == APP_MOTOR_MODE_REVERSE);

    expect_boolean_command("AT+MOTOR_BYPASS=ON\r\n",
                           APP_AT_COMMAND_SET_MOTOR_BYPASS,
                           true);
    expect_boolean_command("AT+MOTOR_BYPASS=OFF\r\n",
                           APP_AT_COMMAND_SET_MOTOR_BYPASS,
                           false);
    expect_boolean_command("AT+CHARGE_BYPASS=ON\r\n",
                           APP_AT_COMMAND_SET_CHARGE_BYPASS,
                           true);
    expect_boolean_command("AT+CHARGE_BYPASS=OFF\r\n",
                           APP_AT_COMMAND_SET_CHARGE_BYPASS,
                           false);

    assert(AppAtProtocol_Parse("AT+NMOS1=ON\r\n", &command));
    assert(command.type == APP_AT_COMMAND_SET_NMOS1);
    assert(command.data.output.enabled == true);

    expect_power_command("AT+CHARGE=ON\r\n", APP_POWER_MODE_CHARGE);
    expect_power_command("AT+CHARGE=OFF\r\n", APP_POWER_MODE_OFF);
    expect_power_command("AT+DRIVE=ON\r\n", APP_POWER_MODE_DRIVE);
    expect_power_command("AT+DRIVE=OFF\r\n", APP_POWER_MODE_OFF);
    expect_power_command("AT+POWER=OFF\r\n", APP_POWER_MODE_OFF);

    expect_charge_time_command("AT+CHARGE_TIME=1\r\n", 1U);
    expect_charge_time_command("AT+CHARGE_TIME=10\r\n", 10U);
    expect_charge_time_command("AT+CHARGE_TIME=60\r\n", 60U);
    expect_query_command("AT+CHARGE_TIME=?\r\n", APP_AT_COMMAND_QUERY_CHARGE_TIME);

    expect_stall_current_command("AT+STALL_CURRENT=1000\r\n", 1000U);
    expect_stall_current_command("AT+STALL_CURRENT=4000\r\n", 4000U);
    expect_stall_current_command("AT+STALL_CURRENT=30000\r\n", 30000U);
    expect_query_command("AT+STALL_CURRENT=?\r\n",
                         APP_AT_COMMAND_QUERY_STALL_CURRENT);
    assert(!AppAtProtocol_Parse("AT+STALL_CURRENT=999\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+STALL_CURRENT=30001\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+STALL_CURRENT=\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+STALL_CURRENT=-4000\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+STALL_CURRENT=4.0\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+STALL_CURRENT=4000MA\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+STALL_CURRENT=+4000\r\n", &command));
    assert(!AppAtProtocol_Parse(
        "AT+STALL_CURRENT=42949672960\r\n", &command));

    build_maximum_length_charge_time_line(maximum_length_charge_time_60, "60");
    expect_charge_time_command(maximum_length_charge_time_60, 60U);
    build_maximum_length_charge_time_line(maximum_length_charge_time_61, "61");
    assert(!AppAtProtocol_Parse(maximum_length_charge_time_61, &command));
    assert(!AppAtProtocol_Parse("AT+CHARGE_TIME=12345678901234567890\r\n", &command));

    assert(!AppAtProtocol_Parse("AT+CHARGE_TIME=0\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+CHARGE_TIME=61\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+CHARGE_TIME=-1\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+CHARGE_TIME=1.5\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+CHARGE_TIME=\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+CHARGE_TIME=10X\r\n", &command));

    assert(!AppAtProtocol_Parse("AT+LM51770=ON\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+LM51770=OFF\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+MP4317=ON\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+MP4317=OFF\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+POWER=ON\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+MOTOR_BYPASS=HIGH\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+MOTOR_BYPASS=\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+MOTOR_BYPASS=ON \r\n", &command));
    assert(!AppAtProtocol_Parse("AT+charge_bypass=ON\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+CHARGE_BYPASS=PRECHARGE\r\n", &command));

    expect_query_command("AT+SENSE?\r\n", APP_AT_COMMAND_QUERY_SENSE);
    expect_query_command("AT+FAULT?\r\n", APP_AT_COMMAND_QUERY_FAULT);
    expect_query_command("AT+MOTOR?\r\n", APP_AT_COMMAND_QUERY_MOTOR);
    expect_query_command("AT+OUTPUT?\r\n", APP_AT_COMMAND_QUERY_OUTPUT);
    expect_query_command("AT+DIAG?\r\n", APP_AT_COMMAND_QUERY_DIAG);
    expect_query_command("AT+VERSION?\r\n", APP_AT_COMMAND_QUERY_VERSION);

    expect_uart_payload("AT+UARTTX=00FF10\r\n", binary_payload, sizeof(binary_payload));
    expect_uart_payload(
        "AT+UARTTX=000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F\r\n",
        maximum_payload,
        sizeof(maximum_payload));
    assert(!AppAtProtocol_Parse("AT+UARTTX=\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+UARTTX=0\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+UARTTX=00ff\r\n", &command));
    assert(!AppAtProtocol_Parse("AT+UARTTX=00-G\r\n", &command));
    assert(!AppAtProtocol_Parse(
        "AT+UARTTX=000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F20\r\n",
        &command));

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
