#include "app_motor.h"

#include "app_config.h"
#include "app_motor_current.h"
#include "app_motor_stall_guard.h"
#include "app_runtime.h"
#include "app_state.h"
#include "app_task_safety.h"

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
        App_MotorStoreStatus(APP_MOTOR_MODE_WAKE, 0U, overcurrent_latched);
        break;
    case APP_MOTOR_MODE_BRAKE:
    case APP_MOTOR_MODE_STOP:
        App_MotorSetOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET);
        App_MotorStoreStatus(mode, 0U, overcurrent_latched);
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
        App_MotorStoreStatus(mode, 0U, false);
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

    *current_ma = App_MotorCurrentFromMillivolts(
        App_RuntimeRawToMillivolts(raw));
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
    AppMotorRequest request = {
        .type = APP_MOTOR_REQUEST_SET_MODE,
        .data.mode = mode,
    };

    return osMessageQueuePut(g_app_runtime.motor_queue, &request, 0U, 0U) == osOK;
}

bool App_MotorEnqueueThermalSleep(void)
{
    AppMotorRequest request = {
        .type = APP_MOTOR_REQUEST_SET_MODE,
        .data.mode = APP_MOTOR_MODE_SLEEP,
    };

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
    AppMotorStallGuard stall_guard = {0};
    uint32_t current_ma = 0U;
    uint32_t stall_current_ma = 0U;
    bool thermal_sleep_applied = false;

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
                                          request.data.mode))
            {
                App_MotorApplyMode(request.data.mode);
                if ((request.data.mode == APP_MOTOR_MODE_FORWARD)
                    || (request.data.mode == APP_MOTOR_MODE_REVERSE))
                {
                    App_MotorStallGuardStart(&stall_guard, HAL_GetTick());
                }
                else
                {
                    App_MotorStallGuardStop(&stall_guard);
                }
            }
        }

        bool thermal_active = false;
        bool state_available =
            App_StateTryGetThermalProtectionActive(&thermal_active);
        if (App_TaskSafetyRequiresForcedSafe(state_available, thermal_active))
        {
            if (!thermal_sleep_applied)
            {
                App_MotorApplyMode(APP_MOTOR_MODE_SLEEP);
                App_MotorStallGuardStop(&stall_guard);
                thermal_sleep_applied = true;
            }
            continue;
        }
        thermal_sleep_applied = false;

        if (!App_MotorTryGetStatus(&snapshot))
        {
            continue;
        }

        if ((snapshot.mode != APP_MOTOR_MODE_FORWARD) && (snapshot.mode != APP_MOTOR_MODE_REVERSE))
        {
            App_MotorStallGuardStop(&stall_guard);
            continue;
        }

        bool current_valid = App_MotorReadCurrent(&current_ma);
        bool threshold_valid =
            App_StateTryGetStallCurrentMa(&stall_current_ma);

        if (!current_valid || !threshold_valid)
        {
            (void)App_MotorStallGuardUpdate(&stall_guard,
                                            HAL_GetTick(),
                                            false,
                                            0U,
                                            0U);
            continue;
        }

        if (App_MotorStallGuardUpdate(&stall_guard,
                                      HAL_GetTick(),
                                      true,
                                      current_ma,
                                      stall_current_ma))
        {
            App_MotorSetOutputs(GPIO_PIN_SET, GPIO_PIN_RESET, HAL_GPIO_ReadPin(PH_IN2_GPIO_Port, PH_IN2_Pin));
            App_MotorStallGuardStop(&stall_guard);
            App_MotorStoreStatus(APP_MOTOR_MODE_BRAKE, current_ma, true);
            continue;
        }

        App_MotorStoreStatus(snapshot.mode, current_ma, snapshot.overcurrent_latched);
    }
}
