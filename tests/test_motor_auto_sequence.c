#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "app_at_protocol.h"

#ifndef APP_MOTOR_TEST_EXPECT_AUTO_ENABLED
#define APP_MOTOR_TEST_EXPECT_AUTO_ENABLED 0
#endif

#ifndef APP_MOTOR_TEST_EXPECT_STEP_DURATION_MS
#define APP_MOTOR_TEST_EXPECT_STEP_DURATION_MS 10000U
#endif

bool App_MotorIsAutoSequenceEnabled(void);
uint32_t App_MotorGetAutoSequenceStepDurationMs(void);
bool App_MotorGetAutoSequenceModeForStep(uint32_t step_index, AppMotorMode *mode);

int main(void)
{
    AppMotorMode mode = APP_MOTOR_MODE_SLEEP;

    assert(App_MotorIsAutoSequenceEnabled() == (APP_MOTOR_TEST_EXPECT_AUTO_ENABLED != 0));

    if (App_MotorIsAutoSequenceEnabled())
    {
        assert(App_MotorGetAutoSequenceStepDurationMs() == APP_MOTOR_TEST_EXPECT_STEP_DURATION_MS);
        assert(App_MotorGetAutoSequenceModeForStep(0U, &mode));
        assert(mode == APP_MOTOR_MODE_WAKE);
        assert(App_MotorGetAutoSequenceModeForStep(1U, &mode));
        assert(mode == APP_MOTOR_MODE_FORWARD);
        assert(App_MotorGetAutoSequenceModeForStep(2U, &mode));
        assert(mode == APP_MOTOR_MODE_BRAKE);
        assert(App_MotorGetAutoSequenceModeForStep(3U, &mode));
        assert(mode == APP_MOTOR_MODE_REVERSE);
        assert(App_MotorGetAutoSequenceModeForStep(4U, &mode));
        assert(mode == APP_MOTOR_MODE_SLEEP);
        assert(App_MotorGetAutoSequenceModeForStep(5U, &mode));
        assert(mode == APP_MOTOR_MODE_WAKE);
    }

    return 0;
}