#ifndef APP_CORE_H
#define APP_CORE_H

#include <stdint.h>

void App_CoreCreateObjects(void);
void App_CoreInit(void);

void App_AtTask(void *argument);
void App_SensorTask(void *argument);
void App_MotorTask(void *argument);
void App_LedTask(void *argument);
void App_NmosTask(void *argument);
void App_BridgeTask(void *argument);

#endif
