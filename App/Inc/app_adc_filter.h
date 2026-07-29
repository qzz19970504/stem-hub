#ifndef APP_ADC_FILTER_H
#define APP_ADC_FILTER_H

#include <stdint.h>

#define APP_ADC_ROLLING_WINDOW_SIZE 5U

typedef struct
{
    uint16_t samples[APP_ADC_ROLLING_WINDOW_SIZE];
    uint32_t sum;
    uint8_t next_index;
    uint8_t count;
} AppAdcRollingMean;

uint16_t App_AdcRollingMeanPush(AppAdcRollingMean *filter, uint16_t sample);

#endif
