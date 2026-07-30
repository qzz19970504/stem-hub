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

static void TestFirstChargeAndFullCycle(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 10U, 50U));
    ExpectAction(App_ChargeCycleRequest(&cycle,
                                        APP_POWER_MODE_CHARGE,
                                        100U),
                 true,
                 APP_POWER_MODE_CHARGE);
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.deadline_tick == 110U);
    assert(App_ChargeCycleWaitTicks(&cycle, 100U) == 10U);

    ExpectAction(App_ChargeCyclePoll(&cycle, 109U),
                 false,
                 APP_POWER_MODE_OFF);
    assert(App_ChargeCycleWaitTicks(&cycle, 109U) == 1U);

    ExpectAction(App_ChargeCyclePoll(&cycle, 110U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.phase == APP_CHARGE_CYCLE_OFF_PHASE);
    assert(cycle.deadline_tick == 160U);

    ExpectAction(App_ChargeCyclePoll(&cycle, 160U),
                 true,
                 APP_POWER_MODE_CHARGE);
    assert(cycle.phase == APP_CHARGE_CYCLE_ON_PHASE);
    assert(cycle.deadline_tick == 170U);
}

static void TestDuplicateChargeDoesNotResetEitherPhase(void)
{
    AppChargeCycle cycle;
    uint32_t original_deadline;

    assert(App_ChargeCycleInit(&cycle, 10U, 50U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 100U);
    original_deadline = cycle.deadline_tick;

    ExpectAction(App_ChargeCycleRequest(&cycle,
                                        APP_POWER_MODE_CHARGE,
                                        105U),
                 false,
                 APP_POWER_MODE_OFF);
    assert(cycle.deadline_tick == original_deadline);
    assert(App_ChargeCycleWaitTicks(&cycle, 105U) == 5U);

    (void)App_ChargeCyclePoll(&cycle, 110U);
    original_deadline = cycle.deadline_tick;
    ExpectAction(App_ChargeCycleRequest(&cycle,
                                        APP_POWER_MODE_CHARGE,
                                        130U),
                 false,
                 APP_POWER_MODE_OFF);
    assert(cycle.phase == APP_CHARGE_CYCLE_OFF_PHASE);
    assert(cycle.deadline_tick == original_deadline);
    assert(App_ChargeCycleWaitTicks(&cycle, 130U) == 30U);
}

static void TestOffAndDriveCancelImmediately(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 10U, 50U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 0U);
    ExpectAction(App_ChargeCycleRequest(&cycle,
                                        APP_POWER_MODE_OFF,
                                        4U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.phase == APP_CHARGE_CYCLE_IDLE);
    assert(App_ChargeCycleWaitTicks(&cycle, 4U)
           == APP_CHARGE_CYCLE_WAIT_FOREVER);

    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 20U);
    (void)App_ChargeCyclePoll(&cycle, 30U);
    assert(cycle.phase == APP_CHARGE_CYCLE_OFF_PHASE);
    ExpectAction(App_ChargeCycleRequest(&cycle,
                                        APP_POWER_MODE_DRIVE,
                                        35U),
                 true,
                 APP_POWER_MODE_DRIVE);
    assert(cycle.phase == APP_CHARGE_CYCLE_IDLE);
    ExpectAction(App_ChargeCyclePoll(&cycle, 1000U),
                 false,
                 APP_POWER_MODE_OFF);
}

static void TestUnrelatedWorkDoesNotExtendDeadline(void)
{
    AppChargeCycle cycle;

    assert(App_ChargeCycleInit(&cycle, 10U, 50U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, 100U);

    assert(App_ChargeCycleWaitTicks(&cycle, 103U) == 7U);
    assert(App_ChargeCycleWaitTicks(&cycle, 107U) == 3U);
    assert(cycle.deadline_tick == 110U);
    assert(App_ChargeCycleWaitTicks(&cycle, 110U) == 0U);
}

static void TestTickWrap(void)
{
    AppChargeCycle cycle;
    uint32_t start = UINT32_MAX - 5U;

    assert(App_ChargeCycleInit(&cycle, 10U, 50U));
    (void)App_ChargeCycleRequest(&cycle, APP_POWER_MODE_CHARGE, start);

    assert(cycle.deadline_tick == 4U);
    assert(App_ChargeCycleWaitTicks(&cycle, start) == 10U);
    assert(App_ChargeCycleWaitTicks(&cycle, 3U) == 1U);
    ExpectAction(App_ChargeCyclePoll(&cycle, 4U),
                 true,
                 APP_POWER_MODE_OFF);
    assert(cycle.deadline_tick == 54U);
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
    TestFirstChargeAndFullCycle();
    TestDuplicateChargeDoesNotResetEitherPhase();
    TestOffAndDriveCancelImmediately();
    TestUnrelatedWorkDoesNotExtendDeadline();
    TestTickWrap();
    TestMillisecondsUseCeilingTickConversion();
    return 0;
}
