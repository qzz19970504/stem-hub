#ifndef APP_OUTPUT_H
#define APP_OUTPUT_H

#include <stdbool.h>

#include "app_types.h"

void App_NmosTask(void *argument);
bool App_OutputEnqueueState(AppOutputTarget target, bool enabled);

#endif