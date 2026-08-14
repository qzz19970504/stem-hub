#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_charge_cycle.h"

static void ExpectAction(AppChargeCycleAction action,
                         bool expected_apply,
                         AppPowerMode expected_mode)
{
    assert(action.apply_mode == expected_apply);
    if (expected_apply)
    {
        assert(action.mode == expected_mode);
    }
}

static void ExpectNoAction(AppChargeCycleAction action)
{
    ExpectAction(action, false, APP_POWER_MODE_OFF);
}

static void TestDefaultTenOnFiftyOff(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 60U, 10U));
    assert(cycle.cycle_ticks == 60U);
    assert(cycle.configured_on_ticks == 10U);

    ExpectAction(App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 100U),
                 true,
                 APP_POWER_MODE_CHARGE);
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.active_off_ticks == 50U);
    assert(cycle.deadline_tick == 110U);

    ExpectNoAction(App_ChargeCyclePoll(&cycle, 109U));
    ExpectAction(App_ChargeCyclePoll(&cycle, 110U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.phase == APP_CHARGE_CYCLE_OFF_PHASE);
    assert(cycle.deadline_tick == 160U);

    ExpectAction(App_ChargeCyclePoll(&cycle, 160U),
                 true,
                 APP_POWER_MODE_CHARGE);
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.active_off_ticks == 50U);
    assert(cycle.deadline_tick == 170U);
}

static void TestOneOnFiftyNineOff(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 60U, 1U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 0U);
    assert(cycle.active_off_ticks == 59U);
    assert(cycle.deadline_tick == 1U);

    ExpectAction(App_ChargeCyclePoll(&cycle, 1U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.deadline_tick == 60U);
    ExpectAction(App_ChargeCyclePoll(&cycle, 60U),
                 true,
                 APP_POWER_MODE_CHARGE);
    assert(cycle.deadline_tick == 61U);
}

static void TestFiftyNineOnOneOff(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 60U, 59U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 0U);
    assert(cycle.active_off_ticks == 1U);
    assert(cycle.deadline_tick == 59U);

    ExpectAction(App_ChargeCyclePoll(&cycle, 59U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.deadline_tick == 60U);
    ExpectAction(App_ChargeCyclePoll(&cycle, 60U),
                 true,
                 APP_POWER_MODE_CHARGE);
    assert(cycle.deadline_tick == 119U);
}

static void TestSixtyOnStaysOnAcrossBoundaries(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 60U, 60U));
    ExpectAction(App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 5U),
                 true,
                 APP_POWER_MODE_CHARGE);
    assert(cycle.active_off_ticks == 0U);
    assert(cycle.deadline_tick == 65U);

    ExpectNoAction(App_ChargeCyclePoll(&cycle, 65U));
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.active_off_ticks == 0U);
    assert(cycle.deadline_tick == 125U);

    ExpectNoAction(App_ChargeCyclePoll(&cycle, 125U));
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.deadline_tick == 185U);
}

static void TestConfigureDuringOnUsesNextCycle(void)
{
    AppChargeCycle cycle;
    uint32_t original_deadline;

    assert(App_ChargeCycleInit(&cycle, 60U, 10U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 100U);
    original_deadline = cycle.deadline_tick;

    assert(App_ChargeCycleConfigureOnTicks(&cycle, 20U));
    assert(cycle.configured_on_ticks == 20U);
    assert(cycle.active_off_ticks == 50U);
    assert(cycle.deadline_tick == original_deadline);

    (void)App_ChargeCyclePoll(&cycle, 110U);
    assert(cycle.phase == APP_CHARGE_CYCLE_OFF_PHASE);
    assert(cycle.deadline_tick == 160U);
    (void)App_ChargeCyclePoll(&cycle, 160U);
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.active_off_ticks == 40U);
    assert(cycle.deadline_tick == 180U);
}

static void TestConfigureDuringOffUsesNextCycle(void)
{
    AppChargeCycle cycle;
    uint32_t original_deadline;

    assert(App_ChargeCycleInit(&cycle, 60U, 10U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 0U);
    (void)App_ChargeCyclePoll(&cycle, 10U);
    original_deadline = cycle.deadline_tick;

    assert(App_ChargeCycleConfigureOnTicks(&cycle, 59U));
    assert(cycle.configured_on_ticks == 59U);
    assert(cycle.active_off_ticks == 50U);
    assert(cycle.deadline_tick == original_deadline);

    (void)App_ChargeCyclePoll(&cycle, 60U);
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.active_off_ticks == 1U);
    assert(cycle.deadline_tick == 119U);
}

static void TestConfigureDownFromSixtyTakesEffectAtBoundary(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 60U, 60U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 0U);
    assert(App_ChargeCycleConfigureOnTicks(&cycle, 10U));
    assert(cycle.deadline_tick == 60U);
    assert(cycle.active_off_ticks == 0U);

    ExpectNoAction(App_ChargeCyclePoll(&cycle, 60U));
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.active_off_ticks == 50U);
    assert(cycle.deadline_tick == 70U);

    ExpectAction(App_ChargeCyclePoll(&cycle, 70U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.deadline_tick == 120U);
}

