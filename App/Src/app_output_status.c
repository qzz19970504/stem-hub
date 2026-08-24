#include "app_output_status.h"

#include <stdio.h>

const char *App_OutputStatusPowerModeName(AppPowerMode power_mode)
{
    switch (power_mode)
    {
    case APP_POWER_MODE_OFF:
        return "OFF";
    case APP_POWER_MODE_CHARGE:
        return "CHARGE";
    case APP_POWER_MODE_DRIVE:
        return "DRIVE";
    default:
        return "UNKNOWN";
    }
}

const char *App_OutputStatusChargePhaseName(AppChargePhase charge_phase)
{
    switch (charge_phase)
    {
    case APP_CHARGE_PHASE_IDLE:
        return "IDLE";
    case APP_CHARGE_PHASE_ON:
        return "ON";
    case APP_CHARGE_PHASE_OFF:
        return "OFF";
    default:
        return "UNKNOWN";
    }
}

bool App_OutputStatusFormat(char *buffer,
                            size_t buffer_size,
                            const AppIoStatus *status)
{
    int response_length;

    if ((buffer == NULL) || (buffer_size == 0U) || (status == NULL))
    {
        return false;
    }

    response_length = snprintf(
        buffer,
        buffer_size,
        "+OUTPUT:POWER=%s,CHARGE_PHASE=%s,NMOS1=%u,NMOS2=%u,"
        "LIGHTS=%u,MOTOR_BYPASS=%u,CHARGE_BYPASS=%u\r\nOK\r\n",
        App_OutputStatusPowerModeName(status->power_mode),
        App_OutputStatusChargePhaseName(status->charge_phase),
        status->nmos1_enabled ? 1U : 0U,
        status->nmos2_enabled ? 1U : 0U,
        status->led_master_enabled ? 1U : 0U,
        status->motor_bypass_enabled ? 1U : 0U,
        status->charge_bypass_enabled ? 1U : 0U);

    return (response_length >= 0) && ((size_t)response_length < buffer_size);
}
