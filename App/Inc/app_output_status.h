#ifndef APP_OUTPUT_STATUS_H
#define APP_OUTPUT_STATUS_H

#include <stdbool.h>
#include <stddef.h>

#include "app_types.h"

const char *App_OutputStatusPowerModeName(AppPowerMode power_mode);
const char *App_OutputStatusChargePhaseName(AppChargePhase charge_phase);
bool App_OutputStatusFormat(char *buffer,
                            size_t buffer_size,
                            const AppIoStatus *status);

#endif
