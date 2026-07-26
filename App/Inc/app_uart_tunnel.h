#ifndef APP_UART_TUNNEL_H
#define APP_UART_TUNNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

bool App_UartTunnelEncodeEvent(uint8_t uart_index,
                              const uint8_t *payload,
                              size_t payload_length,
                              char *destination,
                              size_t destination_capacity,
                              size_t *encoded_length);

#endif
