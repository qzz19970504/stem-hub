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
            /* LM51770 是低电平使能：enabled=true → PB3 拉低（开），enabled=false → PB3 拉高（关）。*/
            HAL_GPIO_WritePin(EN_UVLO_GPIO_Port, EN_UVLO_Pin, request.enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
            App_StateSetOutputEnabled(request.target, request.enabled);
            break;
        case APP_OUTPUT_TARGET_MP4317:
            /* MP4317 是低电平使能：enabled=true → PA8 拉低（开），enabled=false → PA8 拉高（关）。*/
            HAL_GPIO_WritePin(MP4317_GPIO_Port, MP4317_Pin, request.enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
            App_StateSetOutputEnabled(request.target, request.enabled);
            break;
        default:
            break;
        }
    }
}