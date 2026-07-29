#include "app_uart_tunnel.h"

#include <string.h>

bool App_UartTunnelEncodeEvent(uint8_t uart_index,
                              const uint8_t *payload,
                              size_t payload_length,
                              char *destination,
                              size_t destination_capacity,
                              size_t *encoded_length)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    const char *prefix;
    size_t prefix_length;
    size_t required_length;
    size_t index;

    if ((payload == NULL) || (destination == NULL) || (encoded_length == NULL)
        || (payload_length == 0U) || (payload_length > APP_UART_TUNNEL_CHUNK_SIZE))
    {
        return false;
    }

    if (uart_index == 2U)
    {
        prefix = "+UART2RX:";
    }
    else if (uart_index == 3U)
    {
        prefix = "+UART3RX:";
    }
    else
    {
        return false;
    }

    prefix_length = strlen(prefix);
    required_length = prefix_length + (payload_length * 2U) + 2U;
    if (destination_capacity <= required_length)
    {
        return false;
    }

    (void)memcpy(destination, prefix, prefix_length);
    for (index = 0U; index < payload_length; ++index)
    {
        destination[prefix_length + (index * 2U)] = hex_digits[payload[index] >> 4U];
        destination[prefix_length + (index * 2U) + 1U] = hex_digits[payload[index] & 0x0FU];
    }

    destination[required_length - 2U] = '\r';
    destination[required_length - 1U] = '\n';
    destination[required_length] = '\0';
    *encoded_length = required_length;
    return true;
}
