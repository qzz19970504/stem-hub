#include <assert.h>
#include <stdbool.h>

#include "app_resistor_bypass.h"

static void TestMotorActivationRequiresRunningDirection(void)
{
    assert(!App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_SLEEP));
    assert(!App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_WAKE));
    assert(App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_FORWARD));
    assert(App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_REVERSE));
    assert(!App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_BRAKE));
    assert(!App_ResistorBypassMotorActivationAllowed(APP_MOTOR_MODE_STOP));
}

static void TestChargeActivationRequiresActualOutput(void)
{
    assert(!App_ResistorBypassChargeActivationAllowed(false));
    assert(App_ResistorBypassChargeActivationAllowed(true));
}

static void TestDisableRequestIsAlwaysAllowed(void)
{
    assert(App_ResistorBypassRequestAllowed(false, false));
    assert(App_ResistorBypassRequestAllowed(false, true));
}

static void TestEnableRequestUsesActivationPolicy(void)
{
    assert(!App_ResistorBypassRequestAllowed(true, false));
    assert(App_ResistorBypassRequestAllowed(true, true));
}

static void TestNonRunningMotorTransitionRequiresReset(void)
{
    assert(App_ResistorBypassMotorTransitionRequiresReset(
        APP_MOTOR_MODE_FORWARD,
        APP_MOTOR_MODE_STOP));
    assert(App_ResistorBypassMotorTransitionRequiresReset(
        APP_MOTOR_MODE_REVERSE,
        APP_MOTOR_MODE_SLEEP));
}

static void TestDirectionChangeRequiresReset(void)
{
    assert(App_ResistorBypassMotorTransitionRequiresReset(
        APP_MOTOR_MODE_FORWARD,
        APP_MOTOR_MODE_REVERSE));
    assert(App_ResistorBypassMotorTransitionRequiresReset(
        APP_MOTOR_MODE_REVERSE,
        APP_MOTOR_MODE_FORWARD));
}

static void TestRepeatedRunningDirectionKeepsAppliedBypass(void)
{
    assert(!App_ResistorBypassMotorTransitionRequiresReset(
        APP_MOTOR_MODE_FORWARD,
        APP_MOTOR_MODE_FORWARD));
    assert(!App_ResistorBypassMotorTransitionRequiresReset(
        APP_MOTOR_MODE_REVERSE,
        APP_MOTOR_MODE_REVERSE));
}

int main(void)
{
    TestMotorActivationRequiresRunningDirection();
    TestChargeActivationRequiresActualOutput();
    TestDisableRequestIsAlwaysAllowed();
    TestEnableRequestUsesActivationPolicy();
    TestNonRunningMotorTransitionRequiresReset();
    TestDirectionChangeRequiresReset();
    TestRepeatedRunningDirectionKeepsAppliedBypass();
    return 0;
}
