#ifndef APP_SENSOR_THERMAL_H
#define APP_SENSOR_THERMAL_H

#include <stdbool.h>
#include <stdint.h>

#include "app_thermal_guard.h"

typedef struct
{
    bool battery_ntc_valid;
    bool battery_voltage_valid;
    bool mcu_valid;
    bool lm51770_valid;
    bool mp4317_valid;
    bool drv8874_valid;
    bool charge_mos_valid;
    int32_t mcu_temperature_deci_c;
    int32_t lm51770_temperature_deci_c;
    int32_t mp4317_temperature_deci_c;
    int32_t drv8874_temperature_deci_c;
    int32_t charge_mos_temperature_deci_c;
} AppSensorThermalInputs;

AppThermalTransition App_SensorThermalGuardUpdate(
    AppThermalGuard *guard,
    const AppSensorThermalInputs *inputs);

#endif
