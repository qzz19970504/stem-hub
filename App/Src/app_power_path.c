#include "app_power_path.h"

bool App_OutputTargetAllowsDirectControl(AppOutputTarget target)
{
    return (target == APP_OUTPUT_TARGET_NMOS1)
        || (target == APP_OUTPUT_TARGET_NMOS2);
}

bool App_PowerPathApply(AppPowerMode mode,
                        AppPowerPathWrite write,
                        void *context)
{
    if ((write == NULL)
        || ((mode != APP_POWER_MODE_OFF)
            && (mode != APP_POWER_MODE_CHARGE)
            && (mode != APP_POWER_MODE_DRIVE)))
    {
        return false;
    }

    write(APP_OUTPUT_TARGET_UVLO, false, context);
    write(APP_OUTPUT_TARGET_MP4317, false, context);

    if (mode == APP_POWER_MODE_CHARGE)
    {
        write(APP_OUTPUT_TARGET_UVLO, true, context);
    }
    else if (mode == APP_POWER_MODE_DRIVE)
    {
        write(APP_OUTPUT_TARGET_MP4317, true, context);
    }

    return true;
}
