#include "app_resistor_bypass.h"

bool App_ResistorBypassMotorActivationAllowed(AppMotorMode mode)
{
    return (mode == APP_MOTOR_MODE_FORWARD)
        || (mode == APP_MOTOR_MODE_REVERSE);
}

bool App_ResistorBypassChargeActivationAllowed(bool charge_output_enabled)
{
    return charge_output_enabled;
}

bool App_ResistorBypassRequestAllowed(bool requested_enabled,
                                      bool activation_allowed)
{
    return !requested_enabled || activation_allowed;
}
