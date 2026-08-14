#ifndef APP_THERMAL_GUARD_H
#define APP_THERMAL_GUARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_types.h"

typedef enum
{
    APP_THERMAL_NO_CHANGE = 0,
    APP_THERMAL_TRIPPED,
    APP_THERMAL_CLEARED
} AppThermalTransition;

typedef struct
{
    int32_t trip_temperature_deci_c;
    int32_t clear_temperature_deci_c;
    bool active;
} AppThermalGuard;

void App_ThermalGuardInit(AppThermalGuard *guard,
                          int32_t trip_temperature_deci_c,
                          int32_t clear_temperature_deci_c);
AppThermalTransition App_ThermalGuardUpdate(AppThermalGuard *guard,
                                            const int32_t *temperatures_deci_c,
                                            size_t temperature_count);

bool App_ThermalAllowsPowerMode(bool thermal_active, AppPowerMode mode);
bool App_ThermalAllowsOutputState(bool thermal_active, bool enabled);
bool App_ThermalAllowsMotorMode(bool thermal_active, AppMotorMode mode);

#endif
