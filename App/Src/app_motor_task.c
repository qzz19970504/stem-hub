#include "app_motor.h"

#include "app_config.h"
#include "app_runtime.h"
#include "app_state.h"
#include "app_task_safety.h"

static uint32_t App_MotorConvertCurrent(uint32_t millivolts)
{
    /* IPROPI 镜像电流公式 (DRV8874 SLVSF66A §6.5, R19 = IPROPI 下拉):
     *   I_LOAD (A) = V_IPROPI_V / (AIPROPI × R19)
     *   I_LOAD (mA) = V_IPROPI_mV × 1000 / (AIPROPI × R19) × 1000 / 1000
     *               = V_IPROPI_mV × 1000 / (AIPROPI × R19)
     *
     * 但 AIPROPI × R19 = 450e-6 × 2500 = 1.125 (单位 V/A)，所以写成
     *   I_LOAD (mA) = V_IPROPI_mV × 1000 / 1.125
     *               = V_IPROPI_mV × 1000 / 1125
     *
     * 整数实现：denom = (AIPROPI × R19) / 1000，单位变成 mV/mA = 1125，
     * 然后 numerator = mV × 1000 / denom。
     * 校核：V_mV=1125 ⇒ I=1000 mA；V_mV=2250 ⇒ I=2000 mA。
     *
     * ADC 物理上限 ≈ 2.93 A (Vref=3.3V 饱和)，current_ma 用 uint32_t 不会溢出，
     * 但仍保留饱和钳用于防御。*/
    uint64_t denom_mv_per_ma =
        ((uint64_t)APP_MOTOR_IPROPI_AIPROPI_UA_PER_A *
         (uint64_t)APP_MOTOR_IPROPI_R19_OHMS) / 1000ULL; /* = 1125 */
    uint64_t mA = ((uint64_t)millivolts * 1000ULL) / denom_mv_per_ma;
    return (mA > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)mA;
}

static void App_MotorSetOutputs(GPIO_PinState n_sleep,
                                GPIO_PinState enable,
                                GPIO_PinState phase)
{
    HAL_GPIO_WritePin(nSLEEP_GPIO_Port, nSLEEP_Pin, n_sleep);
    HAL_GPIO_WritePin(EN_IN1_GPIO_Port, EN_IN1_Pin, enable);
    HAL_GPIO_WritePin(PH_IN2_GPIO_Port, PH_IN2_Pin, phase);
}

static void App_MotorStoreStatus(AppMotorMode mode,
                                 uint32_t current_ma,
                                 bool overcurrent_latched)
{
    App_StateStoreMotorStatus(mode,
                              current_ma,
                              overcurrent_latched,
                              (HAL_GPIO_ReadPin(nFAULT_GPIO_Port, nFAULT_Pin) == GPIO_PIN_RESET));
}

static void App_MotorApplyMode(AppMotorMode mode)
{
    AppMotorMode previous_mode = APP_MOTOR_MODE_SLEEP;
    bool overcurrent_latched = false;

    AppMotorStatus status;

    if (App_StateTryGetMotorStatus(&status))
    {
        previous_mode = status.mode;
        overcurrent_latched = status.overcurrent_latched;
    }

    switch (mode)
    {
    case APP_MOTOR_MODE_SLEEP:
        App_MotorSetOutputs(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET);
        App_MotorStoreStatus(APP_MOTOR_MODE_SLEEP, 0U, overcurrent_latched);
        break;
    case APP_MOTOR_MODE_WAKE:
        App_MotorSetOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET);
        osDelay(APP_MOTOR_WAKE_DELAY_MS);
        App_MotorStoreStatus(APP_MOTOR_MODE_WAKE, 0U, false);
        break;
    case APP_MOTOR_MODE_BRAKE:
    case APP_MOTOR_MODE_STOP:
        App_MotorSetOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET);
        App_MotorStoreStatus(mode, 0U, false);
        break;
    case APP_MOTOR_MODE_FORWARD:
    case APP_MOTOR_MODE_REVERSE:
        if (previous_mode == APP_MOTOR_MODE_SLEEP)
        {
            App_MotorSetOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET);
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
        App_MotorStoreStatus(mode, 0U, overcurrent_latched);
        break;
    default:
        break;
    }
}

static bool App_MotorReadCurrent(uint32_t *current_ma)
{
    uint16_t raw = 0U;

    if ((current_ma == NULL) || !App_RuntimeReadAdc2Channel(ADC_CHANNEL_8, &raw))
    {
        return false;
    }

    *current_ma = App_MotorConvertCurrent(App_RuntimeRawToMillivolts(raw));
    return true;
}

const char *App_MotorModeToString(AppMotorMode mode)
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

bool App_MotorEnqueueMode(AppMotorMode mode)
{
    AppMotorRequest request = {.mode = mode};

    return osMessageQueuePut(g_app_runtime.motor_queue, &request, 0U, 0U) == osOK;
}

bool App_MotorEnqueueThermalSleep(void)
{
    AppMotorRequest request = {.mode = APP_MOTOR_MODE_SLEEP};

    return osMessageQueuePut(g_app_runtime.motor_queue,
                             &request,
                             1U,
                             osWaitForever) == osOK;
}

bool App_MotorTryGetStatus(AppMotorStatus *status)
{
    return App_StateTryGetMotorStatus(status);
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
            bool thermal_active = false;
            bool state_available =
                App_StateTryGetThermalProtectionActive(&thermal_active);
            if (App_TaskSafetyAllowsMotor(state_available,
                                          thermal_active,
                                          request.mode))
            {
                App_MotorApplyMode(request.mode);
            }
        }

        bool thermal_active = false;
        if (!App_StateTryGetThermalProtectionActive(&thermal_active)
            || thermal_active)
        {
            continue;
        }

        if (!App_MotorTryGetStatus(&snapshot))
        {
            continue;
        }

        if ((snapshot.mode != APP_MOTOR_MODE_FORWARD) && (snapshot.mode != APP_MOTOR_MODE_REVERSE))
        {
            continue;
        }

        if (!App_MotorReadCurrent(&current_ma))
        {
            continue;
        }

        if (current_ma >= APP_MOTOR_OVERCURRENT_THRESHOLD_MA)
        {
            App_MotorSetOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, HAL_GPIO_ReadPin(PH_IN2_GPIO_Port, PH_IN2_Pin));
            App_MotorStoreStatus(APP_MOTOR_MODE_BRAKE, current_ma, true);
            continue;
        }

        App_MotorStoreStatus(snapshot.mode, current_ma, snapshot.overcurrent_latched);
    }
}
