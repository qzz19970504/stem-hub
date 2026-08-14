#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_thermal_guard.h"

static void TestTripAndClearThresholds(void)
{
    AppThermalGuard guard;

    App_ThermalGuardInit(&guard, 600, 550);
    assert(!guard.active);
    assert(App_ThermalGuardUpdate(&guard, 600, 400, 400)
           == APP_THERMAL_NO_CHANGE);
    assert(!guard.active);

    assert(App_ThermalGuardUpdate(&guard, 601, 400, 400)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);
    assert(App_ThermalGuardUpdate(&guard, 550, 551, 550)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);
    assert(App_ThermalGuardUpdate(&guard, 550, 550, 550)
           == APP_THERMAL_CLEARED);
    assert(!guard.active);
}

static void TestInvalidTemperatureTripsAndDoesNotClear(void)
{
    AppThermalGuard guard;

    App_ThermalGuardInit(&guard, 600, 550);
    assert(App_ThermalGuardUpdate(&guard, 400, INT32_MAX, 400)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);
    assert(App_ThermalGuardUpdate(&guard, 400, 400, INT32_MAX)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);
    assert(App_ThermalGuardUpdate(&guard, 550, 550, 550)
           == APP_THERMAL_CLEARED);
}

static void TestSafetyPolicies(void)
{
    assert(!App_ThermalAllowsPowerMode(true, APP_POWER_MODE_CHARGE));
    assert(!App_ThermalAllowsPowerMode(true, APP_POWER_MODE_DRIVE));
    assert(App_ThermalAllowsPowerMode(true, APP_POWER_MODE_OFF));
    assert(App_ThermalAllowsPowerMode(false, APP_POWER_MODE_CHARGE));

    assert(!App_ThermalAllowsOutputState(true, true));
    assert(App_ThermalAllowsOutputState(true, false));
    assert(App_ThermalAllowsOutputState(false, true));

    assert(!App_ThermalAllowsMotorMode(true, APP_MOTOR_MODE_WAKE));
    assert(!App_ThermalAllowsMotorMode(true, APP_MOTOR_MODE_FORWARD));
    assert(!App_ThermalAllowsMotorMode(true, APP_MOTOR_MODE_REVERSE));
    assert(!App_ThermalAllowsMotorMode(true, APP_MOTOR_MODE_BRAKE));
    assert(!App_ThermalAllowsMotorMode(true, APP_MOTOR_MODE_STOP));
    assert(App_ThermalAllowsMotorMode(true, APP_MOTOR_MODE_SLEEP));
    assert(App_ThermalAllowsMotorMode(false, APP_MOTOR_MODE_FORWARD));
}

int main(void)
{
    TestTripAndClearThresholds();
    TestInvalidTemperatureTripsAndDoesNotClear();
    TestSafetyPolicies();
    return 0;
}
