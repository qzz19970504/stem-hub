#include <assert.h>
#include <limits.h>

#include "app_sensor_thermal.h"

static AppSensorThermalInputs NormalInputs(void)
{
    AppSensorThermalInputs inputs = {
        .battery_ntc_valid = true,
        .battery_voltage_valid = true,
        .mcu_valid = true,
        .lm51770_valid = true,
        .mp4317_valid = true,
        .drv8874_valid = true,
        .charge_mos_valid = true,
        .mcu_temperature_deci_c = 400,
        .lm51770_temperature_deci_c = 400,
        .mp4317_temperature_deci_c = 400,
        .drv8874_temperature_deci_c = 400,
        .charge_mos_temperature_deci_c = 400,
    };
    return inputs;
}

static void TestInvalidDrv8874Trips(void)
{
    AppThermalGuard guard;
    AppSensorThermalInputs inputs = NormalInputs();

    App_ThermalGuardInit(&guard, 600, 550);
    inputs.drv8874_valid = false;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);
}

static void TestInvalidChargeMosTrips(void)
{
    AppThermalGuard guard;
    AppSensorThermalInputs inputs = NormalInputs();

    App_ThermalGuardInit(&guard, 600, 550);
    inputs.charge_mos_valid = false;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);
}

static void TestBatteryChannelsAreExcluded(void)
{
    AppThermalGuard guard;
    AppSensorThermalInputs inputs = NormalInputs();

    App_ThermalGuardInit(&guard, 600, 550);
    inputs.battery_ntc_valid = false;
    inputs.battery_voltage_valid = false;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_NO_CHANGE);
    assert(!guard.active);
}

static void TestInvalidSemanticTemperatureDoesNotClear(void)
{
    AppThermalGuard guard;
    AppSensorThermalInputs inputs = NormalInputs();

    App_ThermalGuardInit(&guard, 600, 550);
    inputs.mcu_temperature_deci_c = INT32_MAX;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);

    inputs = NormalInputs();
    inputs.mp4317_valid = false;
    assert(App_SensorThermalGuardUpdate(&guard, &inputs)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);
}

int main(void)
{
    TestInvalidDrv8874Trips();
    TestInvalidChargeMosTrips();
    TestBatteryChannelsAreExcluded();
    TestInvalidSemanticTemperatureDoesNotClear();
    return 0;
}
