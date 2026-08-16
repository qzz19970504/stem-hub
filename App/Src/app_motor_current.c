#include "app_motor_current.h"

#include <stdint.h>

#include "app_config.h"

uint32_t App_MotorCurrentFromMillivolts(uint32_t millivolts)
{
    const uint64_t denominator_mv_per_ma =
        ((uint64_t)APP_MOTOR_IPROPI_AIPROPI_UA_PER_A
         * (uint64_t)APP_MOTOR_IPROPI_R19_OHMS) / 1000ULL;
    const uint64_t current_ma =
        ((uint64_t)millivolts * 1000ULL) / denominator_mv_per_ma;

    return (current_ma > UINT32_MAX) ? UINT32_MAX : (uint32_t)current_ma;
}

uint32_t App_MotorCurrentToDeciAmps(uint32_t current_ma)
{
    uint32_t current_deci_a = (current_ma + 50U) / 100U;

    if (current_deci_a > APP_MOTOR_CURRENT_MAX_DECI_A)
    {
        current_deci_a = APP_MOTOR_CURRENT_MAX_DECI_A;
    }

    return current_deci_a;
}
