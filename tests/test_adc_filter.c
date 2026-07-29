#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "app_adc_filter.h"

int main(void)
{
    AppAdcRollingMean filter = {0};
    AppAdcRollingMean maximum = {0};
    size_t index;

    assert(App_AdcRollingMeanPush(&filter, 100U) == 100U);
    assert(App_AdcRollingMeanPush(&filter, 200U) == 150U);
    assert(App_AdcRollingMeanPush(&filter, 300U) == 200U);
    assert(App_AdcRollingMeanPush(&filter, 400U) == 250U);
    assert(App_AdcRollingMeanPush(&filter, 500U) == 300U);
    assert(filter.sum == 1500U);
    assert(filter.count == APP_ADC_ROLLING_WINDOW_SIZE);

    assert(App_AdcRollingMeanPush(&filter, 600U) == 400U);
    assert(filter.sum == 2000U);
    assert(App_AdcRollingMeanPush(&filter, 700U) == 500U);
    assert(filter.sum == 2500U);

    for (index = 0U; index < APP_ADC_ROLLING_WINDOW_SIZE; ++index)
    {
        assert(App_AdcRollingMeanPush(&maximum, 4095U) == 4095U);
    }
    assert(maximum.sum == 20475U);

    return 0;
}
