#include "app_core.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "adc.h"
#include "app_at_protocol.h"
#include "cmsis_os.h"
#include "gpio.h"
#include "usart.h"

#define APP_UART1_RING_BUFFER_SIZE 256U
#define APP_UART1_LINE_BUFFER_SIZE 128U
#define APP_UART_TX_TIMEOUT_MS 100U
#define APP_ADC_TIMEOUT_MS 10U
#define APP_SENSOR_PERIOD_MS 1000U
#define APP_MOTOR_MONITOR_PERIOD_MS 100U
#define APP_MOTOR_WAKE_DELAY_MS 5U
#define APP_MOTOR_DIRECTION_DEADTIME_MS 20U
#define APP_ADC_VREF_MV 3300U
#define APP_ADC_MAX_VALUE 4095U
#define APP_MOTOR_OVERCURRENT_THRESHOLD_MA 3000U

typedef struct
{
    uint16_t raw;
    uint32_t millivolts;
    int32_t physical_value;
} AppAnalogMeasure;

typedef struct
{
    AppAnalogMeasure battery_ntc;
    AppAnalogMeasure battery_voltage;
    AppAnalogMeasure ntc1;
    AppAnalogMeasure ntc2;
    AppAnalogMeasure ntc3;
    uint32_t sample_tick;
    uint32_t sample_counter;
} AppSensorSnapshot;

typedef struct
{
    AppMotorMode mode;
    uint32_t current_ma;
    bool overcurrent_latched;
    bool drv_fault_active;
} AppMotorStatus;

typedef struct
{
    bool led_master_enabled;
    bool nmos1_enabled;
    bool nmos2_enabled;
    bool uvlo_enabled;
} AppIoStatus;

typedef struct
{
    AppMotorMode mode;
} AppMotorRequest;

typedef struct
{
    bool enabled;
} AppLedRequest;

typedef enum
{
    APP_OUTPUT_TARGET_NMOS1 = 0,
    APP_OUTPUT_TARGET_NMOS2,
    APP_OUTPUT_TARGET_UVLO
} AppOutputTarget;

typedef struct
{
    AppOutputTarget target;
    bool enabled;
} AppOutputRequest;

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
    AppSensorSnapshot sensor_snapshot;
    AppMotorStatus motor_status;
    AppIoStatus io_status;
    bool bridge_uart2_enabled;
    bool bridge_uart3_enabled;
} AppRuntime;

static AppRuntime g_app_runtime = {
    .io_status = {
        .led_master_enabled = true,
        .nmos1_enabled = false,
        .nmos2_enabled = false,
        .uvlo_enabled = false,
    },
    .motor_status = {
        .mode = APP_MOTOR_MODE_SLEEP,
        .current_ma = 0U,
        .overcurrent_latched = false,
        .drv_fault_active = false,
    },
    .bridge_uart2_enabled = false,
    .bridge_uart3_enabled = false,
};

static void App_CoreFailFastIfNull(const void *handle)
{
    if (handle == NULL)
    {
        Error_Handler();
    }
}

static uint32_t App_CoreRawToMillivolts(uint16_t raw)
{
    return ((uint32_t)raw * APP_ADC_VREF_MV) / APP_ADC_MAX_VALUE;
}

static int32_t App_CoreConvertBatteryNtc(uint32_t millivolts)
{
    return (int32_t)millivolts;
}

static int32_t App_CoreConvertBatteryVoltage(uint32_t millivolts)
{
    return (int32_t)millivolts;
}

static int32_t App_CoreConvertNtcTemperature(uint32_t millivolts)
{
    return (int32_t)millivolts;
}

static uint32_t App_CoreConvertMotorCurrent(uint32_t millivolts)
{
    return millivolts;
}

static void App_CoreUpdateMeasure(AppAnalogMeasure *measure,
                                  uint16_t raw,
                                  int32_t (*convert_fn)(uint32_t))
{
    measure->raw = raw;
    measure->millivolts = App_CoreRawToMillivolts(raw);
    measure->physical_value = convert_fn(measure->millivolts);
}

