#include <assert.h>
#include <limits.h>

#include "app_sensor_thermal.h"

static AppSensorThermalInputs NormalInputs(void)
{
    AppSensorThermalInputs inputs = {
        .battery_ntc_valid = true,
        .battery_voltage_valid = true,
        .ntc1_valid = true,
        .ntc2_valid = true,
        .ntc3_valid = true,
        .ntc1_temperature_deci_c = 400,
        .ntc2_temperature_deci_c = 400,
        .ntc3_temperature_deci_c = 400,
    };
    return inputs;
}

int main(void)
{
    AppThermalGuard guard;
    AppSensorThermalInputs inputs = NormalInputs();

    App_ThermalGuardInit(&guard, 600, 550);
    inputs.ntc1_valid = false;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);

    inputs = NormalInputs();
    inputs.ntc2_valid = false;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);

    App_ThermalGuardInit(&guard, 600, 550);
    inputs = NormalInputs();
    inputs.battery_ntc_valid = false;
    inputs.battery_voltage_valid = false;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_NO_CHANGE);
    assert(!guard.active);

    inputs.ntc3_valid = false;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);

    inputs = NormalInputs();
    inputs.ntc1_temperature_deci_c = INT32_MAX;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);
    return 0;
}
