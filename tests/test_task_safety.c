#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "app_task_safety.h"

typedef struct
{
    char events[4];
    size_t count;
    bool setter_result;
} TestLog;

static bool RecordState(bool active, void *context)
{
    TestLog *log = context;
    log->events[log->count++] = active ? 'T' : 'C';
    return log->setter_result;
}

static void RecordOutput(void *context)
{
    TestLog *log = context;
    log->events[log->count++] = 'O';
}

static void RecordMotor(void *context)
{
    TestLog *log = context;
    log->events[log->count++] = 'M';
}

static void TestTripAlwaysDispatchesStopsInOrder(void)
{
    TestLog log = {.setter_result = false};
    AppTaskSafetyCallbacks callbacks = {
        .set_thermal_state = RecordState,
        .request_output_stop = RecordOutput,
        .request_motor_sleep = RecordMotor,
        .context = &log,
    };

    App_TaskSafetyHandleThermalTransition(APP_THERMAL_TRIPPED, &callbacks);
    assert(log.count == 3U);
    assert(log.events[0] == 'T');
    assert(log.events[1] == 'O');
    assert(log.events[2] == 'M');
}

static void TestClearOnlyUpdatesState(void)
{
    TestLog log = {.setter_result = true};
    AppTaskSafetyCallbacks callbacks = {
        .set_thermal_state = RecordState,
        .request_output_stop = RecordOutput,
        .request_motor_sleep = RecordMotor,
        .context = &log,
    };

    App_TaskSafetyHandleThermalTransition(APP_THERMAL_CLEARED, &callbacks);
    assert(log.count == 1U);
    assert(log.events[0] == 'C');
}

static void TestTripDispatchesStopsWithoutAStateWriter(void)
{
    TestLog log = {0};
    AppTaskSafetyCallbacks callbacks = {
        .set_thermal_state = NULL,
        .request_output_stop = RecordOutput,
        .request_motor_sleep = RecordMotor,
        .context = &log,
    };

    App_TaskSafetyHandleThermalTransition(APP_THERMAL_TRIPPED, &callbacks);
    assert(log.count == 2U);
    assert(log.events[0] == 'O');
    assert(log.events[1] == 'M');
}

static void TestConsumerGuardsFailSafe(void)
{
    assert(App_TaskSafetyRequiresForcedSafe(false, false));
    assert(App_TaskSafetyRequiresForcedSafe(true, true));
    assert(!App_TaskSafetyRequiresForcedSafe(true, false));

    assert(!App_TaskSafetyAllowsPower(false, false, APP_POWER_MODE_CHARGE));
    assert(App_TaskSafetyAllowsPower(false, false, APP_POWER_MODE_OFF));
    assert(!App_TaskSafetyAllowsPower(true, true, APP_POWER_MODE_DRIVE));
    assert(App_TaskSafetyAllowsPower(true, false, APP_POWER_MODE_DRIVE));

    assert(!App_TaskSafetyAllowsOutput(false, false, true));
    assert(App_TaskSafetyAllowsOutput(false, false, false));
    assert(!App_TaskSafetyAllowsOutput(true, true, true));

    assert(!App_TaskSafetyAllowsMotor(false, false, APP_MOTOR_MODE_FORWARD));
    assert(App_TaskSafetyAllowsMotor(false, false, APP_MOTOR_MODE_SLEEP));
    assert(!App_TaskSafetyAllowsMotor(true, true, APP_MOTOR_MODE_WAKE));
    assert(App_TaskSafetyAllowsMotor(true, false, APP_MOTOR_MODE_FORWARD));
}

int main(void)
{
    TestTripAlwaysDispatchesStopsInOrder();
    TestClearOnlyUpdatesState();
    TestTripDispatchesStopsWithoutAStateWriter();
    TestConsumerGuardsFailSafe();
    return 0;
}
