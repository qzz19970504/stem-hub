#ifndef APP_RESISTOR_BYPASS_H
#define APP_RESISTOR_BYPASS_H

#include <stdbool.h>

#include "app_at_protocol.h"

bool App_ResistorBypassMotorActivationAllowed(AppMotorMode mode);
bool App_ResistorBypassChargeActivationAllowed(AppPowerMode power_mode);
bool App_ResistorBypassRequestAllowed(bool requested_enabled,
                                      bool activation_allowed);
bool App_ResistorBypassMotorTransitionRequiresReset(
    AppMotorMode previous_mode,
    AppMotorMode requested_mode);

#endif
