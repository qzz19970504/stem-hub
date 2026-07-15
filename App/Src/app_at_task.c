#include "app_at.h"

#include <limits.h>
#include <stdint.h>
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

/* 把 0.1°C 分辨率的有符号整数格式化为 "X.XC" 或 "-X.XC"（避免 newlib-nano %f）。
 * 异常值 (INT32_MAX) 输出为 "ERR"。*/
static int App_AtFormatTempCenti(char *out, size_t out_size, int32_t centi_c)
{
    if (centi_c == INT32_MAX)
    {
        return snprintf(out, out_size, "ERR");
    }
    if (centi_c < 0)
    {
        unsigned long abs_c = (unsigned long)(-centi_c);
        return snprintf(out, out_size, "-%lu.%luC", abs_c / 10UL, abs_c % 10UL);
    }
    return snprintf(out, out_size, "%lu.%luC",
                    (unsigned long)centi_c / 10UL,
                    (unsigned long)centi_c % 10UL);
}

static void App_AtReplySense(void)
{
    char buffer[256];
    AppSensorSnapshot snapshot;
    long batt_mv = 0L;
    unsigned long batt_v_int = 0UL;
    unsigned long batt_v_dec = 0UL;
    char batt_ntc_str[8];
    char ntc1_str[8];
    char ntc2_str[8];
    char ntc3_str[8];

    if (!App_SensorTryGetSnapshot(&snapshot))
    {
        App_RuntimeSendError("SENSE_NOT_READY");
        return;
    }

    /* physical_value 存的是电池 mV；按 "整数位.小数位V" 输出，避免 newlib-nano
     * 默认不链接 %f 的问题。decimals 四舍五入到 0.1V。 */
    batt_mv = snapshot.battery_voltage.physical_value;
    if (batt_mv < 0L)
    {
        batt_mv = 0L;
    }
    batt_v_int = (unsigned long)batt_mv / 1000UL;
    batt_v_dec = ((unsigned long)batt_mv % 1000UL + 50UL) / 100UL;

    /* BATT_NTC/NTC1/2/3 physical_value 是 0.1°C 的有符号整数，由各自
     * 的 App_SensorConvertBatteryNtc / App_SensorConvertNtcTemperature 算出。*/
    (void)App_AtFormatTempCenti(batt_ntc_str, sizeof(batt_ntc_str), snapshot.battery_ntc.physical_value);
    (void)App_AtFormatTempCenti(ntc1_str, sizeof(ntc1_str), snapshot.ntc1.physical_value);
    (void)App_AtFormatTempCenti(ntc2_str, sizeof(ntc2_str), snapshot.ntc2.physical_value);
    (void)App_AtFormatTempCenti(ntc3_str, sizeof(ntc3_str), snapshot.ntc3.physical_value);

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+SENSE:BATT_NTC=%s,BATT_V=%lu.%luV,NTC1_C=%s,NTC2_C=%s,NTC3_C=%s,TICK=%lu,COUNT=%lu\r\nOK\r\n",
                   batt_ntc_str,
                   batt_v_int,
                   batt_v_dec,
                   ntc1_str,
                   ntc2_str,
                   ntc3_str,
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

/* 诊断应答：暴露 UART1 RX 路径上的关键计数器，用于排查 "AT 命令收不到响应"。
 * 字段含义见 AppRuntimeDiag（app_runtime.h）。非关键路径，不影响其它 AT 命令。 */
static void App_AtReplyDiag(void)
{
    char buffer[192];
    AppRuntimeDiag diag;

    App_RuntimeGetDiag(&diag);

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+DIAG:RX_BYTE=%lu,RX_OVERFLOW=%lu,RX_ERR=%lu,ORE=%lu,NE=%lu,FE=%lu,PE=%lu,LINE_TOO_LONG=%lu\r\nOK\r\n",
                   (unsigned long)diag.rx_byte_count,
                   (unsigned long)diag.rx_overflow_count,
                   (unsigned long)diag.rx_error_count,
                   (unsigned long)diag.rx_ore_count,
                   (unsigned long)diag.rx_ne_count,
                   (unsigned long)diag.rx_fe_count,
                   (unsigned long)diag.rx_pe_count,
                   (unsigned long)diag.line_too_long_count);
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
    case APP_AT_COMMAND_QUERY_DIAG:
        App_AtReplyDiag();
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
                App_RuntimeNoteLineTooLong();
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