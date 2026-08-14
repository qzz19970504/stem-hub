#ifndef APP_CHARGE_CYCLE_H
#define APP_CHARGE_CYCLE_H

#include <stdbool.h>
#include <stdint.h>

#include "app_at_protocol.h"

#define APP_CHARGE_CYCLE_WAIT_FOREVER UINT32_MAX

typedef enum
{
    APP_CHARGE_CYCLE_IDLE = 0,
    APP_CHARGE_CYCLE_ON_PHASE,
    APP_CHARGE_CYCLE_OFF_PHASE
} AppChargeCyclePhase;

typedef struct
{
    AppChargeCyclePhase phase;
    uint32_t deadline_tick;
    uint32_t cycle_ticks;
    uint32_t configured_on_ticks;
    uint32_t active_off_ticks;
} AppChargeCycle;

typedef struct
{
    bool apply_mode;
    AppPowerMode mode;
} AppChargeCycleAction;

uint32_t App_ChargeCycleMillisecondsToTicks(uint32_t milliseconds,
                                            uint32_t tick_frequency_hz);
bool App_ChargeCycleInit(AppChargeCycle *cycle,
                         uint32_t cycle_ticks,
                         uint32_t on_ticks);
bool App_ChargeCycleConfigureOnTicks(AppChargeCycle *cycle,
                                     uint32_t on_ticks);
AppChargeCycleAction App_ChargeCycleRequest(AppChargeCycle *cycle,
                                            AppPowerMode requested_mode,
                                            uint32_t now_tick);
AppChargeCycleAction App_ChargeCyclePoll(AppChargeCycle *cycle,
                                         uint32_t now_tick);
uint32_t App_ChargeCycleWaitTicks(const AppChargeCycle *cycle,
                                  uint32_t now_tick);

#endif
