#include "app_core.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_runtime.h"
#include "app_stall_config.h"
#include "app_state.h"
#include "app_uart_chunk_queue.h"
#include "task.h"

AppRuntime g_app_runtime = {0};

/* 诊断计数器：所有字段在 ISR/任务里自增，必须是 volatile 以避免编译器优化丢更新。
 * 读侧通过 App_RuntimeGetDiag 拷一份快照出来。 */
static volatile AppRuntimeDiag g_app_diag = {0};
static AppUartChunkQueue g_uart1_chunk_queue;
static bool g_uart1_next_chunk_silence_before = true;
static volatile bool g_uart1_rx_overflowed = false;

/* USART1 IRQ 入口自增（stm32f1xx_it.c），与 HAL 层完全解耦。*/
volatile uint32_t g_app_diag_usart1_isr_count = 0;

static void App_RuntimeFailFastIfNull(const void *handle)
{
    if (handle == NULL)
    {
        extern void App_RecordFailureAndPrint(uint32_t hint, uint32_t lr_value);
        App_RecordFailureAndPrint(0xE11E0003U, (uint32_t)__builtin_return_address(0));
        Error_Handler();
    }
}

void App_RuntimeCreateObjects(void)
{
    if (!AppUartChunkQueue_Init(&g_uart1_chunk_queue))
    {
        Error_Handler();
    }
    g_uart1_next_chunk_silence_before = true;
    g_uart1_rx_overflowed = false;

    g_app_runtime.uart1_rx_semaphore = osSemaphoreNew(APP_UART1_RING_BUFFER_SIZE, 0U, NULL);
    g_app_runtime.bridge_rx_semaphore =
        osSemaphoreNew(APP_UART_BRIDGE_RING_BUFFER_SIZE * 2U, 0U, NULL);
    g_app_runtime.sensor_ready_semaphore = osSemaphoreNew(1U, 0U, NULL);
    g_app_runtime.uart_tx_mutex = osMutexNew(NULL);
    g_app_runtime.bridge_mutex = osMutexNew(NULL);
    g_app_runtime.sensor_mutex = osMutexNew(NULL);
    g_app_runtime.adc2_mutex = osMutexNew(NULL);
    g_app_runtime.state_mutex = osMutexNew(NULL);
    g_app_runtime.motor_queue = osMessageQueueNew(8U, sizeof(AppMotorRequest), NULL);
    g_app_runtime.led_queue = osMessageQueueNew(4U, sizeof(AppLedRequest), NULL);
    g_app_runtime.output_queue = osMessageQueueNew(8U, sizeof(AppOutputRequest), NULL);

    App_RuntimeFailFastIfNull(g_app_runtime.uart1_rx_semaphore);
    App_RuntimeFailFastIfNull(g_app_runtime.bridge_rx_semaphore);
    App_RuntimeFailFastIfNull(g_app_runtime.sensor_ready_semaphore);
    App_RuntimeFailFastIfNull(g_app_runtime.uart_tx_mutex);
    App_RuntimeFailFastIfNull(g_app_runtime.bridge_mutex);
    App_RuntimeFailFastIfNull(g_app_runtime.sensor_mutex);
    App_RuntimeFailFastIfNull(g_app_runtime.adc2_mutex);
    App_RuntimeFailFastIfNull(g_app_runtime.state_mutex);
    App_RuntimeFailFastIfNull(g_app_runtime.motor_queue);
    App_RuntimeFailFastIfNull(g_app_runtime.led_queue);
    App_RuntimeFailFastIfNull(g_app_runtime.output_queue);
}

void App_RuntimeStartUart1Receive(void)
{
    if (HAL_UARTEx_ReceiveToIdle_IT(&huart1,
                                    g_app_runtime.uart1_rx_chunk,
                                    sizeof(g_app_runtime.uart1_rx_chunk)) != HAL_OK)
    {
        extern void App_RecordFailureAndPrint(uint32_t hint, uint32_t lr_value);
        App_RecordFailureAndPrint(0xE11E0002U, (uint32_t)__builtin_return_address(0));
        Error_Handler();
    }
}

