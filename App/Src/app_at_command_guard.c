#include "app_at_command_guard.h"

#include "app_thermal_guard.h"

static bool App_AtCommandRequiresThermalPermission(const AppAtCommand *command)
{
    switch (command->type)
    {
    case APP_AT_COMMAND_SET_POWER_MODE:
        return !App_ThermalAllowsPowerMode(true, command->data.power.mode);
    case APP_AT_COMMAND_SET_NMOS1:
    case APP_AT_COMMAND_SET_NMOS2:
        return !App_ThermalAllowsOutputState(true, command->data.output.enabled);
    case APP_AT_COMMAND_SET_MOTOR_MODE:
        return !App_ThermalAllowsMotorMode(true, command->data.motor.mode);
    default:
        return false;
    }
}

AppAtCommandGuardResult App_AtCommandGuardEvaluate(
    const AppAtCommand *command,
    bool thermal_state_available,
    bool thermal_protection_active)
{
    if ((command == NULL) || !App_AtCommandRequiresThermalPermission(command))
    {
        return APP_AT_COMMAND_GUARD_ALLOW;
    }

    if (!thermal_state_available)
    {
        return APP_AT_COMMAND_GUARD_STATE_BUSY;
    }

    if (thermal_protection_active)
    {
        return APP_AT_COMMAND_GUARD_OVER_TEMPERATURE;
    }

    return APP_AT_COMMAND_GUARD_ALLOW;
}
