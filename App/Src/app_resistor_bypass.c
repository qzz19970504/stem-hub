#include "app_resistor_bypass.h"

bool App_ResistorBypassMotorActivationAllowed(AppMotorMode mode)
{
    return (mode == APP_MOTOR_MODE_FORWARD)
        || (mode == APP_MOTOR_MODE_REVERSE);
}

bool App_ResistorBypassChargeActivationAllowed(AppPowerMode power_mode)
{
    return power_mode == APP_POWER_MODE_CHARGE;
}

bool App_ResistorBypassRequestAllowed(bool requested_enabled,
                                      bool activation_allowed)
{
    return !requested_enabled || activation_allowed;
}

bool App_ResistorBypassMotorTransitionRequiresReset(
    AppMotorMode previous_mode,
    AppMotorMode requested_mode)
{
    if (!App_ResistorBypassMotorActivationAllowed(requested_mode))
    {
        return true;
    }

    return App_ResistorBypassMotorActivationAllowed(previous_mode)
        && (previous_mode != requested_mode);
}
