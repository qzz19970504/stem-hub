#include "app_charge_cycle.h"

#include <stddef.h>

static AppChargeCycleAction App_ChargeCycleNoAction(void)
{
    AppChargeCycleAction action = {
        .apply_mode = false,
        .mode = APP_POWER_MODE_OFF,
    };
    return action;
}

static AppChargeCycleAction App_ChargeCycleApply(AppPowerMode mode)
{
    AppChargeCycleAction action = {
        .apply_mode = true,
        .mode = mode,
    };
    return action;
}

static bool App_ChargeCycleDeadlineReached(uint32_t now_tick,
                                           uint32_t deadline_tick)
{
    return (int32_t)(now_tick - deadline_tick) >= 0;
}

uint32_t App_ChargeCycleMillisecondsToTicks(uint32_t milliseconds,
                                            uint32_t tick_frequency_hz)
{
    uint64_t ticks;

    if ((milliseconds == 0U) || (tick_frequency_hz == 0U))
    {
        return 0U;
    }

    ticks = (((uint64_t)milliseconds * tick_frequency_hz) + 999U) / 1000U;
    if ((ticks == 0U) || (ticks > (uint64_t)INT32_MAX))
    {
        return 0U;
    }

    return (uint32_t)ticks;
}

bool App_ChargeCycleInit(AppChargeCycle *cycle,
                         uint32_t on_ticks,
                         uint32_t off_ticks)
{
    if ((cycle == NULL)
        || (on_ticks == 0U)
        || (off_ticks == 0U)
        || (on_ticks > (uint32_t)INT32_MAX)
        || (off_ticks > (uint32_t)INT32_MAX))
    {
        return false;
    }

    cycle->phase = APP_CHARGE_CYCLE_IDLE;
    cycle->deadline_tick = 0U;
    cycle->on_ticks = on_ticks;
    cycle->off_ticks = off_ticks;
    return true;
}

AppChargeCycleAction App_ChargeCycleRequest(AppChargeCycle *cycle,
                                            AppPowerMode requested_mode,
                                            uint32_t now_tick)
{
    if (cycle == NULL)
    {
        return App_ChargeCycleNoAction();
    }

    if (requested_mode == APP_POWER_MODE_CHARGE)
    {
        if (cycle->phase != APP_CHARGE_CYCLE_IDLE)
        {
            return App_ChargeCycleNoAction();
        }

        cycle->phase = APP_CHARGE_CYCLE_ON_PHASE;
        cycle->deadline_tick = now_tick + cycle->on_ticks;
        return App_ChargeCycleApply(APP_POWER_MODE_CHARGE);
    }

    if ((requested_mode == APP_POWER_MODE_OFF)
        || (requested_mode == APP_POWER_MODE_DRIVE))
    {
        cycle->phase = APP_CHARGE_CYCLE_IDLE;
        cycle->deadline_tick = 0U;
        return App_ChargeCycleApply(requested_mode);
    }

    return App_ChargeCycleNoAction();
}

AppChargeCycleAction App_ChargeCyclePoll(AppChargeCycle *cycle,
                                         uint32_t now_tick)
{
    if ((cycle == NULL)
        || (cycle->phase == APP_CHARGE_CYCLE_IDLE)
        || !App_ChargeCycleDeadlineReached(now_tick, cycle->deadline_tick))
    {
        return App_ChargeCycleNoAction();
    }

    if (cycle->phase == APP_CHARGE_CYCLE_ON_PHASE)
    {
        cycle->phase = APP_CHARGE_CYCLE_OFF_PHASE;
        cycle->deadline_tick = now_tick + cycle->off_ticks;
        return App_ChargeCycleApply(APP_POWER_MODE_OFF);
    }

    cycle->phase = APP_CHARGE_CYCLE_ON_PHASE;
    cycle->deadline_tick = now_tick + cycle->on_ticks;
    return App_ChargeCycleApply(APP_POWER_MODE_CHARGE);
}

uint32_t App_ChargeCycleWaitTicks(const AppChargeCycle *cycle,
                                  uint32_t now_tick)
{
    if ((cycle == NULL) || (cycle->phase == APP_CHARGE_CYCLE_IDLE))
    {
        return APP_CHARGE_CYCLE_WAIT_FOREVER;
    }

    if (App_ChargeCycleDeadlineReached(now_tick, cycle->deadline_tick))
    {
        return 0U;
    }

    return cycle->deadline_tick - now_tick;
}
