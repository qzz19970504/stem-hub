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

uint16_t App_AdcRollingMeanPreview(const AppAdcRollingMean *filter,
                                   uint16_t sample)
{
    AppAdcRollingMean preview;

    if (filter == NULL)
    {
        return sample;
    }

    preview = *filter;
    return App_AdcRollingMeanPush(&preview, sample);
}

bool App_AdcRollingMeanPushCycle(
    AppAdcRollingMean *filters,
    const uint16_t *samples,
    size_t sample_count,
    uint16_t *means)
{
    size_t index;

    if ((filters == NULL)
        || (samples == NULL)
        || (means == NULL)
        || (sample_count != APP_ADC_ROLLING_CHANNEL_COUNT))
    {
        return false;
    }

    for (index = 0U; index < APP_ADC_ROLLING_CHANNEL_COUNT; ++index)
    {
        means[index] = App_AdcRollingMeanPush(&filters[index], samples[index]);
    }

    return true;
}
