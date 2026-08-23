#include "app_at.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cmsis_os.h"
#include "task.h"

#include "app_at_protocol.h"
#include "app_at_command_guard.h"
#include "app_config.h"
#include "app_led.h"
#include "app_motor.h"
#include "app_output.h"
#include "app_resistor_bypass.h"
#include "app_runtime.h"
#include "app_state.h"
#include "app_stall_config_service.h"
#include "app_sensor.h"

/* task handle 在 freertos.c 里以 file-scope 全局定义；这里 extern 复用，
 * 不引入新头文件依赖，避免触动 CubeMX USER CODE 块。*/
extern osThreadId_t atTaskHandle;
extern osThreadId_t sensorTaskHandle;
extern osThreadId_t motorTaskHandle;

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

static void App_AtSendUartPayload(const AppAtUartPayloadCommand *payload)
{
    bool uart2_enabled = false;
    bool uart3_enabled = false;
    HAL_StatusTypeDef uart2_status = HAL_OK;
    HAL_StatusTypeDef uart3_status = HAL_OK;

    if ((payload == NULL) || (payload->length == 0U))
    {
        App_RuntimeSendError("HEX");
        return;
    }

    App_StateGetBridgeEnabled(&uart2_enabled, &uart3_enabled);
    if (!uart2_enabled && !uart3_enabled)
    {
        App_RuntimeSendError("UART_DISABLED");
        return;
    }

    if (uart2_enabled)
    {
        uart2_status = App_RuntimeSendBytes(
            &huart2,
            payload->bytes,
            (uint16_t)payload->length,
            APP_UART_TX_TIMEOUT_MS);
    }
    if (uart3_enabled)
    {
        uart3_status = App_RuntimeSendBytes(
            &huart3,
            payload->bytes,
            (uint16_t)payload->length,
            APP_UART_TX_TIMEOUT_MS);
    }

    if ((uart2_status == HAL_OK) && (uart3_status == HAL_OK))
    {
        App_RuntimeSendOk();
    }
    else
    {
        App_RuntimeSendError("UART_TX");
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
    char buffer[384];
    AppSensorSnapshot snapshot;
    long batt_mv = 0L;
    unsigned long batt_v_int = 0UL;
    unsigned long batt_v_dec = 0UL;
    char batt_ntc_str[8];
    char mcu_str[8];
    char lm51770_str[8];
    char mp4317_str[8];
    char drv8874_str[8];
    char charge_mos_str[8];
    int response_length;

    /* 高水位 (HighWaterMark) 单位 = StackType_t word (4 B)。
     * 在 SENSE 行尾追加 STK_AT/STK_SENSOR/STK_MOTOR，便于上位机被动采样，
     * 不引入新 task、不动 RTOS 配置/栈分配。
     * 历史背景：这些字段在排查 atTask 栈溢出时引入；详见
     * docs/at-rx-stall-debug-report.md §3 之后的章节。 */
    UBaseType_t stk_at = (atTaskHandle != NULL)
        ? uxTaskGetStackHighWaterMark((TaskHandle_t)atTaskHandle) : 0U;
    UBaseType_t stk_sensor = (sensorTaskHandle != NULL)
        ? uxTaskGetStackHighWaterMark((TaskHandle_t)sensorTaskHandle) : 0U;
    UBaseType_t stk_motor = (motorTaskHandle != NULL)
        ? uxTaskGetStackHighWaterMark((TaskHandle_t)motorTaskHandle) : 0U;

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

    /* 所有温度 physical_value 都是 0.1°C 的有符号整数。 */
    (void)App_AtFormatTempCenti(batt_ntc_str, sizeof(batt_ntc_str), snapshot.battery_ntc.physical_value);
    (void)App_AtFormatTempCenti(
        mcu_str, sizeof(mcu_str), snapshot.mcu_temperature.physical_value);
    (void)App_AtFormatTempCenti(
        lm51770_str,
        sizeof(lm51770_str),
        snapshot.lm51770_temperature.physical_value);
    (void)App_AtFormatTempCenti(
        mp4317_str,
        sizeof(mp4317_str),
        snapshot.mp4317_temperature.physical_value);
    (void)App_AtFormatTempCenti(
        drv8874_str,
        sizeof(drv8874_str),
        snapshot.drv8874_temperature.physical_value);
    (void)App_AtFormatTempCenti(
        charge_mos_str,
        sizeof(charge_mos_str),
        snapshot.charge_mos_temperature.physical_value);

    /* TX 状态机快照（HAL_StatusTypeDef 数值：0=OK,1=ERROR,2=BUSY,3=TIMEOUT）。
     * 通过 AT+SENSE? 尾随字段暴露最近一次发送前的 huart->gState 与返回值，
     * 用于区分 "UART 假死" vs "固件死了"。已知历史故障是栈溢出导致的
     * MemManage + HardFault，而不是 HAL_BUSY；这些字段继续保留为被动观测。 */
    AppRuntimeDiag tail_diag;
    App_RuntimeGetDiag(&tail_diag);

    response_length = snprintf(
        buffer,
        sizeof(buffer),
        "+SENSE:BATT_NTC=%s,BATT_V=%lu.%luV,MCU_C=%s,LM51770_C=%s,MP4317_C=%s,DRV8874_C=%s,CHARGE_MOS_C=%s,MOTOR_I=%lu.%luA,TICK=%lu,COUNT=%lu,STK_AT=%lu,STK_SENSOR=%lu,STK_MOTOR=%lu,TX_SP=%lu,TX_LS=%lu\r\nOK\r\n",
        batt_ntc_str,
        batt_v_int,
        batt_v_dec,
        mcu_str,
        lm51770_str,
        mp4317_str,
        drv8874_str,
        charge_mos_str,
        (unsigned long)(snapshot.motor_current_a_deci / 10UL),
        (unsigned long)(snapshot.motor_current_a_deci % 10UL),
        (unsigned long)snapshot.sample_tick,
        (unsigned long)snapshot.sample_counter,
        (unsigned long)stk_at,
        (unsigned long)stk_sensor,
        (unsigned long)stk_motor,
        (unsigned long)tail_diag.tx_state_pre,
        (unsigned long)tail_diag.tx_last_status);
    if ((response_length < 0) || ((size_t)response_length >= sizeof(buffer)))
    {
        App_RuntimeSendError("RESPONSE_TOO_LONG");
        return;
    }
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
 * 字段含义见 AppRuntimeDiag（app_runtime.h）。非关键路径，不影响其它 AT 命令。
 *
 * 历史：TX_BUSY、TX_STATE_PRE/POST、TX_ERR_PRE/POST、TX_LAST_STATUS 这几个
 * HAL 状态机快照字段是在排查 "AT 命令后跑飞" 时加入的；当时怀疑是 HAL 假死，
 * 最终实机证据 (CFSR=0x00020000, HFSR=0x40000000) 证明根因是 atTask 栈溢出
 * 导致 MLSPERR + FORCED HardFault，与 HAL_BUSY 无关。这些字段保留为被动观测，
 * 不参与任何自动恢复或 watchdog 行为。详见 docs/at-rx-stall-debug-report.md。 */
/* 握手 / 版本查询：返回 APP_FIRMWARE_VERSION（app_config.h 里改一行）。
 * 上位机连上 UART1 后发 AT+VERSION?，收到回包即确认固件可解析且能应答。*/
static void App_AtReplyVersion(void)
{
    char buffer[64];

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+VERSION:%s\r\nOK\r\n",
                   APP_FIRMWARE_VERSION);
    App_RuntimeSendText(&huart1, buffer);
}

static void App_AtReplyDiag(void)
{
    char buffer[640];
    AppRuntimeDiag diag;

    App_RuntimeGetDiag(&diag);

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+DIAG:RX_ISR=%lu,RX_BYTE=%lu,RX_OVERFLOW=%lu,RX_ERR=%lu,ORE=%lu,NE=%lu,FE=%lu,PE=%lu,LINE_TOO_LONG=%lu,AT_LOOP=%lu,TX_CALL=%lu,TX_OK=%lu,TX_TIMEOUT=%lu,TX_ERR=%lu,TX_BUSY=%lu,TX_STATE_PRE=%lu,TX_STATE_POST=%lu,TX_ERR_PRE=%lu,TX_ERR_POST=%lu,TX_LAST_STATUS=%lu,SENSOR_LOOP=%lu,SENSOR_PUBLISH=%lu,SENSOR_LAST_PUBLISH_TICK=%lu,SENSOR_ADC1_READ_FAIL=%lu,SENSOR_ADC2_READ_FAIL=%lu,UART2_RX_BYTE=%lu,UART2_RX_OVERFLOW=%lu,UART3_RX_BYTE=%lu,UART3_RX_OVERFLOW=%lu\r\n",
                   (unsigned long)diag.rx_isr_count,
                   (unsigned long)diag.rx_byte_count,
                   (unsigned long)diag.rx_overflow_count,
                   (unsigned long)diag.rx_error_count,
                   (unsigned long)diag.rx_ore_count,
                   (unsigned long)diag.rx_ne_count,
                   (unsigned long)diag.rx_fe_count,
                   (unsigned long)diag.rx_pe_count,
                   (unsigned long)diag.line_too_long_count,
                   (unsigned long)diag.at_loop_count,
                   (unsigned long)diag.tx_call_count,
                   (unsigned long)diag.tx_completed_count,
                   (unsigned long)diag.tx_timeout_count,
                   (unsigned long)diag.tx_error_count,
                   (unsigned long)diag.tx_busy_count,
                   (unsigned long)diag.tx_state_pre,
                   (unsigned long)diag.tx_state_post,
                   (unsigned long)diag.tx_err_pre,
                   (unsigned long)diag.tx_err_post,
                   (unsigned long)diag.tx_last_status,
                   (unsigned long)diag.sensor_loop_count,
                   (unsigned long)diag.sensor_publish_count,
                   (unsigned long)diag.sensor_last_publish_tick,
                   (unsigned long)diag.sensor_adc1_read_fail_count,
                   (unsigned long)diag.sensor_adc2_read_fail_count,
                   (unsigned long)diag.uart2_rx_byte_count,
                   (unsigned long)diag.uart2_rx_overflow_count,
                   (unsigned long)diag.uart3_rx_byte_count,
                   (unsigned long)diag.uart3_rx_overflow_count);
    App_RuntimeSendText(&huart1, buffer);
    App_RuntimeSendOk();
}

