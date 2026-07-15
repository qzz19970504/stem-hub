#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#include "adc.h"
#include "cmsis_os.h"
#include "main.h"
#include "usart.h"
#include "app_config.h"
#include "app_types.h"

/* 诊断计数器（用于排查 "AT 命令收不到响应" 类问题）。
 * 字段意义:
 *   rx_byte_count         - RX ISR 总触发次数
 *   rx_overflow_count     - 环形缓冲满时丢字节的次数
 *   rx_error_count        - HAL_UART_ErrorCallback 总次数
 *   rx_ore_count          - 硬件 overrun 错误次数
 *   rx_ne_count           - 噪声错误次数
 *   rx_fe_count           - 帧错误次数
 *   rx_pe_count           - 奇偶校验错误次数
 *   line_too_long_count   - atTask 行缓冲溢出次数 */
typedef struct
{
    uint32_t rx_byte_count;
    uint32_t rx_overflow_count;
    uint32_t rx_error_count;
    uint32_t rx_ore_count;
    uint32_t rx_ne_count;
    uint32_t rx_fe_count;
    uint32_t rx_pe_count;
    uint32_t line_too_long_count;
} AppRuntimeDiag;

typedef struct
{
    uint8_t uart1_rx_byte;
    volatile uint16_t uart1_head;
    volatile uint16_t uart1_tail;
    uint8_t uart1_ring[APP_UART1_RING_BUFFER_SIZE];
    osSemaphoreId_t uart1_rx_semaphore;
    osSemaphoreId_t sensor_ready_semaphore;
    osMutexId_t sensor_mutex;
    osMutexId_t adc2_mutex;
    osMutexId_t state_mutex;
    osMessageQueueId_t motor_queue;
    osMessageQueueId_t led_queue;
    osMessageQueueId_t output_queue;
} AppRuntime;

extern AppRuntime g_app_runtime;

void App_RuntimeCreateObjects(void);
void App_RuntimeInit(void);
void App_RuntimeStartUart1Receive(void);
void App_RuntimeSendText(UART_HandleTypeDef *uart, const char *text);
void App_RuntimeSendOk(void);
void App_RuntimeSendError(const char *reason);
bool App_RuntimePushUart1Byte(uint8_t byte);
bool App_RuntimePopUart1Byte(uint8_t *byte);
uint32_t App_RuntimeRawToMillivolts(uint16_t raw);
bool App_RuntimeReadChannel(ADC_HandleTypeDef *adc, uint32_t channel, uint16_t *raw_value);
bool App_RuntimeReadAdc2Channel(uint32_t channel, uint16_t *raw_value);
void App_RuntimeGetDiag(AppRuntimeDiag *out);
void App_RuntimeNoteLineTooLong(void);

#endif