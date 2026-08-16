#include <assert.h>
#include <stdbool.h>

#include "app_at_command_guard.h"

static AppAtCommand MakePowerCommand(AppPowerMode mode)
{
    AppAtCommand command = {0};
    command.type = APP_AT_COMMAND_SET_POWER_MODE;
    command.data.power.mode = mode;
    return command;
}

static AppAtCommand MakeOutputCommand(AppAtCommandType type, bool enabled)
{
    AppAtCommand command = {0};
    command.type = type;
    command.data.output.enabled = enabled;
    return command;
}

static AppAtCommand MakeMotorCommand(AppMotorMode mode)
{
    AppAtCommand command = {0};
    command.type = APP_AT_COMMAND_SET_MOTOR_MODE;
    command.data.motor.mode = mode;
    return command;
}

static void TestThermalProtectionBlocksUnsafeStarts(void)
{
    AppAtCommand charge = MakePowerCommand(APP_POWER_MODE_CHARGE);
    AppAtCommand drive = MakePowerCommand(APP_POWER_MODE_DRIVE);
    AppAtCommand nmos1_on = MakeOutputCommand(APP_AT_COMMAND_SET_NMOS1, true);
    AppAtCommand nmos2_on = MakeOutputCommand(APP_AT_COMMAND_SET_NMOS2, true);
    AppAtCommand motor_wake = MakeMotorCommand(APP_MOTOR_MODE_WAKE);
    AppAtCommand motor_stop = MakeMotorCommand(APP_MOTOR_MODE_STOP);

    assert(App_AtCommandGuardEvaluate(&charge, true, true)
           == APP_AT_COMMAND_GUARD_OVER_TEMPERATURE);
    assert(App_AtCommandGuardEvaluate(&drive, true, true)
           == APP_AT_COMMAND_GUARD_OVER_TEMPERATURE);
    assert(App_AtCommandGuardEvaluate(&nmos1_on, true, true)
           == APP_AT_COMMAND_GUARD_OVER_TEMPERATURE);
    assert(App_AtCommandGuardEvaluate(&nmos2_on, true, true)
           == APP_AT_COMMAND_GUARD_OVER_TEMPERATURE);
    assert(App_AtCommandGuardEvaluate(&motor_wake, true, true)
           == APP_AT_COMMAND_GUARD_OVER_TEMPERATURE);
    assert(App_AtCommandGuardEvaluate(&motor_stop, true, true)
           == APP_AT_COMMAND_GUARD_OVER_TEMPERATURE);
}

static void TestThermalProtectionAllowsSafeCommands(void)
{
    AppAtCommand power_off = MakePowerCommand(APP_POWER_MODE_OFF);
    AppAtCommand nmos1_off = MakeOutputCommand(APP_AT_COMMAND_SET_NMOS1, false);
    AppAtCommand nmos2_off = MakeOutputCommand(APP_AT_COMMAND_SET_NMOS2, false);
    AppAtCommand motor_sleep = MakeMotorCommand(APP_MOTOR_MODE_SLEEP);
    AppAtCommand charge_time = { .type = APP_AT_COMMAND_SET_CHARGE_TIME };
    AppAtCommand query = { .type = APP_AT_COMMAND_QUERY_CHARGE_TIME };
    AppAtCommand stall_current = {
        .type = APP_AT_COMMAND_SET_STALL_CURRENT,
    };
    AppAtCommand stall_query = {
        .type = APP_AT_COMMAND_QUERY_STALL_CURRENT,
    };

    assert(App_AtCommandGuardEvaluate(&power_off, true, true)
           == APP_AT_COMMAND_GUARD_ALLOW);
    assert(App_AtCommandGuardEvaluate(&nmos1_off, true, true)
           == APP_AT_COMMAND_GUARD_ALLOW);
    assert(App_AtCommandGuardEvaluate(&nmos2_off, true, true)
           == APP_AT_COMMAND_GUARD_ALLOW);
    assert(App_AtCommandGuardEvaluate(&motor_sleep, true, true)
           == APP_AT_COMMAND_GUARD_ALLOW);
    assert(App_AtCommandGuardEvaluate(&charge_time, false, true)
           == APP_AT_COMMAND_GUARD_ALLOW);
    assert(App_AtCommandGuardEvaluate(&query, false, true)
           == APP_AT_COMMAND_GUARD_ALLOW);
    assert(App_AtCommandGuardEvaluate(&stall_current, false, true)
           == APP_AT_COMMAND_GUARD_ALLOW);
    assert(App_AtCommandGuardEvaluate(&stall_query, false, true)
           == APP_AT_COMMAND_GUARD_ALLOW);
}

static void TestUnsafeStartNeedsReadableThermalState(void)
{
    AppAtCommand charge = MakePowerCommand(APP_POWER_MODE_CHARGE);
    AppAtCommand motor_wake = MakeMotorCommand(APP_MOTOR_MODE_WAKE);

    assert(App_AtCommandGuardEvaluate(&charge, false, false)
           == APP_AT_COMMAND_GUARD_STATE_BUSY);
    assert(App_AtCommandGuardEvaluate(&motor_wake, false, false)
           == APP_AT_COMMAND_GUARD_STATE_BUSY);
    assert(App_AtCommandGuardEvaluate(&charge, true, false)
           == APP_AT_COMMAND_GUARD_ALLOW);
}

int main(void)
{
    TestThermalProtectionBlocksUnsafeStarts();
    TestThermalProtectionAllowsSafeCommands();
    TestUnsafeStartNeedsReadableThermalState();
    return 0;
}
