#ifndef APP_AT_COMMAND_GUARD_H
#define APP_AT_COMMAND_GUARD_H

#include <stdbool.h>

#include "app_at_protocol.h"

typedef enum
{
    APP_AT_COMMAND_GUARD_ALLOW = 0,
    APP_AT_COMMAND_GUARD_STATE_BUSY,
    APP_AT_COMMAND_GUARD_OVER_TEMPERATURE
} AppAtCommandGuardResult;

AppAtCommandGuardResult App_AtCommandGuardEvaluate(
    const AppAtCommand *command,
    bool thermal_state_available,
    bool thermal_protection_active);

#endif
