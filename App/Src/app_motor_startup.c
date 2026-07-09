#include "app_motor.h"

#include <stddef.h>

#include "app_config.h"

bool App_MotorGetStartupOverrideMode(AppMotorMode *mode)
{
    if (mode == NULL)
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