#include "app_output.h"

#include "app_runtime.h"
#include "app_state.h"

bool App_OutputEnqueueState(AppOutputTarget target, bool enabled)
{
    AppOutputRequest request = {
        .target = target,
        .enabled = enabled,
    };

    return osMessageQueuePut(g_app_runtime.output_queue, &request, 0U, 0U) == osOK;
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
            App_StateSetOutputEnabled(request.target, request.enabled);
            break;
        case APP_OUTPUT_TARGET_NMOS2:
            HAL_GPIO_WritePin(NMOS2_GPIO_Port, NMOS2_Pin, request.enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
            App_StateSetOutputEnabled(request.target, request.enabled);
            break;
        case APP_OUTPUT_TARGET_UVLO:
            HAL_GPIO_WritePin(EN_UVLO_GPIO_Port, EN_UVLO_Pin, request.enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
            App_StateSetOutputEnabled(request.target, request.enabled);
            break;
        default:
            break;
        }
    }
}