#ifndef APP_POWER_PATH_H
#define APP_POWER_PATH_H

#include <stdbool.h>

#include "app_types.h"

typedef void (*AppPowerPathWrite)(AppOutputTarget target,
                                  bool enabled,
                                  void *context);

bool App_OutputTargetAllowsDirectControl(AppOutputTarget target);
bool App_PowerPathApply(AppPowerMode mode,
                        AppPowerPathWrite write,
                        void *context);

#endif
