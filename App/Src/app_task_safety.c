#include "app_task_safety.h"

void App_TaskSafetyHandleThermalTransition(
    AppThermalTransition transition,
    const AppTaskSafetyCallbacks *callbacks)
{
    if (callbacks == NULL)
    {
        return;
    }

    if (transition == APP_THERMAL_TRIPPED)
    {
        if (callbacks->set_thermal_state != NULL)
        {
            (void)callbacks->set_thermal_state(true, callbacks->context);
        }
        if (callbacks->request_output_stop != NULL)
        {
            callbacks->request_output_stop(callbacks->context);
        }
        if (callbacks->request_motor_sleep != NULL)
        {
            callbacks->request_motor_sleep(callbacks->context);
        }
    }
    else if (transition == APP_THERMAL_CLEARED)
    {
        if (callbacks->set_thermal_state != NULL)
        {
            (void)callbacks->set_thermal_state(false, callbacks->context);
        }
    }
}

bool App_TaskSafetyRequiresForcedSafe(bool state_available,
                                      bool thermal_active)
{
    return !state_available || thermal_active;
}

bool App_TaskSafetyAllowsPower(bool state_available,
                               bool thermal_active,
                               AppPowerMode mode)
{
    return App_ThermalAllowsPowerMode(
        App_TaskSafetyRequiresForcedSafe(state_available, thermal_active),
        mode);
}

bool App_TaskSafetyAllowsOutput(bool state_available,
                                bool thermal_active,
                                bool enabled)
{
    return App_ThermalAllowsOutputState(
        App_TaskSafetyRequiresForcedSafe(state_available, thermal_active),
        enabled);
}

bool App_TaskSafetyAllowsMotor(bool state_available,
                               bool thermal_active,
                               AppMotorMode mode)
{
    return App_ThermalAllowsMotorMode(
        App_TaskSafetyRequiresForcedSafe(state_available, thermal_active),
        mode);
}
