#include "app_motor.h"

#include <stddef.h>

#include "app_config.h"

bool App_MotorGetStartupOverrideMode(AppMotorMode *mode)
{
    if (mode == NULL)
    {
        return false;
    }

    if (APP_MOTOR_TEST_MODE != APP_MOTOR_TEST_MODE_STARTUP_OVERRIDE)
    {
        return false;
    }

    switch (APP_MOTOR_STARTUP_OVERRIDE_MODE)
    {
    case APP_MOTOR_STARTUP_OVERRIDE_DISABLED:
        return false;
    case APP_MOTOR_STARTUP_OVERRIDE_SLEEP:
        *mode = APP_MOTOR_MODE_SLEEP;
        return true;
    case APP_MOTOR_STARTUP_OVERRIDE_WAKE:
        *mode = APP_MOTOR_MODE_WAKE;
        return true;
    case APP_MOTOR_STARTUP_OVERRIDE_FORWARD:
        *mode = APP_MOTOR_MODE_FORWARD;
        return true;
    case APP_MOTOR_STARTUP_OVERRIDE_REVERSE:
        *mode = APP_MOTOR_MODE_REVERSE;
        return true;
    case APP_MOTOR_STARTUP_OVERRIDE_BRAKE:
        *mode = APP_MOTOR_MODE_BRAKE;
        return true;
    default:
        return false;
    }
}

bool App_MotorIsAutoSequenceEnabled(void)
{
    return (APP_MOTOR_TEST_MODE == APP_MOTOR_TEST_MODE_AUTO_SEQUENCE);
}

bool App_MotorAllowsExternalControl(void)
{
    return !App_MotorIsAutoSequenceEnabled();
}

uint32_t App_MotorGetAutoSequenceStepDurationMs(void)
{
    return APP_MOTOR_AUTO_SEQUENCE_STEP_DURATION_MS;
}

bool App_MotorGetAutoSequenceModeForStep(uint32_t step_index, AppMotorMode *mode)
{
    if (mode == NULL)
    {
        return false;
    }

    switch (step_index % 5U)
    {
    case 0U:
        *mode = APP_MOTOR_MODE_WAKE;
        return true;
    case 1U:
        *mode = APP_MOTOR_MODE_FORWARD;
        return true;
    case 2U:
        *mode = APP_MOTOR_MODE_BRAKE;
        return true;
    case 3U:
        *mode = APP_MOTOR_MODE_REVERSE;
        return true;
    case 4U:
        *mode = APP_MOTOR_MODE_SLEEP;
        return true;
    default:
        return false;
    }
}