static void App_CoreStartUart1Receive(void)
{
    if (HAL_UART_Receive_IT(&huart1, &g_app_runtime.uart1_rx_byte, 1U) != HAL_OK)
    {
        Error_Handler();
    }
}

static void App_CoreSendText(UART_HandleTypeDef *uart, const char *text)
{
    size_t length = strlen(text);

    if (length > 0U)
    {
        (void)HAL_UART_Transmit(uart, (uint8_t *)text, (uint16_t)length, APP_UART_TX_TIMEOUT_MS);
    }
}

static void App_CoreSendOk(void)
{
    App_CoreSendText(&huart1, "OK\r\n");
}

static void App_CoreSendError(const char *reason)
{
    char buffer[64];

    if ((reason == NULL) || (reason[0] == '\0'))
    {
        App_CoreSendText(&huart1, "ERROR\r\n");
        return;
    }

    (void)snprintf(buffer, sizeof(buffer), "ERROR:%s\r\n", reason);
    App_CoreSendText(&huart1, buffer);
}

static bool App_CorePushUart1Byte(uint8_t byte)
{
    uint16_t next_head = (uint16_t)((g_app_runtime.uart1_head + 1U) % APP_UART1_RING_BUFFER_SIZE);

    if (next_head == g_app_runtime.uart1_tail)
    {
        return false;
    }

    g_app_runtime.uart1_ring[g_app_runtime.uart1_head] = byte;
    g_app_runtime.uart1_head = next_head;
    return true;
}

static bool App_CorePopUart1Byte(uint8_t *byte)
{
    if ((byte == NULL) || (g_app_runtime.uart1_tail == g_app_runtime.uart1_head))
    {
        return false;
    }

    *byte = g_app_runtime.uart1_ring[g_app_runtime.uart1_tail];
    g_app_runtime.uart1_tail = (uint16_t)((g_app_runtime.uart1_tail + 1U) % APP_UART1_RING_BUFFER_SIZE);
    return true;
}

static bool App_CoreReadChannel(ADC_HandleTypeDef *adc, uint32_t channel, uint16_t *raw_value)
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

static bool App_CoreReadAdc2Channel(uint32_t channel, uint16_t *raw_value)
{
    bool success = false;

    if (osMutexAcquire(g_app_runtime.adc2_mutex, osWaitForever) == osOK)
    {
        success = App_CoreReadChannel(&hadc2, channel, raw_value);
        (void)osMutexRelease(g_app_runtime.adc2_mutex);
    }

    return success;
}

static bool App_CoreCopySensorSnapshot(AppSensorSnapshot *snapshot)
{
    bool ready = false;

    if (snapshot == NULL)
    {
        return false;
    }

    if (osSemaphoreAcquire(g_app_runtime.sensor_ready_semaphore, 0U) == osOK)
    {
        if (osMutexAcquire(g_app_runtime.sensor_mutex, osWaitForever) == osOK)
        {
            *snapshot = g_app_runtime.sensor_snapshot;
            (void)osMutexRelease(g_app_runtime.sensor_mutex);
            ready = true;
        }

        (void)osSemaphoreRelease(g_app_runtime.sensor_ready_semaphore);
    }

    return ready;
}

static const char *App_CoreMotorModeToString(AppMotorMode mode)
{
    switch (mode)
    {
    case APP_MOTOR_MODE_SLEEP:
        return "SLEEP";
    case APP_MOTOR_MODE_WAKE:
        return "WAKE";
    case APP_MOTOR_MODE_FORWARD:
        return "FWD";
    case APP_MOTOR_MODE_REVERSE:
        return "REV";
    case APP_MOTOR_MODE_BRAKE:
        return "BRAKE";
    case APP_MOTOR_MODE_STOP:
        return "STOP";
    default:
        return "UNKNOWN";
    }
}

