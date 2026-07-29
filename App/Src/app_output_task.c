#include "app_output.h"

#include "app_power_path.h"
#include "app_runtime.h"
#include "app_state.h"

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

        if (request.type == APP_OUTPUT_REQUEST_SET_POWER_MODE)
        {
            (void)App_PowerPathApply(request.data.power_mode,
                                     App_OutputWritePowerPath,
                                     NULL);
            continue;
        }

        if ((request.type == APP_OUTPUT_REQUEST_SET_TARGET)
            && App_OutputTargetAllowsDirectControl(request.data.target.target))
        {
            App_OutputApplyTarget(request.data.target.target,
                                  request.data.target.enabled);
        }
    }
}
