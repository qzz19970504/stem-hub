#include "app_thermal_guard.h"

#include <limits.h>

static bool IsInvalidTemperature(int32_t temperature_deci_c)
{
    return temperature_deci_c == INT32_MAX;
}

static bool ShouldTrip(const AppThermalGuard *guard,
                       int32_t temperature_deci_c)
{
    return IsInvalidTemperature(temperature_deci_c)
           || (temperature_deci_c > guard->trip_temperature_deci_c);
}

static bool CanClear(const AppThermalGuard *guard,
                     int32_t temperature_deci_c)
{
    return !IsInvalidTemperature(temperature_deci_c)
           && (temperature_deci_c <= guard->clear_temperature_deci_c);
}

void App_ThermalGuardInit(AppThermalGuard *guard,
                          int32_t trip_temperature_deci_c,
                          int32_t clear_temperature_deci_c)
{
    guard->trip_temperature_deci_c = trip_temperature_deci_c;
    guard->clear_temperature_deci_c = clear_temperature_deci_c;
    guard->active = false;
}

AppThermalTransition App_ThermalGuardUpdate(AppThermalGuard *guard,
                                            int32_t ntc1_temperature_deci_c,
                                            int32_t ntc2_temperature_deci_c,
                                            int32_t ntc3_temperature_deci_c)
{
    if (!guard->active)
    {
        if (ShouldTrip(guard, ntc1_temperature_deci_c)
            || ShouldTrip(guard, ntc2_temperature_deci_c)
            || ShouldTrip(guard, ntc3_temperature_deci_c))
        {
            guard->active = true;
            return APP_THERMAL_TRIPPED;
        }

        return APP_THERMAL_NO_CHANGE;
    }

    if (CanClear(guard, ntc1_temperature_deci_c)
        && CanClear(guard, ntc2_temperature_deci_c)
        && CanClear(guard, ntc3_temperature_deci_c))
    {
        guard->active = false;
        return APP_THERMAL_CLEARED;
    }

    return APP_THERMAL_NO_CHANGE;
}

bool App_ThermalAllowsPowerMode(bool thermal_active, AppPowerMode mode)
{
    return !thermal_active || (mode == APP_POWER_MODE_OFF);
}

bool App_ThermalAllowsOutputState(bool thermal_active, bool enabled)
{
    return !thermal_active || !enabled;
}

bool App_ThermalAllowsMotorMode(bool thermal_active, AppMotorMode mode)
{
    return !thermal_active || (mode == APP_MOTOR_MODE_SLEEP);
}
