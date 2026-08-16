#include "app_motor_stall_guard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

static void App_MotorStallGuardClearPending(AppMotorStallGuard *guard)
{
    guard->over_threshold_start_tick = 0U;
    guard->is_over_threshold = false;
}

void App_MotorStallGuardStart(AppMotorStallGuard *guard, uint32_t tick)
{
    if (guard == NULL)
    {
        return;
    }

    guard->run_start_tick = tick;
    guard->is_running = true;
    App_MotorStallGuardClearPending(guard);
}

void App_MotorStallGuardStop(AppMotorStallGuard *guard)
{
    if (guard == NULL)
    {
        return;
    }

    guard->run_start_tick = 0U;
    guard->is_running = false;
    App_MotorStallGuardClearPending(guard);
}

bool App_MotorStallGuardUpdate(AppMotorStallGuard *guard,
                               uint32_t tick,
                               bool is_sample_valid,
                               uint32_t current_ma,
                               uint32_t threshold_ma)
{
    if ((guard == NULL) || !guard->is_running)
    {
        return false;
    }

    if ((uint32_t)(tick - guard->run_start_tick)
        < APP_MOTOR_STALL_STARTUP_BLANKING_MS)
    {
        App_MotorStallGuardClearPending(guard);
        return false;
    }

    if (!is_sample_valid || (current_ma < threshold_ma))
    {
        App_MotorStallGuardClearPending(guard);
        return false;
    }

    if (!guard->is_over_threshold)
    {
        guard->over_threshold_start_tick = tick;
        guard->is_over_threshold = true;
        return false;
    }

    return (uint32_t)(tick - guard->over_threshold_start_tick)
        >= APP_MOTOR_STALL_PERSISTENCE_MS;
}
