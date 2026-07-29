#include "app_adc_filter.h"

uint16_t App_AdcRollingMeanPush(AppAdcRollingMean *filter, uint16_t sample)
{
    if (filter->count == APP_ADC_ROLLING_WINDOW_SIZE)
    {
        filter->sum -= filter->samples[filter->next_index];
    }
    else
    {
        ++filter->count;
    }

    filter->samples[filter->next_index] = sample;
    filter->sum += sample;
    filter->next_index = (uint8_t)((filter->next_index + 1U)
                                   % APP_ADC_ROLLING_WINDOW_SIZE);

    return (uint16_t)(filter->sum / filter->count);
}