static void App_AtReplyChargeTime(void)
{
    char buffer[48];
    uint32_t seconds = 0U;

    if (!App_StateTryGetChargeOnTimeSeconds(&seconds))
    {
        App_RuntimeSendError("STATE_BUSY");
        return;
    }

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+CHARGE_TIME:%lu\r\nOK\r\n",
                   (unsigned long)seconds);
    App_RuntimeSendText(&huart1, buffer);
}

static void App_AtReplyStallCurrent(void)
{
    char buffer[64];
    uint32_t current_ma = 0U;

    if (!App_StateTryGetStallCurrentMa(&current_ma))
    {
        App_RuntimeSendError("STATE_BUSY");
        return;
    }

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "+STALL_CURRENT:%lu\r\nOK\r\n",
                   (unsigned long)current_ma);
    App_RuntimeSendText(&huart1, buffer);
}

static void App_AtHandleMotorBypass(bool enabled)
{
    AppMotorStatus status;

    if (enabled)
    {
        if (!App_MotorTryGetStatus(&status))
        {
            App_RuntimeSendError("STATE_BUSY");
            return;
        }

        if (!App_ResistorBypassMotorActivationAllowed(status.mode))
        {
            App_RuntimeSendError("STATE");
            return;
        }
    }

    App_MotorEnqueueBypass(enabled)
        ? App_RuntimeSendOk()
        : App_RuntimeSendError("MOTOR_QUEUE");
}