static void App_CoreSetBridgeState(AppBridgeTarget target, bool enabled)
{
    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        return;
    }

    if ((target == APP_BRIDGE_TARGET_UART2) || (target == APP_BRIDGE_TARGET_UART23))
    {
        g_app_runtime.bridge_uart2_enabled = enabled;
    }

    if ((target == APP_BRIDGE_TARGET_UART3) || (target == APP_BRIDGE_TARGET_UART23))
    {
        g_app_runtime.bridge_uart3_enabled = enabled;
    }

    (void)osMutexRelease(g_app_runtime.state_mutex);
}

static void App_CoreForwardLine(const char *line)
{
    char packet[APP_UART1_LINE_BUFFER_SIZE + 3U];
    bool uart2_enabled = false;
    bool uart3_enabled = false;

    if (line == NULL)
    {
        return;
    }

    (void)snprintf(packet, sizeof(packet), "%s\r\n", line);

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
    {
        uart2_enabled = g_app_runtime.bridge_uart2_enabled;
        uart3_enabled = g_app_runtime.bridge_uart3_enabled;
        (void)osMutexRelease(g_app_runtime.state_mutex);
    }

    if (uart2_enabled)
    {
        App_CoreSendText(&huart2, packet);
    }

    if (uart3_enabled)
    {
        App_CoreSendText(&huart3, packet);
    }
}

static void App_CoreReplySense(void)
{
    char buffer[256];
    AppSensorSnapshot snapshot;

    if (!App_CoreCopySensorSnapshot(&snapshot))
    {
        App_CoreSendError("SENSE_NOT_READY");
        return;
    }

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+SENSE:BATT_NTC=%ld,BATT_MV=%lu,NTC1=%ld,NTC2=%ld,NTC3=%ld,TICK=%lu,COUNT=%lu\r\nOK\r\n",
                   (long)snapshot.battery_ntc.physical_value,
                   (unsigned long)snapshot.battery_voltage.physical_value,
                   (long)snapshot.ntc1.physical_value,
                   (long)snapshot.ntc2.physical_value,
                   (long)snapshot.ntc3.physical_value,
                   (unsigned long)snapshot.sample_tick,
                   (unsigned long)snapshot.sample_counter);
    App_CoreSendText(&huart1, buffer);
}

static void App_CoreReplyFault(void)
{
    char buffer[96];
    GPIO_PinState drv_fault = HAL_GPIO_ReadPin(nFAULT_GPIO_Port, nFAULT_Pin);
    GPIO_PinState aux_fault = HAL_GPIO_ReadPin(nFLT_GPIO_Port, nFLT_Pin);

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+FAULT:DRV=%u,AUX=%u\r\nOK\r\n",
                   (drv_fault == GPIO_PIN_RESET) ? 1U : 0U,
                   (aux_fault == GPIO_PIN_RESET) ? 1U : 0U);
    App_CoreSendText(&huart1, buffer);
}

static void App_CoreReplyMotor(void)
{
    char buffer[128];
    AppMotorStatus status;

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
    {
        App_CoreSendError("STATE_BUSY");
        return;
    }

    status = g_app_runtime.motor_status;
    (void)osMutexRelease(g_app_runtime.state_mutex);

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+MOTOR:MODE=%s,CURRENT_MA=%lu,OVERCURRENT=%u,FAULT=%u\r\nOK\r\n",
                   App_CoreMotorModeToString(status.mode),
                   (unsigned long)status.current_ma,
                   status.overcurrent_latched ? 1U : 0U,
                   status.drv_fault_active ? 1U : 0U);
    App_CoreSendText(&huart1, buffer);
}

static bool App_CoreQueueMotorMode(AppMotorMode mode)
{
    AppMotorRequest request = {.mode = mode};

    return osMessageQueuePut(g_app_runtime.motor_queue, &request, 0U, 0U) == osOK;
}

static bool App_CoreQueueLedState(bool enabled)
{
    AppLedRequest request = {.enabled = enabled};

    return osMessageQueuePut(g_app_runtime.led_queue, &request, 0U, 0U) == osOK;
}

