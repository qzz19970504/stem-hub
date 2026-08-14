#include "app_sensor_thermal.h"

#include <limits.h>

AppThermalTransition App_SensorThermalGuardUpdate(
    AppThermalGuard *guard,
    const AppSensorThermalInputs *inputs)
{
    int32_t ntc1 = INT32_MAX;
    int32_t ntc2 = INT32_MAX;
    int32_t ntc3 = INT32_MAX;

    if (inputs != NULL)
    {
        /* Battery channels are deliberately excluded from the protection
         * decision; their validity only affects SENSE snapshot publication. */
        if (inputs->ntc1_valid)
        {
            ntc1 = inputs->ntc1_temperature_deci_c;
        }
        if (inputs->ntc2_valid)
        {
            ntc2 = inputs->ntc2_temperature_deci_c;
        }
        if (inputs->ntc3_valid)
        {
            ntc3 = inputs->ntc3_temperature_deci_c;
        }
    }

    return App_ThermalGuardUpdate(guard, ntc1, ntc2, ntc3);
}
