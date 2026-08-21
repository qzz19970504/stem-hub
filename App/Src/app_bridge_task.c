#include "app_bridge.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"
#include "app_runtime.h"
#include "app_state.h"
#include "app_uart_tunnel.h"

static void App_BridgeDrainUart(uint8_t uart_index)
{
    uint8_t payload[APP_UART_TUNNEL_CHUNK_SIZE];
    char frame[96];
    size_t payload_length = 0U;
    size_t frame_length = 0U;
    bool uart2_enabled = false;
    bool uart3_enabled = false;
    bool enabled;

    App_RuntimeLockBridge();
    App_StateGetBridgeEnabled(&uart2_enabled, &uart3_enabled);
    enabled = (uart_index == 2U) ? uart2_enabled : uart3_enabled;

    if (!enabled)
    {
        App_RuntimeFlushBridgeRx(uart_index);
        App_RuntimeUnlockBridge();
        return;
    }

    while ((payload_length < sizeof(payload))
           && App_RuntimePopBridgeByte(uart_index, &payload[payload_length]))
    {
        payload_length++;
    }

    if ((payload_length > 0U)
        && App_UartTunnelEncodeEvent(
            uart_index, payload, payload_length, frame, sizeof(frame), &frame_length))
    {
        (void)App_RuntimeSendBytes(
            &huart1, (const uint8_t *)frame, (uint16_t)frame_length, APP_UART_TX_TIMEOUT_MS);
    }

    App_RuntimeUnlockBridge();
}

void App_BridgeTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        if (osSemaphoreAcquire(g_app_runtime.bridge_rx_semaphore, osWaitForever) != osOK)
        {
            continue;
        }

        App_BridgeDrainUart(2U);
        App_BridgeDrainUart(3U);
    }
}
