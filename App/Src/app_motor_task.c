#include "app_motor.h"

#include "app_config.h"
#include "app_runtime.h"
#include "app_state.h"

static uint32_t App_MotorConvertCurrent(uint32_t millivolts)
{
    return millivolts;
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
            App_MotorApplyMode(request.mode);
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