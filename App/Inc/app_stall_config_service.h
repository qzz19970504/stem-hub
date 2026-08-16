#ifndef APP_STALL_CONFIG_SERVICE_H
#define APP_STALL_CONFIG_SERVICE_H

#include <stdint.h>

typedef enum
{
    APP_STALL_CONFIG_SET_OK = 0,
    APP_STALL_CONFIG_SET_STATE_BUSY,
    APP_STALL_CONFIG_SET_MOTOR_RUNNING,
    APP_STALL_CONFIG_SET_FLASH_WRITE_FAILED
} AppStallConfigSetResult;

AppStallConfigSetResult App_StallConfigServiceSetCurrentMa(
    uint32_t current_ma);

#endif