static void App_AtHandleCommand(const AppAtCommand *command)
{
    bool queued = false;
    bool thermal_protection_active = false;
    bool thermal_state_available = false;
    AppAtCommandGuardResult guard_result;

    if (command == NULL)
    {
        App_RuntimeSendError("BAD_COMMAND");
        return;
    }

    guard_result = App_AtCommandGuardEvaluate(command, false, false);
    if (guard_result == APP_AT_COMMAND_GUARD_STATE_BUSY)
    {
        thermal_state_available = App_StateTryGetThermalProtectionActive(
            &thermal_protection_active);
        guard_result = App_AtCommandGuardEvaluate(command,
                                                   thermal_state_available,
                                                   thermal_protection_active);
    }
    if (guard_result == APP_AT_COMMAND_GUARD_STATE_BUSY)
    {
        App_RuntimeSendError("STATE_BUSY");
        return;
    }
    if (guard_result == APP_AT_COMMAND_GUARD_OVER_TEMPERATURE)
    {
        App_RuntimeSendError("OVER_TEMPERATURE");
        return;
    }

    switch (command->type)
    {
    case APP_AT_COMMAND_SET_BRIDGE:
        App_StateSetBridgeEnabled(command->data.bridge.target, command->data.bridge.enabled);
        if (!command->data.bridge.enabled)
        {
            if ((command->data.bridge.target == APP_BRIDGE_TARGET_UART2)
                || (command->data.bridge.target == APP_BRIDGE_TARGET_UART23))
            {
                App_RuntimeFlushBridgeRx(2U);
            }
            if ((command->data.bridge.target == APP_BRIDGE_TARGET_UART3)
                || (command->data.bridge.target == APP_BRIDGE_TARGET_UART23))
            {
                App_RuntimeFlushBridgeRx(3U);
            }
        }
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
    case APP_AT_COMMAND_SET_MOTOR_BYPASS:
        App_AtHandleMotorBypass(command->data.output.enabled);
        break;
    case APP_AT_COMMAND_SET_NMOS1:
        queued = App_OutputEnqueueState(APP_OUTPUT_TARGET_NMOS1, command->data.output.enabled);
        queued ? App_RuntimeSendOk() : App_RuntimeSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_SET_NMOS2:
        queued = App_OutputEnqueueState(APP_OUTPUT_TARGET_NMOS2, command->data.output.enabled);
        queued ? App_RuntimeSendOk() : App_RuntimeSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_SET_POWER_MODE:
        queued = App_OutputEnqueuePowerMode(command->data.power.mode);
        queued ? App_RuntimeSendOk() : App_RuntimeSendError("OUTPUT_QUEUE");
        break;
    case APP_AT_COMMAND_SET_CHARGE_TIME:
        App_StateSetChargeOnTimeSeconds(command->data.charge_time.seconds)
            ? App_RuntimeSendOk()
            : App_RuntimeSendError("STATE_BUSY");
        break;
    case APP_AT_COMMAND_SET_STALL_CURRENT:
    {
        AppStallConfigSetResult result =
            App_StallConfigServiceSetCurrentMa(
                command->data.stall_current.current_ma);

        switch (result)
        {
        case APP_STALL_CONFIG_SET_OK:
            App_RuntimeSendOk();
            break;
        case APP_STALL_CONFIG_SET_STATE_BUSY:
            App_RuntimeSendError("STATE_BUSY");
            break;
        case APP_STALL_CONFIG_SET_MOTOR_RUNNING:
            App_RuntimeSendError("MOTOR_RUNNING");
            break;
        case APP_STALL_CONFIG_SET_FLASH_WRITE_FAILED:
            App_RuntimeSendError("FLASH_WRITE");
            break;
        default:
            App_RuntimeSendError("STALL_CONFIG");
            break;
        }
        break;
    }
    case APP_AT_COMMAND_SEND_UART:
        App_AtSendUartPayload(&command->data.uart_payload);
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
    case APP_AT_COMMAND_QUERY_VERSION:
        App_AtReplyVersion();
        break;
    case APP_AT_COMMAND_QUERY_CHARGE_TIME:
        App_AtReplyChargeTime();
        break;
    case APP_AT_COMMAND_QUERY_STALL_CURRENT:
        App_AtReplyStallCurrent();
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

    if (strncmp(line, "AT+UARTTX=", 10U) == 0)
    {
        if (AppAtProtocol_Parse(line, &command))
        {
            App_AtHandleCommand(&command);
        }
        else
        {
            App_RuntimeSendError("HEX");
        }
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

        App_RuntimeNoteAtLoop();

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
