#include "app_sensor_thermal.h"

#include <limits.h>

#define PROTECTED_TEMPERATURE_COUNT 5U

AppThermalTransition App_SensorThermalGuardUpdate(
    AppThermalGuard *guard,
    const AppSensorThermalInputs *inputs)
{
    int32_t temperatures_deci_c[PROTECTED_TEMPERATURE_COUNT] = {
        INT32_MAX,
        INT32_MAX,
        INT32_MAX,
        INT32_MAX,
        INT32_MAX,
    };

    if (inputs != NULL)
    {
        /* Battery channels are deliberately excluded from the protection
         * decision; their validity only affects SENSE snapshot publication. */
        if (inputs->mcu_valid)
        {
            temperatures_deci_c[0] = inputs->mcu_temperature_deci_c;
        }
        if (inputs->lm51770_valid)
        {
            temperatures_deci_c[1] = inputs->lm51770_temperature_deci_c;
        }
        if (inputs->mp4317_valid)
        {
            temperatures_deci_c[2] = inputs->mp4317_temperature_deci_c;
        }
        if (inputs->drv8874_valid)
        {
            temperatures_deci_c[3] = inputs->drv8874_temperature_deci_c;
        }
        if (inputs->charge_mos_valid)
        {
            temperatures_deci_c[4] = inputs->charge_mos_temperature_deci_c;
        }
    }

    return App_ThermalGuardUpdate(guard,
                                  temperatures_deci_c,
                                  PROTECTED_TEMPERATURE_COUNT);
}
