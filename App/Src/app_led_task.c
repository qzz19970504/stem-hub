#include "app_led.h"

#include "app_runtime.h"
#include "app_state.h"

bool App_LedEnqueueState(bool enabled)
{
    AppLedRequest request = {.enabled = enabled};

    return osMessageQueuePut(g_app_runtime.led_queue, &request, 0U, 0U) == osOK;
}

void App_LedTask(void *argument)
{
    AppLedRequest request;
    AppMotorMode mode = APP_MOTOR_MODE_SLEEP;
    bool led_master_enabled = true;

    (void)argument;

    for (;;)
    {
        if (osMessageQueueGet(g_app_runtime.led_queue, &request, NULL, 100U) == osOK)
        {
            App_StateSetLedMasterEnabled(request.enabled);
        }

        (void)App_StateTryGetLedAndMotor(&led_master_enabled, &mode);

        HAL_GPIO_WritePin(LED1_GPIO_Port,
                  LED1_Pin,
                  led_master_enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);

        HAL_GPIO_WritePin(LED2_GPIO_Port,
                          LED2_Pin,
                          (led_master_enabled && (mode == APP_MOTOR_MODE_FORWARD)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED3_GPIO_Port,
                          LED3_Pin,
                          (led_master_enabled && (mode == APP_MOTOR_MODE_REVERSE)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}