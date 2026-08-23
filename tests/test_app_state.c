#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "app_runtime.h"
#include "app_state.h"

static int state_mutex_token;
static int sensor_mutex_token;
static int sensor_ready_semaphore_token;
static bool state_mutex_available = true;

AppRuntime g_app_runtime = {
    .sensor_ready_semaphore = &sensor_ready_semaphore_token,
    .sensor_mutex = &sensor_mutex_token,
    .state_mutex = &state_mutex_token,
};

osStatus_t osMutexAcquire(osMutexId_t mutex_id, uint32_t timeout)
{
    (void)timeout;
    if (mutex_id == NULL)
    {
        return osError;
    }
    if ((mutex_id == g_app_runtime.state_mutex) && !state_mutex_available)
    {
        return osError;
    }
    return osOK;
}

osStatus_t osMutexRelease(osMutexId_t mutex_id)
{
    (void)mutex_id;
    return osOK;
}

osStatus_t osSemaphoreAcquire(osSemaphoreId_t semaphore_id, uint32_t timeout)
{
    (void)semaphore_id;
    (void)timeout;
    return osError;
}

osStatus_t osSemaphoreRelease(osSemaphoreId_t semaphore_id)
{
    (void)semaphore_id;
    return osOK;
}

uint32_t osSemaphoreGetCount(osSemaphoreId_t semaphore_id)
{
    (void)semaphore_id;
    return 0U;
}

static void TestResistorBypassesDefaultDisabled(void)
{
    AppIoStatus io_status = {0};

    assert(App_StateTryGetIoStatus(&io_status));
    assert(!io_status.motor_bypass_enabled);
    assert(!io_status.charge_bypass_enabled);
}

static void TestResistorBypassAppliedStateCanChange(void)
{
    AppIoStatus io_status = {0};

    App_StateSetMotorBypassEnabled(true);
    App_StateSetOutputEnabled(APP_OUTPUT_TARGET_CHARGE_BYPASS, true);

    assert(App_StateTryGetIoStatus(&io_status));
    assert(io_status.motor_bypass_enabled);
    assert(io_status.charge_bypass_enabled);

    App_StateSetMotorBypassEnabled(false);
    App_StateSetOutputEnabled(APP_OUTPUT_TARGET_CHARGE_BYPASS, false);
}

static void TestOwnerRequestsCarryBypassState(void)
{
    AppMotorRequest motor_request = {
        .type = APP_MOTOR_REQUEST_SET_BYPASS,
        .data.bypass_enabled = true,
    };
    AppOutputRequest output_request = {
        .type = APP_OUTPUT_REQUEST_SET_CHARGE_BYPASS,
        .data.charge_bypass_enabled = true,
    };

    assert(motor_request.type == APP_MOTOR_REQUEST_SET_BYPASS);
    assert(motor_request.data.bypass_enabled);
    assert(output_request.type == APP_OUTPUT_REQUEST_SET_CHARGE_BYPASS);
    assert(output_request.data.charge_bypass_enabled);
}

static void TestChargeTimeDefaultsToTenSeconds(void)
{
    uint32_t seconds = 0U;

    assert(App_StateTryGetChargeOnTimeSeconds(&seconds));
    assert(seconds == APP_CHARGE_DEFAULT_ON_TIME_SECONDS);
}

static void TestChargeTimeAcceptsConfiguredRange(void)
{
    uint32_t seconds = 0U;

    assert(App_StateSetChargeOnTimeSeconds(APP_CHARGE_MIN_ON_TIME_SECONDS));
    assert(App_StateTryGetChargeOnTimeSeconds(&seconds));
    assert(seconds == APP_CHARGE_MIN_ON_TIME_SECONDS);

    assert(App_StateSetChargeOnTimeSeconds(APP_CHARGE_MAX_ON_TIME_SECONDS));
    assert(App_StateTryGetChargeOnTimeSeconds(&seconds));
    assert(seconds == APP_CHARGE_MAX_ON_TIME_SECONDS);
}

static void TestChargeTimeRejectsOutOfRangeWithoutChangingState(void)
{
    uint32_t seconds = 0U;

    assert(App_StateSetChargeOnTimeSeconds(APP_CHARGE_DEFAULT_ON_TIME_SECONDS));
    assert(!App_StateSetChargeOnTimeSeconds(APP_CHARGE_MIN_ON_TIME_SECONDS - 1U));
    assert(!App_StateSetChargeOnTimeSeconds(APP_CHARGE_MAX_ON_TIME_SECONDS + 1U));
    assert(App_StateTryGetChargeOnTimeSeconds(&seconds));
    assert(seconds == APP_CHARGE_DEFAULT_ON_TIME_SECONDS);
}

