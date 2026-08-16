#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_motor.h"
#include "app_stall_config_service.h"

static bool is_motor_status_available;
static AppMotorMode motor_mode;
static bool is_current_available;
static uint32_t active_current_ma;
static bool should_store;
static bool should_set_state;
static uint32_t store_call_count;
static uint32_t set_state_call_count;

bool App_StateTryGetMotorStatus(AppMotorStatus *status)
{
    if (!is_motor_status_available || (status == NULL))
    {
        return false;
    }

    status->mode = motor_mode;
    return true;
}

bool App_StateTryGetStallCurrentMa(uint32_t *current_ma)
{
    if (!is_current_available || (current_ma == NULL))
    {
        return false;
    }

    *current_ma = active_current_ma;
    return true;
}

bool App_StateSetStallCurrentMa(uint32_t current_ma)
{
    set_state_call_count++;
    if (!should_set_state)
    {
        return false;
    }

    active_current_ma = current_ma;
    return true;
}

bool App_StallConfigStoreCurrentMa(uint32_t current_ma)
{
    (void)current_ma;
    store_call_count++;
    return should_store;
}

static void ResetFakes(void)
{
    is_motor_status_available = true;
    motor_mode = APP_MOTOR_MODE_STOP;
    is_current_available = true;
    active_current_ma = 4000U;
    should_store = true;
    should_set_state = true;
    store_call_count = 0U;
    set_state_call_count = 0U;
}

static void TestUnavailableMotorStateReturnsBusy(void)
{
    ResetFakes();
    is_motor_status_available = false;

    assert(App_StallConfigServiceSetCurrentMa(4200U)
           == APP_STALL_CONFIG_SET_STATE_BUSY);
    assert(store_call_count == 0U);
}

static void TestRunningMotorIsRejected(void)
{
    ResetFakes();
    motor_mode = APP_MOTOR_MODE_FORWARD;
    assert(App_StallConfigServiceSetCurrentMa(4200U)
           == APP_STALL_CONFIG_SET_MOTOR_RUNNING);

    motor_mode = APP_MOTOR_MODE_REVERSE;
    assert(App_StallConfigServiceSetCurrentMa(4200U)
           == APP_STALL_CONFIG_SET_MOTOR_RUNNING);
    assert(store_call_count == 0U);
}

static void TestUnavailableActiveThresholdReturnsBusy(void)
{
    ResetFakes();
    is_current_available = false;

    assert(App_StallConfigServiceSetCurrentMa(4200U)
           == APP_STALL_CONFIG_SET_STATE_BUSY);
    assert(store_call_count == 0U);
}

static void TestSameValueSkipsFlashAndStateWrites(void)
{
    ResetFakes();

    assert(App_StallConfigServiceSetCurrentMa(4000U)
           == APP_STALL_CONFIG_SET_OK);
    assert(store_call_count == 0U);
    assert(set_state_call_count == 0U);
}

static void TestFlashFailureLeavesStateUnchanged(void)
{
    ResetFakes();
    should_store = false;

    assert(App_StallConfigServiceSetCurrentMa(4200U)
           == APP_STALL_CONFIG_SET_FLASH_WRITE_FAILED);
    assert(store_call_count == 1U);
    assert(set_state_call_count == 0U);
    assert(active_current_ma == 4000U);
}

static void TestSuccessfulWritePublishesNewState(void)
{
    ResetFakes();

    assert(App_StallConfigServiceSetCurrentMa(4200U)
           == APP_STALL_CONFIG_SET_OK);
    assert(store_call_count == 1U);
    assert(set_state_call_count == 1U);
    assert(active_current_ma == 4200U);
}

static void TestStatePublicationRetriesThenReportsBusy(void)
{
    ResetFakes();
    should_set_state = false;

    assert(App_StallConfigServiceSetCurrentMa(4200U)
           == APP_STALL_CONFIG_SET_STATE_BUSY);
    assert(store_call_count == 1U);
    assert(set_state_call_count == 2U);
    assert(active_current_ma == 4000U);
}

int main(void)
{
    TestUnavailableMotorStateReturnsBusy();
    TestRunningMotorIsRejected();
    TestUnavailableActiveThresholdReturnsBusy();
    TestSameValueSkipsFlashAndStateWrites();
    TestFlashFailureLeavesStateUnchanged();
    TestSuccessfulWritePublishesNewState();
    TestStatePublicationRetriesThenReportsBusy();
    puts("OK: stall configuration service behavior verified.");
    return 0;
}
