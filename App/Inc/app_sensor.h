#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include <stdbool.h>

#include "app_types.h"

void App_SensorTask(void *argument);
bool App_SensorTryGetSnapshot(AppSensorSnapshot *snapshot);

#endif