static void TestThermalProtectionDefaultsInactiveAndCanChange(void)
{
    bool is_active = true;

    assert(App_StateTryGetThermalProtectionActive(&is_active));
    assert(!is_active);

    assert(App_StateSetThermalProtectionActive(true));
    assert(App_StateTryGetThermalProtectionActive(&is_active));
    assert(is_active);

    assert(App_StateSetThermalProtectionActive(false));
    assert(App_StateTryGetThermalProtectionActive(&is_active));
    assert(!is_active);
}

static void TestStallCurrentDefaultsToFourAmps(void)
{
    uint32_t current_ma = 0U;

    assert(App_StateTryGetStallCurrentMa(&current_ma));
    assert(current_ma == APP_MOTOR_STALL_DEFAULT_CURRENT_MA);
}

static void TestStallCurrentAcceptsConfiguredRange(void)
{
    uint32_t current_ma = 0U;

    assert(App_StateSetStallCurrentMa(APP_MOTOR_STALL_MIN_CURRENT_MA));
    assert(App_StateTryGetStallCurrentMa(&current_ma));
    assert(current_ma == APP_MOTOR_STALL_MIN_CURRENT_MA);

    assert(App_StateSetStallCurrentMa(APP_MOTOR_STALL_MAX_CURRENT_MA));
    assert(App_StateTryGetStallCurrentMa(&current_ma));
    assert(current_ma == APP_MOTOR_STALL_MAX_CURRENT_MA);
}

static void TestStallCurrentRejectsOutOfRangeWithoutChangingState(void)
{
    uint32_t current_ma = 0U;

    assert(App_StateSetStallCurrentMa(APP_MOTOR_STALL_DEFAULT_CURRENT_MA));
    assert(!App_StateSetStallCurrentMa(APP_MOTOR_STALL_MIN_CURRENT_MA - 1U));
    assert(!App_StateSetStallCurrentMa(APP_MOTOR_STALL_MAX_CURRENT_MA + 1U));
    assert(App_StateTryGetStallCurrentMa(&current_ma));
    assert(current_ma == APP_MOTOR_STALL_DEFAULT_CURRENT_MA);
}

static void TestTryGetReportsInvalidOutputAndBusyState(void)
{
    AppIoStatus io_status = {0};
    uint32_t seconds = 0U;
    uint32_t current_ma = 0U;
    bool is_active = false;

    assert(!App_StateTryGetChargeOnTimeSeconds(NULL));
    assert(!App_StateTryGetStallCurrentMa(NULL));
    assert(!App_StateTryGetThermalProtectionActive(NULL));
    assert(!App_StateTryGetIoStatus(NULL));

    state_mutex_available = false;
    assert(!App_StateTryGetChargeOnTimeSeconds(&seconds));
    assert(!App_StateTryGetStallCurrentMa(&current_ma));
    assert(!App_StateTryGetThermalProtectionActive(&is_active));
    assert(!App_StateTryGetIoStatus(&io_status));
    assert(!App_StateSetChargeOnTimeSeconds(APP_CHARGE_DEFAULT_ON_TIME_SECONDS));
    assert(!App_StateSetStallCurrentMa(APP_MOTOR_STALL_DEFAULT_CURRENT_MA));
    assert(!App_StateSetThermalProtectionActive(true));
    state_mutex_available = true;
}

static void TestMissingStateMutexFailsSafely(void)
{
    osMutexId_t state_mutex = g_app_runtime.state_mutex;
    bool is_active = false;

    g_app_runtime.state_mutex = NULL;
    assert(!App_StateSetThermalProtectionActive(true));
    assert(!App_StateTryGetThermalProtectionActive(&is_active));
    g_app_runtime.state_mutex = state_mutex;
}

int main(void)
{
    TestResistorBypassesDefaultDisabled();
    TestResistorBypassAppliedStateCanChange();
    TestOwnerRequestsCarryBypassState();
    TestChargeTimeDefaultsToTenSeconds();
    TestChargeTimeAcceptsConfiguredRange();
    TestChargeTimeRejectsOutOfRangeWithoutChangingState();
    TestThermalProtectionDefaultsInactiveAndCanChange();
    TestStallCurrentDefaultsToFourAmps();
    TestStallCurrentAcceptsConfiguredRange();
    TestStallCurrentRejectsOutOfRangeWithoutChangingState();
    TestTryGetReportsInvalidOutputAndBusyState();
    TestMissingStateMutexFailsSafely();
    return 0;
}
