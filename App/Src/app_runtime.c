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

/* UART 看门狗：记录最近一次 RX ISR 的系统 tick (ms)。
 * 在 ISR 里写 (只写 32 位对齐的 volatile 变量)，任务里读。 */
static volatile uint32_t g_app_runtime_last_rx_ms = 0;

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

    /* UART 看门狗：100ms 周期性 software timer。
     * 不放在任何用户任务里——motorTask 可能因未知原因退出，
     * atTask 在 UART 死时会卡在信号量上。timer task 独立于用户任务。 */
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

    /* 喂 UART 看门狗 + 标 armed：从此以后 watchdog 才会检查静默超时，
     * 避免设备上电后无命令期间被误判为 stall。armed 是一次性 sticky 位，
     * 一旦置位不再清零（看门狗也持续监测，避免合法长空闲被误触发）。 */
    g_app_runtime_last_rx_ms = (uint32_t)osKernelGetTickCount();
    g_app_runtime.watchdog_armed = true;

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
    out->uart_watchdog_reset_count = g_app_diag.uart_watchdog_reset_count;
    out->wdg_check_count = g_app_diag.wdg_check_count;
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

/* UART 看门狗触发复位：UART_DeInit + MX_USART1_UART_Init 重新初始化外设，
 * 然后重新启动 RX_IT。这能把 UART1 从"假死"状态里拉回来。
 * 计数到 uart_watchdog_reset_count 方便诊断。
 *
 * 同时清掉 ring buffer、置位 uart_reset_pending，让 atTask 在下次唤醒时
 * 丢掉 line_buffer 半包状态、避免重发命令被旧半包污染。
 *
 * armed 不清零——armed 是 sticky 的，一旦置位表示"用户已经在用 AT 通道"，
 * watchdog 持续监测；清零会让"先发一次命令 → 等 30s → 再发命令"这种合法
 * 节奏被误触发。 */
static void App_RuntimeResetUart1(void)
{
    g_app_diag.uart_watchdog_reset_count++;

    /* 关中断清 ring buffer 和 head/tail——必须原子完成，
     * 否则 ISR 可能在我们清 tail 之后又往 ring 里 push。 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    g_app_runtime.uart1_head = 0U;
    g_app_runtime.uart1_tail = 0U;
    (void)memset(g_app_runtime.uart1_ring, 0, sizeof(g_app_runtime.uart1_ring));
    __set_PRIMASK(primask);

    HAL_UART_DeInit(&huart1);
    MX_USART1_UART_Init();

    /* MX_USART1_UART_Init 已经把 RX_IT 状态机复位了，但需要在调它之前
     * 先清掉 ErrorCode，否则 HAL_UART_Receive_IT 会失败。 */
    huart1.ErrorCode = HAL_UART_ERROR_NONE;

    App_RuntimeStartUart1Receive();

    /* 置位 uart_reset_pending 并 release 信号量，唤醒 atTask 去做
     * 半包清理。释放一个信号量足够：atTask 醒来后看到 reset_pending，
     * 会先清 line_buffer 再正常 drain ring。 */
    g_app_runtime.uart_reset_pending = true;
    if (g_app_runtime.uart1_rx_semaphore != NULL)
    {
        (void)osSemaphoreRelease(g_app_runtime.uart1_rx_semaphore);
    }

    /* 喂狗避免刚复位就被自己的 watchdog 再次触发。 */
    g_app_runtime_last_rx_ms = (uint32_t)osKernelGetTickCount();
}

/* 喂狗——任何成功的 RX ISR 都会通过 App_RuntimePushUart1Byte 间接更新
 * g_app_runtime_last_rx_ms 并置 armed。暴露出来便于测试。 */
void App_RuntimeUartWatchdogKick(void)
{
    g_app_runtime_last_rx_ms = (uint32_t)osKernelGetTickCount();
    g_app_runtime.watchdog_armed = true;
}

/* UART 看门狗 timer 回调——每 100ms 由 FreeRTOS software timer 驱动。
 *
 * Timer 回调约束（FreeRTOS 文档明确要求）：
 * - 不能 block（不能等信号量/队列/互斥锁）
 * - 只能调 interrupt-safe API
 * - 尽快返回
 *
 * 本函数只做：volatile 读 → unsigned 减法 → 条件触发硬件级恢复。
 * 不调任何阻塞 API，不在回调里清 ring buffer（留给 atTask 安全地做）。
 *
 * 三重保护避免误触发：
 *   1. armed=false → 直接返回
 *   2. tick 回绕安全：delta >= threshold && delta < 0x80000000U
 *   3. threshold 可按用户场景调整（app_config.h） */
void App_RuntimeUartWatchdogTick(void *argument)
{
    (void)argument;

    g_app_diag.wdg_check_count++;

    if (!g_app_runtime.watchdog_armed)
    {
        return;
    }

    uint32_t now = (uint32_t)osKernelGetTickCount();
    uint32_t last = g_app_runtime_last_rx_ms;
    uint32_t delta = now - last;

    if ((delta >= APP_UART_WATCHDOG_TIMEOUT_MS) && (delta < 0x80000000U))
    {
        App_RuntimeResetUart1();
        /* ResetUart1 内部已经清 ring、置 uart_reset_pending、release
         * 信号量唤醒 atTask 清半包，以及喂狗。 */
    }
}

uint32_t App_RuntimeGetLastRxMs(void)
{
    return g_app_runtime_last_rx_ms;
}

bool App_RuntimeIsWatchdogArmed(void)
{
    return g_app_runtime.watchdog_armed;
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