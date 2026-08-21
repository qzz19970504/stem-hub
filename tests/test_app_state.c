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
    uint32_t seconds = 0U;
    uint32_t current_ma = 0U;
    bool is_active = false;

    assert(!App_StateTryGetChargeOnTimeSeconds(NULL));
    assert(!App_StateTryGetStallCurrentMa(NULL));
    assert(!App_StateTryGetThermalProtectionActive(NULL));

    state_mutex_available = false;
    assert(!App_StateTryGetChargeOnTimeSeconds(&seconds));
    assert(!App_StateTryGetStallCurrentMa(&current_ma));
    assert(!App_StateTryGetThermalProtectionActive(&is_active));
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

static void TestBridgeTargetSelectionReplacesPreviousState(void)
{
    bool uart2_enabled = false;
    bool uart3_enabled = false;

    App_StateSelectBridgeTarget(APP_BRIDGE_TARGET_UART2);
    App_StateGetBridgeEnabled(&uart2_enabled, &uart3_enabled);
    assert(uart2_enabled);
    assert(!uart3_enabled);

    App_StateSelectBridgeTarget(APP_BRIDGE_TARGET_UART3);
    App_StateGetBridgeEnabled(&uart2_enabled, &uart3_enabled);
    assert(!uart2_enabled);
    assert(uart3_enabled);

    App_StateSelectBridgeTarget(APP_BRIDGE_TARGET_UART23);
    App_StateGetBridgeEnabled(&uart2_enabled, &uart3_enabled);
    assert(uart2_enabled);
    assert(uart3_enabled);

    App_StateClearBridgeTarget();
    App_StateGetBridgeEnabled(&uart2_enabled, &uart3_enabled);
    assert(!uart2_enabled);
    assert(!uart3_enabled);
}

int main(void)
{
    TestChargeTimeDefaultsToTenSeconds();
    TestChargeTimeAcceptsConfiguredRange();
    TestChargeTimeRejectsOutOfRangeWithoutChangingState();
    TestThermalProtectionDefaultsInactiveAndCanChange();
    TestStallCurrentDefaultsToFourAmps();
    TestStallCurrentAcceptsConfiguredRange();
    TestStallCurrentRejectsOutOfRangeWithoutChangingState();
    TestTryGetReportsInvalidOutputAndBusyState();
    TestMissingStateMutexFailsSafely();
    TestBridgeTargetSelectionReplacesPreviousState();
    return 0;
}
