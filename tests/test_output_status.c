#include <assert.h>
#include <stdbool.h>
#include <string.h>

#include "app_output_status.h"

static void TestFormatsCompleteOutputState(void)
{
    char response[192];
    AppIoStatus status = {
        .power_mode = APP_POWER_MODE_CHARGE,
        .charge_phase = APP_CHARGE_PHASE_OFF,
        .led_master_enabled = true,
        .nmos1_enabled = false,
        .nmos2_enabled = true,
        .motor_bypass_enabled = false,
        .charge_bypass_enabled = true,
    };

    assert(App_OutputStatusFormat(response, sizeof(response), &status));
    assert(strcmp(
        response,
        "+OUTPUT:POWER=CHARGE,CHARGE_PHASE=OFF,NMOS1=0,NMOS2=1,"
        "LIGHTS=1,MOTOR_BYPASS=0,CHARGE_BYPASS=1\r\nOK\r\n") == 0);
}

static void TestRejectsInvalidArgumentsAndShortBuffers(void)
{
    char response[8];
    AppIoStatus status = {0};

    assert(!App_OutputStatusFormat(NULL, sizeof(response), &status));
    assert(!App_OutputStatusFormat(response, sizeof(response), NULL));
    assert(!App_OutputStatusFormat(response, sizeof(response), &status));
}

static void TestFormatsEveryPowerAndPhaseName(void)
{
    assert(strcmp(App_OutputStatusPowerModeName(APP_POWER_MODE_OFF), "OFF") == 0);
    assert(strcmp(App_OutputStatusPowerModeName(APP_POWER_MODE_DRIVE), "DRIVE") == 0);
    assert(strcmp(App_OutputStatusChargePhaseName(APP_CHARGE_PHASE_IDLE), "IDLE") == 0);
    assert(strcmp(App_OutputStatusChargePhaseName(APP_CHARGE_PHASE_ON), "ON") == 0);
}

int main(void)
{
    TestFormatsCompleteOutputState();
    TestRejectsInvalidArgumentsAndShortBuffers();
    TestFormatsEveryPowerAndPhaseName();
    return 0;
}
