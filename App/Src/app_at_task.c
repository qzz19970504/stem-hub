#include "app_at.h"

#include <stdio.h>

#include "app_at_protocol.h"
#include "app_config.h"
#include "app_led.h"
#include "app_motor.h"
#include "app_output.h"
#include "app_runtime.h"
#include "app_state.h"
#include "app_sensor.h"

static void App_AtForwardLine(const char *line)
{
    bool uart2_enabled = false;
    bool uart3_enabled = false;

    if (line == NULL)
    {
        return;
    }

    App_StateGetBridgeEnabled(&uart2_enabled, &uart3_enabled);

    if (uart2_enabled)
    {
        App_RuntimeSendText(&huart2, line);
    }

    if (uart3_enabled)
    {
        App_RuntimeSendText(&huart3, line);
    }
}

static void App_AtReplySense(void)
{
    char buffer[256];
    AppSensorSnapshot snapshot;

    if (!App_SensorTryGetSnapshot(&snapshot))
    {
        App_RuntimeSendError("SENSE_NOT_READY");
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
    App_RuntimeSendText(&huart1, buffer);
}

static void App_AtReplyFault(void)
{
    char buffer[96];
    GPIO_PinState drv_fault = HAL_GPIO_ReadPin(nFAULT_GPIO_Port, nFAULT_Pin);
    GPIO_PinState aux_fault = HAL_GPIO_ReadPin(nFLT_GPIO_Port, nFLT_Pin);

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+FAULT:DRV=%u,AUX=%u\r\nOK\r\n",
                   (drv_fault == GPIO_PIN_RESET) ? 1U : 0U,
                   (aux_fault == GPIO_PIN_RESET) ? 1U : 0U);
    App_RuntimeSendText(&huart1, buffer);
}

static void App_AtReplyMotor(void)
{
    char buffer[128];
    AppMotorStatus status;

    if (!App_MotorTryGetStatus(&status))
    {
        App_RuntimeSendError("STATE_BUSY");
        return;
    }

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+MOTOR:MODE=%s,CURRENT_MA=%lu,OVERCURRENT=%u,FAULT=%u\r\nOK\r\n",
                   App_MotorModeToString(status.mode),
                   (unsigned long)status.current_ma,
                   status.overcurrent_latched ? 1U : 0U,
                   status.drv_fault_active ? 1U : 0U);
    App_RuntimeSendText(&huart1, buffer);
}

static void App_AtHandleCommand(const AppAtCommand *command)
{
    bool queued = false;

    if (command == NULL)
    {
        App_RuntimeSendError("BAD_COMMAND");
        return;
    }

    switch (command->type)
    {
    case APP_AT_COMMAND_SET_BRIDGE:
        App_StateSetBridgeEnabled(command->data.bridge.target, command->data.bridge.enabled);
        App_RuntimeSendOk();
        break;
    case APP_AT_COMMAND_SET_LED_MASTER:
        queued = App_LedEnqueueState(command->data.led.enabled);
        queued ? App_RuntimeSendOk() : App_RuntimeSendError("LED_QUEUE");
        break;
    case APP_AT_COMMAND_SET_MOTOR_MODE:
        if (!App_MotorAllowsExternalControl())
        {
            App_RuntimeSendError("MOTOR_TEST_MODE");
            break;
        }
        queued = App_MotorEnqueueMode(command->data.motor.mode);
        queued ? App_RuntimeSendOk() : App_RuntimeSendError("MOTOR_QUEUE");
        break;
    case APP_AT_COMMAND_SET_NMOS1:
        queued = App_OutputEnqueueState(APP_OUTPUT_TARGET_NMOS1, command->data.output.enabled);
        queued ? App_RuntimeSendOk() : App_RuntimeSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_SET_NMOS2:
        queued = App_OutputEnqueueState(APP_OUTPUT_TARGET_NMOS2, command->data.output.enabled);
        queued ? App_RuntimeSendOk() : App_RuntimeSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_SET_EN_UVLO:
        queued = App_OutputEnqueueState(APP_OUTPUT_TARGET_UVLO, command->data.output.enabled);
        queued ? App_RuntimeSendOk() : App_RuntimeSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_QUERY_SENSE:
        App_AtReplySense();
        break;
    case APP_AT_COMMAND_QUERY_FAULT:
        App_AtReplyFault();
        break;
    case APP_AT_COMMAND_QUERY_MOTOR:
        App_AtReplyMotor();
        break;
    default:
        App_RuntimeSendError("UNSUPPORTED");
        break;
    }
}

static void App_AtProcessLine(const char *line)
{
    AppAtCommand command = {0};

    if ((line == NULL) || (line[0] == '\0'))
    {
        return;
    }

    if (!AppAtProtocol_IsAtCommand(line))
    {
        App_AtForwardLine(line);
        return;
    }

    if (!AppAtProtocol_Parse(line, &command))
    {
        App_RuntimeSendError("PARSE");
        return;
    }

    App_AtHandleCommand(&command);
}

void App_AtTask(void *argument)
{
    char line_buffer[APP_UART1_LINE_BUFFER_SIZE];
    size_t line_length = 0U;
    uint8_t byte = 0U;
    bool saw_carriage_return = false;

    (void)argument;

    for (;;)
    {
        if (osSemaphoreAcquire(g_app_runtime.uart1_rx_semaphore, osWaitForever) != osOK)
        {
            continue;
        }

        while (App_RuntimePopUart1Byte(&byte))
        {
            if (line_length + 1U >= sizeof(line_buffer))
            {
                line_length = 0U;
                saw_carriage_return = false;
                App_RuntimeSendError("LINE_TOO_LONG");
                continue;
            }

            line_buffer[line_length++] = (char)byte;

            if (saw_carriage_return)
            {
                if (byte == '\n')
                {
                    line_buffer[line_length] = '\0';
                    App_AtProcessLine(line_buffer);
                    line_length = 0U;
                }

                saw_carriage_return = false;
                continue;
            }

            saw_carriage_return = (byte == '\r');
        }
    }
}