static void App_RuntimeRearmReceiveIfReady(UART_HandleTypeDef *uart,
                                           uint8_t *receive_byte)
{
    if ((uart == NULL) || (receive_byte == NULL))
    {
        return;
    }

    /*
     * For a non-blocking NE/FE/PE error HAL can consume RXNE first. That calls
     * HAL_UART_RxCpltCallback, which has already armed the next byte by the
     * time HAL_UART_ErrorCallback runs. BUSY_RX therefore means recovery is
     * complete, not a fatal HAL_BUSY failure. ORE ends the transfer and leaves
     * RxState READY, so only that state needs an explicit rearm here.
     */
    if (uart->RxState == HAL_UART_STATE_READY)
    {
        (void)HAL_UART_Receive_IT(uart, receive_byte, 1U);
    }
}

void App_RuntimeStartBridgeReceive(void)
{
    if (HAL_UART_Receive_IT(&huart2, &g_app_runtime.uart2_rx_byte, 1U) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_UART_Receive_IT(&huart3, &g_app_runtime.uart3_rx_byte, 1U) != HAL_OK)
    {
        Error_Handler();
    }
}

void App_RuntimeInit(void)
{
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);

    (void)HAL_ADCEx_Calibration_Start(&hadc1);
    (void)HAL_ADCEx_Calibration_Start(&hadc2);

    HAL_GPIO_WritePin(nSLEEP_GPIO_Port, nSLEEP_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(EN_IN1_GPIO_Port, EN_IN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(PH_IN2_GPIO_Port, PH_IN2_Pin, GPIO_PIN_RESET);

    if (!App_StateSetStallCurrentMa(App_StallConfigLoadCurrentMa()))
    {
        Error_Handler();
    }

    App_RuntimeStartUart1Receive();
    App_RuntimeStartBridgeReceive();
}

uint32_t App_RuntimeRawToMillivolts(uint16_t raw)
{
    return ((uint32_t)raw * APP_ADC_VREF_MV) / APP_ADC_MAX_VALUE;
}

HAL_StatusTypeDef App_RuntimeSendBytes(UART_HandleTypeDef *uart,
                                       const uint8_t *data,
                                       uint16_t length,
                                       uint32_t timeout)
{
    HAL_StatusTypeDef status = HAL_ERROR;

    if ((uart == NULL) || (data == NULL) || (length == 0U))
    {
        return HAL_ERROR;
    }

    if (osMutexAcquire(g_app_runtime.uart_tx_mutex, osWaitForever) != osOK)
    {
        return HAL_BUSY;
    }

    if (length > 0U)
    {
        g_app_diag.tx_call_count++;

        /* 只读快照：记录 HAL 状态机在发送前后状态，作为"UART 假死"
         * 与 Cortex-M 异常的分类证据。不修改任何 HAL/USART 寄存器。
         * 历史故障根因是 atTask 栈溢出导致 MLSPERR（见
         * docs/at-rx-stall-debug-report.md），不是 HAL_BUSY；本快照
         * 字段继续保留为被动观测。 */
        g_app_diag.tx_state_pre = (uint32_t)uart->gState;
        g_app_diag.tx_err_pre = (uint32_t)uart->ErrorCode;

        status = HAL_UART_Transmit(uart, (uint8_t *)data, length, timeout);
        g_app_diag.tx_last_status = (uint32_t)status;

        g_app_diag.tx_state_post = (uint32_t)uart->gState;
        g_app_diag.tx_err_post = (uint32_t)uart->ErrorCode;

        if (status == HAL_OK)
        {
            g_app_diag.tx_completed_count++;
        }
        else if (status == HAL_TIMEOUT)
        {
            g_app_diag.tx_timeout_count++;
        }
        else if (status == HAL_BUSY)
        {
            g_app_diag.tx_busy_count++;
        }
        else
        {
            g_app_diag.tx_error_count++;
        }
    }

    (void)osMutexRelease(g_app_runtime.uart_tx_mutex);
    return status;
}

void App_RuntimeSendText(UART_HandleTypeDef *uart, const char *text)
{
    size_t length;

    if (text == NULL)
    {
        return;
    }

    length = strlen(text);
    if ((length > 0U) && (length <= UINT16_MAX))
    {
        (void)App_RuntimeSendBytes(
            uart, (const uint8_t *)text, (uint16_t)length, APP_UART_TX_TIMEOUT_MS);
    }
}

void App_RuntimeSendOk(void)
{
    App_RuntimeSendText(&huart1, "OK\r\n");
}

void App_RuntimeSendError(const char *reason)
{
    char buffer[64];

    if ((reason == NULL) || (reason[0] == '\0'))
    {
        App_RuntimeSendText(&huart1, "ERROR\r\n");
        return;
    }

    (void)snprintf(buffer, sizeof(buffer), "ERROR:%s\r\n", reason);
    App_RuntimeSendText(&huart1, buffer);
}

static bool App_RuntimePushUart1Byte(uint8_t byte)
{
    uint16_t next_head = (uint16_t)((g_app_runtime.uart1_head + 1U) % APP_UART1_RING_BUFFER_SIZE);

    g_app_diag.rx_byte_count++;

    if (next_head == g_app_runtime.uart1_tail)
    {
        g_app_diag.rx_overflow_count++;
        g_uart1_rx_overflowed = true;
        return false;
    }

    g_app_runtime.uart1_ring[g_app_runtime.uart1_head] = byte;
    g_app_runtime.uart1_head = next_head;

    return true;
}

bool App_RuntimePopUart1Chunk(uint8_t *bytes,
                             size_t capacity,
                             size_t *length,
                             bool *silence_before,
                             bool *silence_after)
{
    AppUartChunk chunk;
    size_t byte_index;

    if ((bytes == NULL) || (capacity < APP_UART1_RX_CHUNK_SIZE)
        || (length == NULL) || (silence_before == NULL)
        || (silence_after == NULL) || g_uart1_rx_overflowed
        || AppUartChunkQueue_HasOverflowed(&g_uart1_chunk_queue))
    {
        return false;
    }

    if (!AppUartChunkQueue_Pop(&g_uart1_chunk_queue, &chunk))
    {
        return false;
    }

    for (byte_index = 0U; byte_index < chunk.length; ++byte_index)
    {
        if (g_app_runtime.uart1_tail == g_app_runtime.uart1_head)
        {
            Error_Handler();
            return false;
        }

        bytes[byte_index] = g_app_runtime.uart1_ring[g_app_runtime.uart1_tail];
        g_app_runtime.uart1_tail =
            (uint16_t)((g_app_runtime.uart1_tail + 1U) % APP_UART1_RING_BUFFER_SIZE);
    }

    *length = chunk.length;
    *silence_before = chunk.silence_before;
    *silence_after = chunk.silence_after;
    return true;
}

bool App_RuntimeConsumeUart1Overflow(void)
{
    bool has_overflowed;

    taskENTER_CRITICAL();
    has_overflowed = g_uart1_rx_overflowed
        || AppUartChunkQueue_HasOverflowed(&g_uart1_chunk_queue);
    if (has_overflowed)
    {
        g_app_runtime.uart1_tail = g_app_runtime.uart1_head;
        AppUartChunkQueue_Reset(&g_uart1_chunk_queue);
        g_uart1_next_chunk_silence_before = true;
        g_uart1_rx_overflowed = false;
    }
    taskEXIT_CRITICAL();

    return has_overflowed;
}

static bool App_RuntimePushBridgeByte(uint8_t uart_index, uint8_t byte)
{
    volatile uint16_t *head;
    volatile uint16_t *tail;
    uint8_t *ring;
    uint16_t next_head;

    if (uart_index == 2U)
    {
        head = &g_app_runtime.uart2_head;
        tail = &g_app_runtime.uart2_tail;
        ring = g_app_runtime.uart2_ring;
        g_app_diag.uart2_rx_byte_count++;
    }
    else
    {
        head = &g_app_runtime.uart3_head;
        tail = &g_app_runtime.uart3_tail;
        ring = g_app_runtime.uart3_ring;
        g_app_diag.uart3_rx_byte_count++;
    }

    next_head = (uint16_t)((*head + 1U) % APP_UART_BRIDGE_RING_BUFFER_SIZE);
    if (next_head == *tail)
    {
        if (uart_index == 2U)
        {
            g_app_diag.uart2_rx_overflow_count++;
        }
        else
        {
            g_app_diag.uart3_rx_overflow_count++;
        }
        return false;
    }

    ring[*head] = byte;
    *head = next_head;
    return true;
}

bool App_RuntimePopBridgeByte(uint8_t uart_index, uint8_t *byte)
{
    volatile uint16_t *head;
    volatile uint16_t *tail;
    uint8_t *ring;

    if (byte == NULL)
    {
        return false;
    }

    if (uart_index == 2U)
    {
        head = &g_app_runtime.uart2_head;
        tail = &g_app_runtime.uart2_tail;
        ring = g_app_runtime.uart2_ring;
    }
    else if (uart_index == 3U)
    {
        head = &g_app_runtime.uart3_head;
        tail = &g_app_runtime.uart3_tail;
        ring = g_app_runtime.uart3_ring;
    }
    else
    {
        return false;
    }

    if (*tail == *head)
    {
        return false;
    }

    *byte = ring[*tail];
    *tail = (uint16_t)((*tail + 1U) % APP_UART_BRIDGE_RING_BUFFER_SIZE);
    return true;
}

void App_RuntimeFlushBridgeRx(uint8_t uart_index)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if (uart_index == 2U)
    {
        g_app_runtime.uart2_tail = g_app_runtime.uart2_head;
    }
    else if (uart_index == 3U)
    {
        g_app_runtime.uart3_tail = g_app_runtime.uart3_head;
    }
    __set_PRIMASK(primask);
}

