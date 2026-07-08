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

#endif