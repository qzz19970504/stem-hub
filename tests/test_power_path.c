#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "app_power_path.h"

typedef struct
{
    AppOutputTarget target;
    bool enabled;
} PowerEvent;

typedef struct
{
    PowerEvent events[3];
    size_t count;
} PowerEventLog;

static void RecordWrite(AppOutputTarget target, bool enabled, void *context)
{
    PowerEventLog *log = context;

    assert(log != NULL);
    assert(log->count < 3U);
    log->events[log->count].target = target;
    log->events[log->count].enabled = enabled;
    ++log->count;
}

static void ExpectEvent(const PowerEventLog *log,
                        size_t index,
                        AppOutputTarget expected_target,
                        bool expected_enabled)
{
    assert(log != NULL);
    assert(index < log->count);
    assert(log->events[index].target == expected_target);
    assert(log->events[index].enabled == expected_enabled);
}

int main(void)
{
    PowerEventLog log = {0};

    assert(App_OutputTargetAllowsDirectControl(APP_OUTPUT_TARGET_NMOS1));
    assert(App_OutputTargetAllowsDirectControl(APP_OUTPUT_TARGET_NMOS2));
    assert(!App_OutputTargetAllowsDirectControl(APP_OUTPUT_TARGET_UVLO));
    assert(!App_OutputTargetAllowsDirectControl(APP_OUTPUT_TARGET_MP4317));
    assert(!App_PowerPathAllowsAuxiliaryOutput(APP_POWER_MODE_OFF));
    assert(!App_PowerPathAllowsAuxiliaryOutput(APP_POWER_MODE_CHARGE));
    assert(App_PowerPathAllowsAuxiliaryOutput(APP_POWER_MODE_DRIVE));

    assert(App_PowerPathApply(APP_POWER_MODE_OFF, RecordWrite, &log));
    assert(log.count == 2U);
    ExpectEvent(&log, 0U, APP_OUTPUT_TARGET_UVLO, false);
    ExpectEvent(&log, 1U, APP_OUTPUT_TARGET_MP4317, false);

    log = (PowerEventLog){0};
    assert(App_PowerPathApply(APP_POWER_MODE_CHARGE, RecordWrite, &log));
    assert(log.count == 3U);
    ExpectEvent(&log, 0U, APP_OUTPUT_TARGET_UVLO, false);
    ExpectEvent(&log, 1U, APP_OUTPUT_TARGET_MP4317, false);
    ExpectEvent(&log, 2U, APP_OUTPUT_TARGET_UVLO, true);

    log = (PowerEventLog){0};
    assert(App_PowerPathApply(APP_POWER_MODE_DRIVE, RecordWrite, &log));
    assert(log.count == 3U);
    ExpectEvent(&log, 0U, APP_OUTPUT_TARGET_UVLO, false);
    ExpectEvent(&log, 1U, APP_OUTPUT_TARGET_MP4317, false);
    ExpectEvent(&log, 2U, APP_OUTPUT_TARGET_MP4317, true);

    assert(!App_PowerPathApply((AppPowerMode)99, RecordWrite, &log));
    assert(!App_PowerPathApply(APP_POWER_MODE_OFF, NULL, &log));

    return 0;
}