void App_RuntimeLockBridge(void)
{
    if (osMutexAcquire(g_app_runtime.bridge_mutex, osWaitForever) != osOK)
    {
        Error_Handler();
    }
}

void App_RuntimeUnlockBridge(void)
{
    if (osMutexRelease(g_app_runtime.bridge_mutex) != osOK)
    {
        Error_Handler();
    }
}

void App_RuntimeSelectBridgeTarget(AppBridgeTarget target)
{
    App_RuntimeLockBridge();
    App_StateSelectBridgeTarget(target);
    App_RuntimeFlushBridgeRx(2U);
    App_RuntimeFlushBridgeRx(3U);
    App_RuntimeUnlockBridge();
}

void App_RuntimeClearBridgeTarget(void)
{
    App_RuntimeLockBridge();
    App_StateClearBridgeTarget();
    App_RuntimeFlushBridgeRx(2U);
    App_RuntimeFlushBridgeRx(3U);
    App_RuntimeUnlockBridge();
}

bool App_RuntimeReadChannel(ADC_HandleTypeDef *adc, uint32_t channel, uint16_t *raw_value)
{
    ADC_ChannelConfTypeDef config = {0};
    bool success = false;

    if ((adc == NULL) || (raw_value == NULL))
    {
        return false;
    }

    config.Channel = channel;
    config.Rank = ADC_REGULAR_RANK_1;
    config.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;

    if (HAL_ADC_ConfigChannel(adc, &config) != HAL_OK)
    {
        return false;
    }

    if (HAL_ADC_Start(adc) != HAL_OK)
    {
        return false;
    }

    if (HAL_ADC_PollForConversion(adc, APP_ADC_TIMEOUT_MS) == HAL_OK)
    {
        *raw_value = (uint16_t)HAL_ADC_GetValue(adc);
        success = true;
    }

    (void)HAL_ADC_Stop(adc);
    return success;
}

