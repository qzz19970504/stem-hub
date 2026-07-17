#include "app_core.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_runtime.h"

AppRuntime g_app_runtime = {0};

/* 诊断计数器：所有字段在 ISR/任务里自增，必须是 volatile 以避免编译器优化丢更新。
 * 读侧通过 App_RuntimeGetDiag 拷一份快照出来。 */
static volatile AppRuntimeDiag g_app_diag = {0};

/* USART1 IRQ 入口自增（stm32f1xx_it.c），与 HAL 层完全解耦。*/
volatile uint32_t g_app_diag_usart1_isr_count = 0;

static void App_RuntimeFailFastIfNull(const void *handle)
{
    if (handle == NULL)
    {
        Error_Handler();
    }
}

void App_RuntimeCreateObjects(void)
{
    g_app_runtime.uart1_rx_semaphore = osSemaphoreNew(APP_UART1_RING_BUFFER_SIZE, 0U, NULL);
    g_app_runtime.sensor_ready_semaphore = osSemaphoreNew(1U, 0U, NULL);
    g_app_runtime.sensor_mutex = osMutexNew(NULL);
    g_app_runtime.adc2_mutex = osMutexNew(NULL);
    g_app_runtime.state_mutex = osMutexNew(NULL);
    g_app_runtime.motor_queue = osMessageQueueNew(8U, sizeof(AppMotorRequest), NULL);
    g_app_runtime.led_queue = osMessageQueueNew(4U, sizeof(AppLedRequest), NULL);
    g_app_runtime.output_queue = osMessageQueueNew(8U, sizeof(AppOutputRequest), NULL);

    App_RuntimeFailFastIfNull(g_app_runtime.uart1_rx_semaphore);
    App_RuntimeFailFastIfNull(g_app_runtime.sensor_ready_semaphore);
    App_RuntimeFailFastIfNull(g_app_runtime.sensor_mutex);
    App_RuntimeFailFastIfNull(g_app_runtime.adc2_mutex);
    App_RuntimeFailFastIfNull(g_app_runtime.state_mutex);
    App_RuntimeFailFastIfNull(g_app_runtime.motor_queue);
    App_RuntimeFailFastIfNull(g_app_runtime.led_queue);
    App_RuntimeFailFastIfNull(g_app_runtime.output_queue);
}

void App_RuntimeStartUart1Receive(void)
{
    if (HAL_UART_Receive_IT(&huart1, &g_app_runtime.uart1_rx_byte, 1U) != HAL_OK)
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

    App_RuntimeStartUart1Receive();
}

uint32_t App_RuntimeRawToMillivolts(uint16_t raw)
{
    return ((uint32_t)raw * APP_ADC_VREF_MV) / APP_ADC_MAX_VALUE;
}

void App_RuntimeSendText(UART_HandleTypeDef *uart, const char *text)
{
    size_t length = strlen(text);

    if (length > 0U)
    {
        g_app_diag.tx_call_count++;
        HAL_StatusTypeDef status = HAL_UART_Transmit(uart, (uint8_t *)text, (uint16_t)length, APP_UART_TX_TIMEOUT_MS);
        if (status == HAL_OK)
        {
            g_app_diag.tx_completed_count++;
        }
        else if (status == HAL_TIMEOUT)
        {
            g_app_diag.tx_timeout_count++;
        }
        else
        {
            g_app_diag.tx_error_count++;
        }
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

bool App_RuntimePushUart1Byte(uint8_t byte)
{
    uint16_t next_head = (uint16_t)((g_app_runtime.uart1_head + 1U) % APP_UART1_RING_BUFFER_SIZE);

    g_app_diag.rx_byte_count++;

    if (next_head == g_app_runtime.uart1_tail)
    {
        g_app_diag.rx_overflow_count++;
        return false;
    }

    g_app_runtime.uart1_ring[g_app_runtime.uart1_head] = byte;
    g_app_runtime.uart1_head = next_head;

    return true;
}

bool App_RuntimePopUart1Byte(uint8_t *byte)
{
    if ((byte == NULL) || (g_app_runtime.uart1_tail == g_app_runtime.uart1_head))
    {
        return false;
    }

    *byte = g_app_runtime.uart1_ring[g_app_runtime.uart1_tail];
    g_app_runtime.uart1_tail = (uint16_t)((g_app_runtime.uart1_tail + 1U) % APP_UART1_RING_BUFFER_SIZE);
    return true;
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
    if (huart->Instance == USART1)
    {
        (void)App_RuntimePushUart1Byte(g_app_runtime.uart1_rx_byte);

        if (g_app_runtime.uart1_rx_semaphore != NULL)
        {
            (void)osSemaphoreRelease(g_app_runtime.uart1_rx_semaphore);
        }

        App_RuntimeStartUart1Receive();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        /* 把各类错误拆开计数：ore 可能是数据真的丢了；ne/fe/pe 更可能是线路噪声。 */
        g_app_diag.rx_error_count++;
        if ((huart->ErrorCode & HAL_UART_ERROR_ORE) != 0U)
        {
            g_app_diag.rx_ore_count++;
        }
        if ((huart->ErrorCode & HAL_UART_ERROR_NE) != 0U)
        {
            g_app_diag.rx_ne_count++;
        }
        if ((huart->ErrorCode & HAL_UART_ERROR_FE) != 0U)
        {
            g_app_diag.rx_fe_count++;
        }
        if ((huart->ErrorCode & HAL_UART_ERROR_PE) != 0U)
        {
            g_app_diag.rx_pe_count++;
        }

        /* 清 ErrorCode，让下一次错误能被准确归类——HAL 默认在 UART_Receive_IT
         * 错误处理里清一部分标志，但 ErrorCode 这个汇总字段我们手动清。*/
        huart->ErrorCode = HAL_UART_ERROR_NONE;

        App_RuntimeStartUart1Receive();
    }
}