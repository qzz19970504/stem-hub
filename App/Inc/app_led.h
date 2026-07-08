#ifndef APP_LED_H
#define APP_LED_H

#include <stdbool.h>

void App_LedTask(void *argument);
bool App_LedEnqueueState(bool enabled);

#endif