bool App_RuntimeReadAdc2Channel(uint32_t channel, uint16_t *raw_value)
{
    bool success = false;

    if (osMutexAcquire(g_app_runtime.adc2_mutex, osWaitForever) == osOK)
    {
        success = App_RuntimeReadChannel(&hadc2, channel, raw_value);
        (void)osMutexRelease(g_app_runtime.adc2_mutex);
    }

    return success;
}

void App_RuntimeGetDiag(AppRuntimeDiag *out)
{
    if (out == NULL)
    {
        return;
    }

    /* 关中断读快照，避免 ISR 中途修改造成撕裂读。
     * 计数器是 32 位，Cortex-M3 上 32 位对齐访问是原子的，但组合读多字段
     * 时仍可能撕裂——所以这里关一次中断。*/
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    out->rx_isr_count = g_app_diag_usart1_isr_count;
    out->rx_byte_count = g_app_diag.rx_byte_count;
    out->rx_overflow_count = g_app_diag.rx_overflow_count;
    out->rx_error_count = g_app_diag.rx_error_count;
    out->rx_ore_count = g_app_diag.rx_ore_count;
    out->rx_ne_count = g_app_diag.rx_ne_count;
    out->rx_fe_count = g_app_diag.rx_fe_count;
    out->rx_pe_count = g_app_diag.rx_pe_count;
    out->line_too_long_count = g_app_diag.line_too_long_count;
    out->at_loop_count = g_app_diag.at_loop_count;
    out->tx_call_count = g_app_diag.tx_call_count;
    out->tx_completed_count = g_app_diag.tx_completed_count;
    out->tx_timeout_count = g_app_diag.tx_timeout_count;
    out->tx_error_count = g_app_diag.tx_error_count;
    out->tx_busy_count = g_app_diag.tx_busy_count;
    out->tx_state_pre = g_app_diag.tx_state_pre;
    out->tx_state_post = g_app_diag.tx_state_post;
    out->tx_err_pre = g_app_diag.tx_err_pre;
    out->tx_err_post = g_app_diag.tx_err_post;
    out->tx_last_status = g_app_diag.tx_last_status;
    out->sensor_loop_count = g_app_diag.sensor_loop_count;
    out->sensor_publish_count = g_app_diag.sensor_publish_count;
    out->sensor_last_publish_tick = g_app_diag.sensor_last_publish_tick;
    out->sensor_adc1_read_fail_count = g_app_diag.sensor_adc1_read_fail_count;
    out->sensor_adc2_read_fail_count = g_app_diag.sensor_adc2_read_fail_count;
    out->uart2_rx_byte_count = g_app_diag.uart2_rx_byte_count;
    out->uart2_rx_overflow_count = g_app_diag.uart2_rx_overflow_count;
    out->uart3_rx_byte_count = g_app_diag.uart3_rx_byte_count;
    out->uart3_rx_overflow_count = g_app_diag.uart3_rx_overflow_count;
    __set_PRIMASK(primask);
}