static bool App_CoreQueueOutputState(AppOutputTarget target, bool enabled)
{
    AppOutputRequest request = {
        .target = target,
        .enabled = enabled,
    };

    return osMessageQueuePut(g_app_runtime.output_queue, &request, 0U, 0U) == osOK;
}

static void App_CoreHandleCommand(const AppAtCommand *command)
{
    bool queued = false;

    if (command == NULL)
    {
        App_CoreSendError("BAD_COMMAND");
        return;
    }

    switch (command->type)
    {
    case APP_AT_COMMAND_SET_BRIDGE:
        App_CoreSetBridgeState(command->data.bridge.target, command->data.bridge.enabled);
        App_CoreSendOk();
        break;
    case APP_AT_COMMAND_SET_LED_MASTER:
        queued = App_CoreQueueLedState(command->data.led.enabled);
        queued ? App_CoreSendOk() : App_CoreSendError("LED_QUEUE");
        break;
    case APP_AT_COMMAND_SET_MOTOR_MODE:
        queued = App_CoreQueueMotorMode(command->data.motor.mode);
        queued ? App_CoreSendOk() : App_CoreSendError("MOTOR_QUEUE");
        break;
    case APP_AT_COMMAND_SET_NMOS1:
        queued = App_CoreQueueOutputState(APP_OUTPUT_TARGET_NMOS1, command->data.output.enabled);
        queued ? App_CoreSendOk() : App_CoreSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_SET_NMOS2:
        queued = App_CoreQueueOutputState(APP_OUTPUT_TARGET_NMOS2, command->data.output.enabled);
        queued ? App_CoreSendOk() : App_CoreSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_SET_EN_UVLO:
        queued = App_CoreQueueOutputState(APP_OUTPUT_TARGET_UVLO, command->data.output.enabled);
        queued ? App_CoreSendOk() : App_CoreSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_QUERY_SENSE:
        App_CoreReplySense();
        break;
    case APP_AT_COMMAND_QUERY_FAULT:
        App_CoreReplyFault();
        break;
    case APP_AT_COMMAND_QUERY_MOTOR:
        App_CoreReplyMotor();
        break;
    default:
        App_CoreSendError("UNSUPPORTED");
        break;
    }
}

static void App_CoreProcessLine(const char *line)
{
    AppAtCommand command = {0};

    if ((line == NULL) || (line[0] == '\0'))
    {
        return;
    }

    if (!AppAtProtocol_IsAtCommand(line))
    {
        App_CoreForwardLine(line);
        return;
    }

    if (!AppAtProtocol_Parse(line, &command))
    {
        App_CoreSendError("PARSE");
        return;
    }

    App_CoreHandleCommand(&command);
}

static void App_CoreSetMotorOutputs(GPIO_PinState n_sleep,
                                    GPIO_PinState enable,
                                    GPIO_PinState phase)
{
    HAL_GPIO_WritePin(nSLEEP_GPIO_Port, nSLEEP_Pin, n_sleep);
    HAL_GPIO_WritePin(EN_IN1_GPIO_Port, EN_IN1_Pin, enable);
    HAL_GPIO_WritePin(PH_IN2_GPIO_Port, PH_IN2_Pin, phase);
}

static void App_CoreStoreMotorStatus(AppMotorMode mode,
                                     uint32_t current_ma,
                                     bool overcurrent_latched)
{
    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
    {
        g_app_runtime.motor_status.mode = mode;
        g_app_runtime.motor_status.current_ma = current_ma;
        g_app_runtime.motor_status.overcurrent_latched = overcurrent_latched;
        g_app_runtime.motor_status.drv_fault_active = (HAL_GPIO_ReadPin(nFAULT_GPIO_Port, nFAULT_Pin) == GPIO_PIN_RESET);
        (void)osMutexRelease(g_app_runtime.state_mutex);
    }
}

