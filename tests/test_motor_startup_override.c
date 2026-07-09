#include <assert.h>
#include <stdbool.h>

#include "app_at_protocol.h"

#ifndef APP_MOTOR_TEST_EXPECT_ENABLED
#define APP_MOTOR_TEST_EXPECT_ENABLED 0
#endif

#ifndef APP_MOTOR_TEST_EXPECT_MODE
#define APP_MOTOR_TEST_EXPECT_MODE APP_MOTOR_MODE_SLEEP
#endif

bool App_MotorGetStartupOverrideMode(AppMotorMode *mode);

int main(void)
{
    AppMotorMode mode = APP_MOTOR_MODE_SLEEP;
    bool enabled = App_MotorGetStartupOverrideMode(&mode);

    assert(enabled == (APP_MOTOR_TEST_EXPECT_ENABLED != 0));

    if (enabled)
    {
        assert(mode == APP_MOTOR_TEST_EXPECT_MODE);
    }

    return 0;
}