static void TestDuplicateChargeDoesNotResetEitherPhase(void)
{
    AppChargeCycle cycle;
    uint32_t original_deadline;

    assert(App_ChargeCycleInit(&cycle, 60U, 10U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 100U);
    original_deadline = cycle.deadline_tick;

    ExpectNoAction(App_ChargeCycleRequest(&cycle,
                                          APP_POWER_MODE_CHARGE,
                                          105U));
    assert(cycle.deadline_tick == original_deadline);

    (void)App_ChargeCyclePoll(&cycle, 110U);
    original_deadline = cycle.deadline_tick;
    ExpectNoAction(App_ChargeCycleRequest(&cycle,
                                          APP_POWER_MODE_CHARGE,
                                          130U));
    assert(cycle.phase == APP_CHARGE_CYCLE_OFF_PHASE);
    assert(cycle.deadline_tick == original_deadline);
}

static void TestOffAndDriveCancelImmediately(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 60U, 10U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 0U);
    ExpectAction(App_ChargeCycleRequest(&cycle, APP_POWER_MODE_OFF, 4U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.phase == APP_CHARGE_CYCLE_IDLE);
    assert(cycle.deadline_tick == 0U);
    assert(App_ChargeCycleWaitTicks(&cycle, 4U)
           == APP_CHARGE_CYCLE_WAIT_FOREVER);

    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 20U);
    (void)App_ChargeCyclePoll(&cycle, 30U);
    ExpectAction(App_ChargeCycleRequest(&cycle, APP_POWER_MODE_DRIVE, 35U),
                 true,
                 APP_POWER_MODE_DRIVE);
    assert(cycle.phase == APP_CHARGE_CYCLE_IDLE);
    ExpectNoAction(App_ChargeCyclePoll(&cycle, 1000U));
}

static void TestUnrelatedWaitsDoNotExtendDeadline(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 60U, 10U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 100U);

    assert(App_ChargeCycleWaitTicks(&cycle, 103U) == 7U);
    assert(App_ChargeCycleWaitTicks(&cycle, 107U) == 3U);
    assert(cycle.deadline_tick == 110U);
    assert(App_ChargeCycleWaitTicks(&cycle, 110U) == 0U);
    assert(App_ChargeCycleWaitTicks(NULL, 0U)
           == APP_CHARGE_CYCLE_WAIT_FOREVER);
}

static void TestTickWrap(void)
{
    AppChargeCycle cycle;
    uint32_t start = UINT32_MAX - 5U;

    assert(App_ChargeCycleInit(&cycle, 60U, 10U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, start);

    assert(cycle.deadline_tick == 4U);
    assert(App_ChargeCycleWaitTicks(&cycle, start) == 10U);
    assert(App_ChargeCycleWaitTicks(&cycle, 3U) == 1U);
    ExpectAction(App_ChargeCyclePoll(&cycle, 4U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.deadline_tick == 54U);
    ExpectAction(App_ChargeCyclePoll(&cycle, 54U),
                 true,
                 APP_POWER_MODE_CHARGE);
    assert(cycle.deadline_tick == 64U);
}

static void TestInvalidInitAndConfigure(void)
{
    AppChargeCycle cycle;

    assert(!App_ChargeCycleInit(NULL, 60U, 10U));
    assert(!App_ChargeCycleInit(&cycle, 0U, 0U));
    assert(!App_ChargeCycleInit(&cycle, 60U, 0U));
    assert(!App_ChargeCycleInit(&cycle, 60U, 61U));
    assert(!App_ChargeCycleInit(&cycle, (uint32_t)INT32_MAX + 1U, 1U));
    assert(App_ChargeCycleInit(&cycle, (uint32_t)INT32_MAX,
                              (uint32_t)INT32_MAX));

    assert(!App_ChargeCycleConfigureOnTicks(NULL, 1U));
    assert(!App_ChargeCycleConfigureOnTicks(&cycle, 0U));
    assert(!App_ChargeCycleConfigureOnTicks(&cycle,
                                            (uint32_t)INT32_MAX + 1U));
    assert(cycle.configured_on_ticks == (uint32_t)INT32_MAX);
    assert(App_ChargeCycleConfigureOnTicks(&cycle, 1U));
    assert(cycle.configured_on_ticks == 1U);
}

static void TestMillisecondsUseCeilingTickConversion(void)
{
    assert(App_ChargeCycleMillisecondsToTicks(10000U, 1000U) == 10000U);
    assert(App_ChargeCycleMillisecondsToTicks(50000U, 100U) == 5000U);
    assert(App_ChargeCycleMillisecondsToTicks(1U, 128U) == 1U);
    assert(App_ChargeCycleMillisecondsToTicks(0U, 1000U) == 0U);
    assert(App_ChargeCycleMillisecondsToTicks(1000U, 0U) == 0U);
    assert(App_ChargeCycleMillisecondsToTicks(UINT32_MAX, UINT32_MAX) == 0U);
}

int main(void)
{
    TestDefaultTenOnFiftyOff();
    TestOneOnFiftyNineOff();
    TestFiftyNineOnOneOff();
    TestSixtyOnStaysOnAcrossBoundaries();
    TestConfigureDuringOnUsesNextCycle();
    TestConfigureDuringOffUsesNextCycle();
    TestConfigureDownFromSixtyTakesEffectAtBoundary();
    TestDuplicateChargeDoesNotResetEitherPhase();
    TestOffAndDriveCancelImmediately();
    TestUnrelatedWaitsDoNotExtendDeadline();
    TestTickWrap();
    TestInvalidInitAndConfigure();
    TestMillisecondsUseCeilingTickConversion();
    return 0;
}
