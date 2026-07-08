#ifndef APP_MOTOR_H
#define APP_MOTOR_H

#include <stdbool.h>

#include "app_at_protocol.h"
#include "app_types.h"

void App_MotorTask(void *argument);
bool App_MotorEnqueueMode(AppMotorMode mode);
bool App_MotorTryGetStatus(AppMotorStatus *status);
const char *App_MotorModeToString(AppMotorMode mode);

#endif