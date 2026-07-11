#ifndef APP_MOTOR_H
#define APP_MOTOR_H

#include <stdbool.h>

#include "app_at_protocol.h"
#include "app_types.h"

void App_MotorTask(void *argument);
bool App_MotorEnqueueMode(AppMotorMode mode);
bool App_MotorTryGetStatus(AppMotorStatus *status);
bool App_MotorGetStartupOverrideMode(AppMotorMode *mode);
bool App_MotorIsAutoSequenceEnabled(void);
bool App_MotorAllowsExternalControl(void);
uint32_t App_MotorGetAutoSequenceStepDurationMs(void);
bool App_MotorGetAutoSequenceModeForStep(uint32_t step_index, AppMotorMode *mode);
const char *App_MotorModeToString(AppMotorMode mode);

#endif