static void App_CoreApplyMotorMode(AppMotorMode mode)
{
    AppMotorMode previous_mode = APP_MOTOR_MODE_SLEEP;
    bool overcurrent_latched = false;

    if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
    {
        previous_mode = g_app_runtime.motor_status.mode;
        overcurrent_latched = g_app_runtime.motor_status.overcurrent_latched;
        (void)osMutexRelease(g_app_runtime.state_mutex);
    }

    switch (mode)
    {
    case APP_MOTOR_MODE_SLEEP:
        App_CoreSetMotorOutputs(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET);
        App_CoreStoreMotorStatus(APP_MOTOR_MODE_SLEEP, 0U, overcurrent_latched);
        break;
    case APP_MOTOR_MODE_WAKE:
        App_CoreSetMotorOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET);
        osDelay(APP_MOTOR_WAKE_DELAY_MS);
        App_CoreStoreMotorStatus(APP_MOTOR_MODE_WAKE, 0U, false);
        break;
    case APP_MOTOR_MODE_BRAKE:
    case APP_MOTOR_MODE_STOP:
        App_CoreSetMotorOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET);
        App_CoreStoreMotorStatus(mode, 0U, false);
        break;
    case APP_MOTOR_MODE_FORWARD:
    case APP_MOTOR_MODE_REVERSE:
        if (previous_mode == APP_MOTOR_MODE_SLEEP)
        {
            App_CoreSetMotorOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET);
            osDelay(APP_MOTOR_WAKE_DELAY_MS);
        }

        if (((previous_mode == APP_MOTOR_MODE_FORWARD) && (mode == APP_MOTOR_MODE_REVERSE))
            || ((previous_mode == APP_MOTOR_MODE_REVERSE) && (mode == APP_MOTOR_MODE_FORWARD)))
        {
            HAL_GPIO_WritePin(EN_IN1_GPIO_Port, EN_IN1_Pin, GPIO_PIN_RESET);
            osDelay(APP_MOTOR_DIRECTION_DEADTIME_MS);
        }

        HAL_GPIO_WritePin(PH_IN2_GPIO_Port,
                          PH_IN2_Pin,
                          (mode == APP_MOTOR_MODE_FORWARD) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(nSLEEP_GPIO_Port, nSLEEP_Pin, GPIO_PIN_SET);
        HAL_GPIO_WritePin(EN_IN1_GPIO_Port, EN_IN1_Pin, GPIO_PIN_SET);
        App_CoreStoreMotorStatus(mode, 0U, overcurrent_latched);
        break;
    default:
        break;
    }
}

static bool App_CoreReadMotorCurrent(uint32_t *current_ma)
{
    uint16_t raw = 0U;

    if ((current_ma == NULL) || !App_CoreReadAdc2Channel(ADC_CHANNEL_8, &raw))
    {
        return false;
    }

    *current_ma = App_CoreConvertMotorCurrent(App_CoreRawToMillivolts(raw));
    return true;
}

void App_CoreCreateObjects(void)
{
    g_app_runtime.uart1_rx_semaphore = osSemaphoreNew(APP_UART1_RING_BUFFER_SIZE, 0U, NULL);
    g_app_runtime.sensor_ready_semaphore = osSemaphoreNew(1U, 0U, NULL);
    g_app_runtime.sensor_mutex = osMutexNew(NULL);
    g_app_runtime.adc2_mutex = osMutexNew(NULL);
    g_app_runtime.state_mutex = osMutexNew(NULL);
    g_app_runtime.motor_queue = osMessageQueueNew(8U, sizeof(AppMotorRequest), NULL);
    g_app_runtime.led_queue = osMessageQueueNew(4U, sizeof(AppLedRequest), NULL);
    g_app_runtime.output_queue = osMessageQueueNew(8U, sizeof(AppOutputRequest), NULL);

    App_CoreFailFastIfNull(g_app_runtime.uart1_rx_semaphore);
    App_CoreFailFastIfNull(g_app_runtime.sensor_ready_semaphore);
    App_CoreFailFastIfNull(g_app_runtime.sensor_mutex);
    App_CoreFailFastIfNull(g_app_runtime.adc2_mutex);
    App_CoreFailFastIfNull(g_app_runtime.state_mutex);
    App_CoreFailFastIfNull(g_app_runtime.motor_queue);
    App_CoreFailFastIfNull(g_app_runtime.led_queue);
    App_CoreFailFastIfNull(g_app_runtime.output_queue);
}

