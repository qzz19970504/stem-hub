#include "app_stall_config_service.h"

#include <stdint.h>

#include "app_stall_config.h"
#include "app_state.h"

AppStallConfigSetResult App_StallConfigServiceSetCurrentMa(
    uint32_t current_ma)
{
    AppMotorStatus motor_status;
    uint32_t active_current_ma = 0U;

    if (!App_StateTryGetMotorStatus(&motor_status))
    {
        return APP_STALL_CONFIG_SET_STATE_BUSY;
    }

    if ((motor_status.mode == APP_MOTOR_MODE_FORWARD)
        || (motor_status.mode == APP_MOTOR_MODE_REVERSE))
    {
        return APP_STALL_CONFIG_SET_MOTOR_RUNNING;
    }

    if (!App_StateTryGetStallCurrentMa(&active_current_ma))
    {
        return APP_STALL_CONFIG_SET_STATE_BUSY;
    }

    if (active_current_ma == current_ma)
    {
        return APP_STALL_CONFIG_SET_OK;
    }

    if (!App_StallConfigStoreCurrentMa(current_ma))
    {
        return APP_STALL_CONFIG_SET_FLASH_WRITE_FAILED;
    }

    if (App_StateSetStallCurrentMa(current_ma)
        || App_StateSetStallCurrentMa(current_ma))
    {
        return APP_STALL_CONFIG_SET_OK;
    }

    return APP_STALL_CONFIG_SET_STATE_BUSY;
}
