#include "app_output.h"

#include "app_charge_cycle.h"
#include "app_config.h"
#include "app_power_path.h"
#include "app_runtime.h"
#include "app_state.h"
#include "app_task_safety.h"

bool App_OutputEnqueueState(AppOutputTarget target, bool enabled)
{
    if (!App_OutputTargetAllowsDirectControl(target))
    {
        return false;
    }

    AppOutputRequest request = {
        .type = APP_OUTPUT_REQUEST_SET_TARGET,
        .data.target = {
            .target = target,
            .enabled = enabled,
        },
    };

    return osMessageQueuePut(g_app_runtime.output_queue, &request, 0U, 0U) == osOK;
}

bool App_OutputEnqueuePowerMode(AppPowerMode mode)
{
    AppOutputRequest request = {
        .type = APP_OUTPUT_REQUEST_SET_POWER_MODE,
        .data.power_mode = mode,
    };

    return osMessageQueuePut(g_app_runtime.output_queue, &request, 0U, 0U) == osOK;
}

bool App_OutputEnqueueThermalStop(void)
{
    AppOutputRequest request = {.type = APP_OUTPUT_REQUEST_THERMAL_STOP};

    return osMessageQueuePut(g_app_runtime.output_queue,
                             &request,
                             1U,
                             osWaitForever) == osOK;
}

static void App_OutputApplyTarget(AppOutputTarget target, bool enabled)
{
    switch (target)
    {
    case APP_OUTPUT_TARGET_NMOS1:
        HAL_GPIO_WritePin(NMOS1_GPIO_Port, NMOS1_Pin, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
        App_StateSetOutputEnabled(target, enabled);
        break;
    case APP_OUTPUT_TARGET_NMOS2:
        HAL_GPIO_WritePin(NMOS2_GPIO_Port, NMOS2_Pin, enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
        App_StateSetOutputEnabled(target, enabled);
        break;
    case APP_OUTPUT_TARGET_UVLO:
        HAL_GPIO_WritePin(EN_UVLO_GPIO_Port, EN_UVLO_Pin, enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
        App_StateSetOutputEnabled(target, enabled);
        break;
    case APP_OUTPUT_TARGET_MP4317:
        HAL_GPIO_WritePin(MP4317_GPIO_Port, MP4317_Pin, enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
        App_StateSetOutputEnabled(target, enabled);
        break;
    default:
        break;
    }
}

static void App_OutputWritePowerPath(AppOutputTarget target,
                                     bool enabled,
                                     void *context)
{
    (void)context;
    App_OutputApplyTarget(target, enabled);
}

static bool App_OutputPowerRequestAllowed(AppPowerMode mode);

static void App_OutputApplyPowerAction(AppChargeCycleAction action)
{
    if (action.apply_mode && App_OutputPowerRequestAllowed(action.mode))
    {
        (void)App_PowerPathApply(action.mode,
                                 App_OutputWritePowerPath,
                                 NULL);
    }
}

static void App_OutputShutDownAuxiliaryOutputs(void)
{
    App_OutputApplyTarget(APP_OUTPUT_TARGET_NMOS1, false);
    App_OutputApplyTarget(APP_OUTPUT_TARGET_NMOS2, false);
}

static bool App_OutputPowerRequestAllowed(AppPowerMode mode)
{
    bool thermal_active = false;
    bool state_available = App_StateTryGetThermalProtectionActive(&thermal_active);
    return App_TaskSafetyAllowsPower(state_available, thermal_active, mode);
}

static bool App_OutputStateRequestAllowed(bool enabled)
{
    bool thermal_active = false;
    bool state_available = App_StateTryGetThermalProtectionActive(&thermal_active);
    return App_TaskSafetyAllowsOutput(state_available, thermal_active, enabled);
}

static void App_OutputRefreshChargeConfiguration(AppChargeCycle *charge_cycle,
                                                 uint32_t tick_frequency)
{
    uint32_t seconds;

    if (App_StateTryGetChargeOnTimeSeconds(&seconds))
    {
        uint32_t on_ticks = App_ChargeCycleMillisecondsToTicks(
            seconds * 1000U,
            tick_frequency);
        (void)App_ChargeCycleConfigureOnTicks(charge_cycle, on_ticks);
    }
}

void App_NmosTask(void *argument)
{
    AppOutputRequest request;
    AppChargeCycle charge_cycle;
    AppChargeCycleAction action;
    uint32_t now_tick;
    uint32_t wait_ticks;
    osStatus_t queue_status;
    uint32_t tick_frequency = osKernelGetTickFreq();
    uint32_t charge_on_seconds = APP_CHARGE_DEFAULT_ON_TIME_SECONDS;
    uint32_t charge_cycle_ticks = App_ChargeCycleMillisecondsToTicks(
        APP_CHARGE_CYCLE_TIME_SECONDS * 1000U,
        tick_frequency);
    uint32_t charge_on_ticks;

    (void)argument;

    (void)App_StateTryGetChargeOnTimeSeconds(&charge_on_seconds);
    charge_on_ticks = App_ChargeCycleMillisecondsToTicks(
        charge_on_seconds * 1000U,
        tick_frequency);

    if (!App_ChargeCycleInit(&charge_cycle,
                             charge_cycle_ticks,
                             charge_on_ticks))
    {
        (void)App_PowerPathApply(APP_POWER_MODE_OFF,
                                 App_OutputWritePowerPath,
                                 NULL);
        Error_Handler();
    }

    for (;;)
    {
        App_OutputRefreshChargeConfiguration(&charge_cycle, tick_frequency);
        now_tick = osKernelGetTickCount();
        action = App_ChargeCyclePoll(&charge_cycle, now_tick);
        App_OutputApplyPowerAction(action);

        now_tick = osKernelGetTickCount();
        wait_ticks = App_ChargeCycleWaitTicks(&charge_cycle, now_tick);
        queue_status = osMessageQueueGet(g_app_runtime.output_queue,
                                         &request,
                                         NULL,
                                         wait_ticks);

        if (queue_status == osErrorTimeout)
        {
            continue;
        }

        if (queue_status != osOK)
        {
            continue;
        }

        App_OutputRefreshChargeConfiguration(&charge_cycle, tick_frequency);

        if (request.type == APP_OUTPUT_REQUEST_THERMAL_STOP)
        {
            action = App_ChargeCycleRequest(&charge_cycle,
                                            APP_POWER_MODE_OFF,
                                            osKernelGetTickCount());
            App_OutputApplyPowerAction(action);
            App_OutputShutDownAuxiliaryOutputs();
            continue;
        }

        if (request.type == APP_OUTPUT_REQUEST_SET_POWER_MODE)
        {
            if (!App_OutputPowerRequestAllowed(request.data.power_mode))
            {
                continue;
            }
            action = App_ChargeCycleRequest(&charge_cycle,
                                            request.data.power_mode,
                                            osKernelGetTickCount());
            App_OutputApplyPowerAction(action);
            if (request.data.power_mode == APP_POWER_MODE_OFF)
            {
                App_OutputShutDownAuxiliaryOutputs();
            }
            continue;
        }

        if ((request.type == APP_OUTPUT_REQUEST_SET_TARGET)
            && App_OutputTargetAllowsDirectControl(request.data.target.target)
            && App_OutputStateRequestAllowed(request.data.target.enabled))
        {
            App_OutputApplyTarget(request.data.target.target,
                                  request.data.target.enabled);
        }
    }
}
