#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_motor_stall_guard.h"

static void TestStartupBlankingIgnoresHighCurrent(void)
{
    AppMotorStallGuard guard = {0};

    App_MotorStallGuardStart(&guard, 1000U);
    assert(!App_MotorStallGuardUpdate(&guard, 1299U, true, 9000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 1300U, true, 4000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 1399U, true, 4000U, 4000U));
    assert(App_MotorStallGuardUpdate(&guard, 1400U, true, 4000U, 4000U));
}

static void TestLowSampleBreaksContinuousEvidence(void)
{
    AppMotorStallGuard guard = {0};

    App_MotorStallGuardStart(&guard, 0U);
    assert(!App_MotorStallGuardUpdate(&guard, 300U, true, 5000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 350U, true, 5000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 360U, true, 3999U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 370U, true, 5000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 469U, true, 5000U, 4000U));
    assert(App_MotorStallGuardUpdate(&guard, 470U, true, 5000U, 4000U));
}

static void TestInvalidSampleBreaksContinuousEvidence(void)
{
    AppMotorStallGuard guard = {0};

    App_MotorStallGuardStart(&guard, 0U);
    assert(!App_MotorStallGuardUpdate(&guard, 300U, true, 5000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 350U, false, 5000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 360U, true, 5000U, 4000U));
    assert(App_MotorStallGuardUpdate(&guard, 460U, true, 5000U, 4000U));
}

static void TestStopAndRestartResetTiming(void)
{
    AppMotorStallGuard guard = {0};

    App_MotorStallGuardStart(&guard, 0U);
    assert(!App_MotorStallGuardUpdate(&guard, 300U, true, 5000U, 4000U));
    App_MotorStallGuardStop(&guard);
    assert(!App_MotorStallGuardUpdate(&guard, 500U, true, 5000U, 4000U));

    App_MotorStallGuardStart(&guard, 500U);
    assert(!App_MotorStallGuardUpdate(&guard, 799U, true, 5000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 800U, true, 5000U, 4000U));
    assert(App_MotorStallGuardUpdate(&guard, 900U, true, 5000U, 4000U));
}

static void TestTickWrapIsHandledByUnsignedSubtraction(void)
{
    AppMotorStallGuard guard = {0};
    const uint32_t start_tick = UINT32_MAX - 199U;

    App_MotorStallGuardStart(&guard, start_tick);
    assert(!App_MotorStallGuardUpdate(&guard, 99U, true, 5000U, 4000U));
    assert(!App_MotorStallGuardUpdate(&guard, 100U, true, 5000U, 4000U));
    assert(App_MotorStallGuardUpdate(&guard, 200U, true, 5000U, 4000U));
}

static void TestNullGuardFailsSafely(void)
{
    App_MotorStallGuardStart(NULL, 0U);
    App_MotorStallGuardStop(NULL);
    assert(!App_MotorStallGuardUpdate(NULL, 0U, true, 5000U, 4000U));
}

int main(void)
{
    TestStartupBlankingIgnoresHighCurrent();
    TestLowSampleBreaksContinuousEvidence();
    TestInvalidSampleBreaksContinuousEvidence();
    TestStopAndRestartResetTiming();
    TestTickWrapIsHandledByUnsignedSubtraction();
    TestNullGuardFailsSafely();
    puts("OK: motor stall guard timing verified.");
    return 0;
}
