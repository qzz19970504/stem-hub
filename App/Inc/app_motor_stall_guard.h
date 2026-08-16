#ifndef APP_MOTOR_STALL_GUARD_H
#define APP_MOTOR_STALL_GUARD_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    uint32_t run_start_tick;
    uint32_t over_threshold_start_tick;
    bool is_running;
    bool is_over_threshold;
} AppMotorStallGuard;

void App_MotorStallGuardStart(AppMotorStallGuard *guard, uint32_t tick);
void App_MotorStallGuardStop(AppMotorStallGuard *guard);
bool App_MotorStallGuardUpdate(AppMotorStallGuard *guard,
                               uint32_t tick,
                               bool is_sample_valid,
                               uint32_t current_ma,
                               uint32_t threshold_ma);

#endif