void App_CoreInit(void)
{
    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_SET);

    (void)HAL_ADCEx_Calibration_Start(&hadc1);
    (void)HAL_ADCEx_Calibration_Start(&hadc2);

    App_CoreSetMotorOutputs(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET);
    App_CoreStartUart1Receive();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        (void)App_CorePushUart1Byte(g_app_runtime.uart1_rx_byte);

        if (g_app_runtime.uart1_rx_semaphore != NULL)
        {
            (void)osSemaphoreRelease(g_app_runtime.uart1_rx_semaphore);
        }

        App_CoreStartUart1Receive();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        App_CoreStartUart1Receive();
    }
}

void App_AtTask(void *argument)
{
    char line_buffer[APP_UART1_LINE_BUFFER_SIZE];
    size_t line_length = 0U;
    uint8_t byte = 0U;

    (void)argument;

    for (;;)
    {
        if (osSemaphoreAcquire(g_app_runtime.uart1_rx_semaphore, osWaitForever) != osOK)
        {
            continue;
        }

        while (App_CorePopUart1Byte(&byte))
        {
            if ((byte == '\r') || (byte == '\n'))
            {
                if (line_length > 0U)
                {
                    line_buffer[line_length] = '\0';
                    App_CoreProcessLine(line_buffer);
                    line_length = 0U;
                }

                continue;
            }

            if (line_length + 1U >= sizeof(line_buffer))
            {
                line_length = 0U;
                App_CoreSendError("LINE_TOO_LONG");
                continue;
            }

            line_buffer[line_length++] = (char)byte;
        }
    }
}

void App_SensorTask(void *argument)
{
    AppSensorSnapshot next_snapshot;
    uint16_t raw = 0U;
    bool success = false;

    (void)argument;

    for (;;)
    {
        (void)memset(&next_snapshot, 0, sizeof(next_snapshot));
        success = App_CoreReadChannel(&hadc1, ADC_CHANNEL_4, &raw);
        if (success)
        {
            App_CoreUpdateMeasure(&next_snapshot.battery_ntc, raw, App_CoreConvertBatteryNtc);
        }

        if (success)
        {
            success = App_CoreReadChannel(&hadc1, ADC_CHANNEL_5, &raw);
            if (success)
            {
                App_CoreUpdateMeasure(&next_snapshot.battery_voltage, raw, App_CoreConvertBatteryVoltage);
            }
        }

        if (success)
        {
            success = App_CoreReadAdc2Channel(ADC_CHANNEL_6, &raw);
            if (success)
            {
                App_CoreUpdateMeasure(&next_snapshot.ntc3, raw, App_CoreConvertNtcTemperature);
            }
        }

        if (success)
        {
            success = App_CoreReadAdc2Channel(ADC_CHANNEL_7, &raw);
            if (success)
            {
                App_CoreUpdateMeasure(&next_snapshot.ntc2, raw, App_CoreConvertNtcTemperature);
            }
        }

        if (success)
        {
            success = App_CoreReadAdc2Channel(ADC_CHANNEL_9, &raw);
            if (success)
            {
                App_CoreUpdateMeasure(&next_snapshot.ntc1, raw, App_CoreConvertNtcTemperature);
            }
        }

        if (success && (osMutexAcquire(g_app_runtime.sensor_mutex, osWaitForever) == osOK))
        {
            next_snapshot.sample_tick = osKernelGetTickCount();
            next_snapshot.sample_counter = g_app_runtime.sensor_snapshot.sample_counter + 1U;
            g_app_runtime.sensor_snapshot = next_snapshot;
            (void)osMutexRelease(g_app_runtime.sensor_mutex);

            if (osSemaphoreGetCount(g_app_runtime.sensor_ready_semaphore) == 0U)
            {
                (void)osSemaphoreRelease(g_app_runtime.sensor_ready_semaphore);
            }
        }

        osDelay(APP_SENSOR_PERIOD_MS);
    }
}