void App_RuntimeNoteLineTooLong(void)
{
    g_app_diag.line_too_long_count++;
}

void App_RuntimeNoteAtLoop(void)
{
    g_app_diag.at_loop_count++;
}

void App_RuntimeNoteSensorLoop(void)
{
    g_app_diag.sensor_loop_count++;
}

void App_RuntimeNoteSensorPublish(uint32_t tick)
{
    g_app_diag.sensor_publish_count++;
    g_app_diag.sensor_last_publish_tick = tick;
}

void App_RuntimeNoteSensorAdc1ReadFail(void)
{
    g_app_diag.sensor_adc1_read_fail_count++;
}

void App_RuntimeNoteSensorAdc2ReadFail(void)
{
    g_app_diag.sensor_adc2_read_fail_count++;
}

void App_CoreCreateObjects(void)
{
    App_RuntimeCreateObjects();
}

void App_CoreInit(void)
{
    App_RuntimeInit();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        (void)App_RuntimePushBridgeByte(2U, g_app_runtime.uart2_rx_byte);
        if (g_app_runtime.bridge_rx_semaphore != NULL)
        {
            (void)osSemaphoreRelease(g_app_runtime.bridge_rx_semaphore);
        }
        (void)HAL_UART_Receive_IT(&huart2, &g_app_runtime.uart2_rx_byte, 1U);
    }
    else if (huart->Instance == USART3)
    {
        (void)App_RuntimePushBridgeByte(3U, g_app_runtime.uart3_rx_byte);
        if (g_app_runtime.bridge_rx_semaphore != NULL)
        {
            (void)osSemaphoreRelease(g_app_runtime.bridge_rx_semaphore);
        }
        (void)HAL_UART_Receive_IT(&huart3, &g_app_runtime.uart3_rx_byte, 1U);
    }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t received_length)
{
    const bool silence_after = (huart != NULL)
        && (HAL_UARTEx_GetRxEventType(huart) == HAL_UART_RXEVENT_IDLE);
    bool all_bytes_pushed = true;
    uint16_t byte_index;

    if ((huart == NULL) || (huart->Instance != USART1))
    {
        return;
    }

    if (received_length > sizeof(g_app_runtime.uart1_rx_chunk))
    {
        g_app_diag.rx_overflow_count++;
        g_uart1_rx_overflowed = true;
        all_bytes_pushed = false;
    }
    else
    {
        for (byte_index = 0U; byte_index < received_length; ++byte_index)
        {
            if (!App_RuntimePushUart1Byte(g_app_runtime.uart1_rx_chunk[byte_index]))
            {
                all_bytes_pushed = false;
            }
        }
    }

    if (received_length > 0U)
    {
        if (all_bytes_pushed
            && !AppUartChunkQueue_Push(&g_uart1_chunk_queue,
                                       received_length,
                                       g_uart1_next_chunk_silence_before,
                                       silence_after))
        {
            g_app_diag.rx_overflow_count++;
            g_uart1_rx_overflowed = true;
        }

        if (g_app_runtime.uart1_rx_semaphore != NULL)
        {
            (void)osSemaphoreRelease(g_app_runtime.uart1_rx_semaphore);
        }
    }

    g_uart1_next_chunk_silence_before = silence_after;
    App_RuntimeStartUart1Receive();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        uint32_t error_code = huart->ErrorCode;

        /* 把各类错误拆开计数：ore 可能是数据真的丢了；ne/fe/pe 更可能是线路噪声。 */
        g_app_diag.rx_error_count++;
        if ((error_code & HAL_UART_ERROR_ORE) != 0U)
        {
            g_app_diag.rx_ore_count++;
        }
        if ((error_code & HAL_UART_ERROR_NE) != 0U)
        {
            g_app_diag.rx_ne_count++;
        }
        if ((error_code & HAL_UART_ERROR_FE) != 0U)
        {
            g_app_diag.rx_fe_count++;
        }
        if ((error_code & HAL_UART_ERROR_PE) != 0U)
        {
            g_app_diag.rx_pe_count++;
        }

        g_uart1_rx_overflowed = true;
        g_uart1_next_chunk_silence_before = true;
        if (g_app_runtime.uart1_rx_semaphore != NULL)
        {
            (void)osSemaphoreRelease(g_app_runtime.uart1_rx_semaphore);
        }
        (void)HAL_UART_AbortReceive(&huart1);
        App_RuntimeStartUart1Receive();
    }
    else if (huart->Instance == USART2)
    {
        App_RuntimeRearmReceiveIfReady(
            &huart2,
            &g_app_runtime.uart2_rx_byte);
    }
    else if (huart->Instance == USART3)
    {
        App_RuntimeRearmReceiveIfReady(
            &huart3,
            &g_app_runtime.uart3_rx_byte);
    }
}
