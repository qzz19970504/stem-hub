#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_thermal_guard.h"

#define PROTECTED_TEMPERATURE_COUNT 5U

static void FillTemperatures(int32_t *temperatures, int32_t temperature_deci_c)
{
    size_t index;

    for (index = 0U; index < PROTECTED_TEMPERATURE_COUNT; ++index)
    {
        temperatures[index] = temperature_deci_c;
    }
}

static void TestTripAndClearThresholds(void)
{
    AppThermalGuard guard;
    int32_t temperatures[PROTECTED_TEMPERATURE_COUNT];

    App_ThermalGuardInit(&guard, 600, 550);
    FillTemperatures(temperatures, 400);
    temperatures[0] = 600;
    assert(!guard.active);
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_NO_CHANGE);
    assert(!guard.active);

    temperatures[0] = 601;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);

    FillTemperatures(temperatures, 550);
    temperatures[0] = 551;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);

    temperatures[0] = 550;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_CLEARED);
    assert(!guard.active);
}

static void TestEveryProtectedPositionCanTrip(void)
{
    AppThermalGuard guard;
    int32_t temperatures[PROTECTED_TEMPERATURE_COUNT];
    size_t position;

    for (position = 0U; position < PROTECTED_TEMPERATURE_COUNT; ++position)
    {
        App_ThermalGuardInit(&guard, 600, 550);
        FillTemperatures(temperatures, 400);
        temperatures[position] = 601;
        assert(App_ThermalGuardUpdate(&guard,
                                      temperatures,
                                      PROTECTED_TEMPERATURE_COUNT)
               == APP_THERMAL_TRIPPED);
        assert(guard.active);
    }
}

static void TestFourthAndFifthPositionsBlockClear(void)
{
    AppThermalGuard guard;
    int32_t temperatures[PROTECTED_TEMPERATURE_COUNT];

    App_ThermalGuardInit(&guard, 600, 550);
    FillTemperatures(temperatures, 400);
    temperatures[3] = 601;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_TRIPPED);

    FillTemperatures(temperatures, 550);
    temperatures[3] = 551;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_NO_CHANGE);
    temperatures[3] = 550;
    temperatures[4] = INT32_MAX;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_NO_CHANGE);
    temperatures[4] = 550;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_CLEARED);
}

static void TestInvalidTemperatureTripsAndDoesNotClear(void)
{
    AppThermalGuard guard;
    int32_t temperatures[PROTECTED_TEMPERATURE_COUNT];

    App_ThermalGuardInit(&guard, 600, 550);
    FillTemperatures(temperatures, 400);
    temperatures[4] = INT32_MAX;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);

    FillTemperatures(temperatures, 400);
    temperatures[3] = INT32_MAX;
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);
}

static void TestMalformedInputsFailSafe(void)
{
    AppThermalGuard guard;
    int32_t temperatures[PROTECTED_TEMPERATURE_COUNT];

    FillTemperatures(temperatures, 400);

    App_ThermalGuardInit(&guard, 600, 550);
    assert(App_ThermalGuardUpdate(&guard,
                                  NULL,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT - 1U)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);

    App_ThermalGuardInit(&guard, 600, 550);
    assert(App_ThermalGuardUpdate(&guard,
                                  temperatures,
                                  PROTECTED_TEMPERATURE_COUNT + 1U)
           == APP_THERMAL_TRIPPED);
    assert(guard.active);
    assert(App_ThermalGuardUpdate(&guard,
                                  NULL,
                                  PROTECTED_TEMPERATURE_COUNT)
           == APP_THERMAL_NO_CHANGE);
    assert(guard.active);
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
    TestEveryProtectedPositionCanTrip();
    TestFourthAndFifthPositionsBlockClear();
    TestInvalidTemperatureTripsAndDoesNotClear();
    TestMalformedInputsFailSafe();
    TestSafetyPolicies();
    return 0;
}
