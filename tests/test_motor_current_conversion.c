#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "app_motor_current.h"

static void TestCurrentConversionUsesFitted220OhmResistor(void)
{
    assert(App_MotorCurrentFromMillivolts(0U) == 0U);
    assert(App_MotorCurrentFromMillivolts(50U) == 505U);
    assert(App_MotorCurrentFromMillivolts(317U) == 3202U);
    assert(App_MotorCurrentFromMillivolts(1881U) == 19000U);
    assert(App_MotorCurrentFromMillivolts(3300U) == 33333U);
}

static void TestDeciAmpConversionRoundsAndClampsAtAdcRange(void)
{
    assert(App_MotorCurrentToDeciAmps(0U) == 0U);
    assert(App_MotorCurrentToDeciAmps(49U) == 0U);
    assert(App_MotorCurrentToDeciAmps(50U) == 1U);
    assert(App_MotorCurrentToDeciAmps(3202U) == 32U);
    assert(App_MotorCurrentToDeciAmps(33333U) == 333U);
    assert(App_MotorCurrentToDeciAmps(40000U) == 333U);
}

int main(void)
{
    TestCurrentConversionUsesFitted220OhmResistor();
    TestDeciAmpConversionRoundsAndClampsAtAdcRange();
    puts("OK: 220 ohm motor current conversion verified.");
    return 0;
}
