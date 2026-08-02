#ifndef APP_TASK_SAFETY_H
#define APP_TASK_SAFETY_H

#include <stdbool.h>

#include "app_thermal_guard.h"

typedef bool (*AppTaskSafetySetThermalStateFn)(bool active, void *context);
typedef void (*AppTaskSafetyRequestFn)(void *context);

typedef struct
{
    AppTaskSafetySetThermalStateFn set_thermal_state;
    AppTaskSafetyRequestFn request_output_stop;
    AppTaskSafetyRequestFn request_motor_sleep;
    void *context;
} AppTaskSafetyCallbacks;

void App_TaskSafetyHandleThermalTransition(
    AppThermalTransition transition,
    const AppTaskSafetyCallbacks *callbacks);
bool App_TaskSafetyRequiresForcedSafe(bool state_available,
                                      bool thermal_active);
bool App_TaskSafetyAllowsPower(bool state_available,
                               bool thermal_active,
                               AppPowerMode mode);
bool App_TaskSafetyAllowsOutput(bool state_available,
                                bool thermal_active,
                                bool enabled);
bool App_TaskSafetyAllowsMotor(bool state_available,
                               bool thermal_active,
                               AppMotorMode mode);

#endif