void App_MotorTask(void *argument)
{
    AppMotorRequest request;
    AppMotorStatus snapshot;
    uint32_t current_ma = 0U;

    (void)argument;

    for (;;)
    {
        if (osMessageQueueGet(g_app_runtime.motor_queue, &request, NULL, APP_MOTOR_MONITOR_PERIOD_MS) == osOK)
        {
            App_CoreApplyMotorMode(request.mode);
        }

        if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) != osOK)
        {
            continue;
        }

        snapshot = g_app_runtime.motor_status;
        (void)osMutexRelease(g_app_runtime.state_mutex);

        if ((snapshot.mode != APP_MOTOR_MODE_FORWARD) && (snapshot.mode != APP_MOTOR_MODE_REVERSE))
        {
            continue;
        }

        if (!App_CoreReadMotorCurrent(&current_ma))
        {
            continue;
        }

        if (current_ma >= APP_MOTOR_OVERCURRENT_THRESHOLD_MA)
        {
            App_CoreSetMotorOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, HAL_GPIO_ReadPin(PH_IN2_GPIO_Port, PH_IN2_Pin));
            App_CoreStoreMotorStatus(APP_MOTOR_MODE_BRAKE, current_ma, true);
            continue;
        }

        App_CoreStoreMotorStatus(snapshot.mode, current_ma, snapshot.overcurrent_latched);
    }
}

void App_LedTask(void *argument)
{
    AppLedRequest request;
    AppMotorMode mode = APP_MOTOR_MODE_SLEEP;
    bool led_master_enabled = true;

    (void)argument;

    for (;;)
    {
        if (osMessageQueueGet(g_app_runtime.led_queue, &request, NULL, 100U) == osOK)
        {
            if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
            {
                g_app_runtime.io_status.led_master_enabled = request.enabled;
                (void)osMutexRelease(g_app_runtime.state_mutex);
            }
        }

        if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
        {
            led_master_enabled = g_app_runtime.io_status.led_master_enabled;
            mode = g_app_runtime.motor_status.mode;
            (void)osMutexRelease(g_app_runtime.state_mutex);
        }

        HAL_GPIO_WritePin(LED2_GPIO_Port,
                          LED2_Pin,
                          (led_master_enabled && (mode == APP_MOTOR_MODE_FORWARD)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED3_GPIO_Port,
                          LED3_Pin,
                          (led_master_enabled && (mode == APP_MOTOR_MODE_REVERSE)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}

void App_NmosTask(void *argument)
{
    AppOutputRequest request;

    (void)argument;

    for (;;)
    {
        if (osMessageQueueGet(g_app_runtime.output_queue, &request, NULL, osWaitForever) != osOK)
        {
            continue;
        }

        switch (request.target)
        {
        case APP_OUTPUT_TARGET_NMOS1:
            HAL_GPIO_WritePin(NMOS1_GPIO_Port, NMOS1_Pin, request.enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
            if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
            {
                g_app_runtime.io_status.nmos1_enabled = request.enabled;
                (void)osMutexRelease(g_app_runtime.state_mutex);
            }
            break;
        case APP_OUTPUT_TARGET_NMOS2:
            HAL_GPIO_WritePin(NMOS2_GPIO_Port, NMOS2_Pin, request.enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
            if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
            {
                g_app_runtime.io_status.nmos2_enabled = request.enabled;
                (void)osMutexRelease(g_app_runtime.state_mutex);
            }
            break;
        case APP_OUTPUT_TARGET_UVLO:
            HAL_GPIO_WritePin(EN_UVLO_GPIO_Port, EN_UVLO_Pin, request.enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
            if (osMutexAcquire(g_app_runtime.state_mutex, osWaitForever) == osOK)
            {
                g_app_runtime.io_status.uvlo_enabled = request.enabled;
                (void)osMutexRelease(g_app_runtime.state_mutex);
            }
            break;
        default:
            break;
        }
    }
}