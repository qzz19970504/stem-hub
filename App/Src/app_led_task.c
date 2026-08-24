#include "app_led.h"

#include "app_runtime.h"
#include "app_power_path.h"
#include "app_state.h"

#define APP_LED_STARTUP_TOTAL_MS 5000U
#define APP_LED_STARTUP_PHASE_MS 1000U

bool App_LedEnqueueState(bool enabled)
{
    AppLedRequest request = {.enabled = enabled};

    return osMessageQueuePut(g_app_runtime.led_queue, &request, 0U, 0U) == osOK;
}

/* 开机自检序列：LED1 (PC15) 与 LED3 (PA15) 交替点亮各 1 秒，总长 5 秒。
 * 电路改版后 LED2 (PA8) 已移除，原 LED2/LED3 映射改为 LED1/LED3。*/
static void App_LedRunStartupSequence(void)
{
    GPIO_PinState led1_state = GPIO_PIN_SET;
    GPIO_PinState led3_state = GPIO_PIN_RESET;

    for (uint32_t phase = 0U; phase < (APP_LED_STARTUP_TOTAL_MS / APP_LED_STARTUP_PHASE_MS); phase++)
    {
        HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, led1_state);
        HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, led3_state);

        osDelay(APP_LED_STARTUP_PHASE_MS);

        GPIO_PinState swap = led1_state;
        led1_state = led3_state;
        led3_state = swap;
    }

    HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED3_GPIO_Port, LED3_Pin, GPIO_PIN_RESET);
}

void App_LedTask(void *argument)
{
    AppLedRequest request;
    AppMotorMode mode = APP_MOTOR_MODE_SLEEP;
    bool led_master_enabled = false;

    (void)argument;

    App_LedRunStartupSequence();

    for (;;)
    {
        if (osMessageQueueGet(g_app_runtime.led_queue, &request, NULL, 100U) == osOK)
        {
            AppIoStatus io_status;
            bool activation_allowed = !request.enabled
                || (App_StateTryGetIoStatus(&io_status)
                    && App_PowerPathAllowsAuxiliaryOutput(io_status.power_mode));
            if (activation_allowed)
            {
                App_StateSetLedMasterEnabled(request.enabled);
            }
        }

        (void)App_StateTryGetLedAndMotor(&led_master_enabled, &mode);

        HAL_GPIO_WritePin(LED1_GPIO_Port,
                          LED1_Pin,
                          (led_master_enabled && (mode == APP_MOTOR_MODE_FORWARD)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        HAL_GPIO_WritePin(LED3_GPIO_Port,
                          LED3_Pin,
                          (led_master_enabled && (mode == APP_MOTOR_MODE_REVERSE)) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    }
}
