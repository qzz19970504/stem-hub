#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "app_uart_tunnel.h"

static void expect_event(uint8_t uart_index,
                         const uint8_t *payload,
                         size_t payload_length,
                         const char *expected)
{
    char output[96] = {0};
    size_t output_length = 0U;

    assert(App_UartTunnelEncodeEvent(
        uart_index,
        payload,
        payload_length,
        output,
        sizeof(output),
        &output_length));
    assert(output_length == strlen(expected));
    assert(memcmp(output, expected, output_length) == 0);
}

int main(void)
{
    static const uint8_t uart2_payload[] = {0x00U, 0x0DU, 0x0AU, 0xFFU};
    static const uint8_t uart3_payload[] = {0x41U, 0x42U, 0x43U};
    char too_small[16] = {0};
    size_t encoded_length = 123U;

    expect_event(2U, uart2_payload, sizeof(uart2_payload), "+UART2RX:000D0AFF\r\n");
    expect_event(3U, uart3_payload, sizeof(uart3_payload), "+UART3RX:414243\r\n");

    assert(!App_UartTunnelEncodeEvent(
        1U, uart2_payload, sizeof(uart2_payload), too_small, sizeof(too_small), &encoded_length));
    assert(!App_UartTunnelEncodeEvent(
        2U, NULL, sizeof(uart2_payload), too_small, sizeof(too_small), &encoded_length));
    assert(!App_UartTunnelEncodeEvent(
        2U, uart2_payload, 0U, too_small, sizeof(too_small), &encoded_length));
    assert(!App_UartTunnelEncodeEvent(
        2U, uart2_payload, sizeof(uart2_payload), too_small, 8U, &encoded_length));

    return 0;
}
