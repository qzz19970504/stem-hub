#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "app_adc_filter.h"

int main(void)
{
    AppAdcRollingMean filter = {0};
    AppAdcRollingMean maximum = {0};
    AppAdcRollingMean cycle_filters[APP_ADC_ROLLING_CHANNEL_COUNT] = {0};
    const uint16_t partial_cycle[] = {100U, 200U, 300U, 400U};
    const uint16_t complete_cycle[] = {100U, 200U, 300U, 400U, 500U};
    uint16_t cycle_means[APP_ADC_ROLLING_CHANNEL_COUNT] = {0};
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

    assert(!App_AdcRollingMeanPushCycle(cycle_filters,
                                        partial_cycle,
                                        4U,
                                        cycle_means));
    for (index = 0U; index < APP_ADC_ROLLING_CHANNEL_COUNT; ++index)
    {
        assert(cycle_filters[index].count == 0U);
    }

    assert(App_AdcRollingMeanPushCycle(cycle_filters,
                                       complete_cycle,
                                       APP_ADC_ROLLING_CHANNEL_COUNT,
                                       cycle_means));
    for (index = 0U; index < APP_ADC_ROLLING_CHANNEL_COUNT; ++index)
    {
        assert(cycle_filters[index].count == 1U);
        assert(cycle_means[index] == complete_cycle[index]);
    }

    return 0;
}
