#ifndef APP_SENSOR_THERMAL_H
#define APP_SENSOR_THERMAL_H

#include <stdbool.h>
#include <stdint.h>

#include "app_thermal_guard.h"

typedef struct
{
    bool battery_ntc_valid;
    bool battery_voltage_valid;
    bool ntc1_valid;
    bool ntc2_valid;
    bool ntc3_valid;
    int32_t ntc1_temperature_deci_c;
    int32_t ntc2_temperature_deci_c;
    int32_t ntc3_temperature_deci_c;
} AppSensorThermalInputs;

AppThermalTransition App_SensorThermalGuardUpdate(
    AppThermalGuard *guard,
    const AppSensorThermalInputs *inputs);

#endif
