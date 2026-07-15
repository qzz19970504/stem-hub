#include "app_sensor.h"

#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "app_runtime.h"
#include "app_state.h"

static int32_t App_SensorConvertBatteryNtc(uint32_t millivolts)
{
    return (int32_t)millivolts;
}

static int32_t App_SensorConvertBatteryVoltage(uint32_t millivolts)
{
    /* millivolts 是 ADC 引脚上的电压（即 R_BOTTOM 上的压降），
     * 通过分压比换算出电池实际电压，单位仍为 mV。
     * VBAT = Vadc * (R_TOP + R_BOTTOM) / R_BOTTOM */
    uint32_t ratio_num = APP_BATT_VOLTAGE_DIVIDER_R_TOP_OHMS + APP_BATT_VOLTAGE_DIVIDER_R_BOTTOM_OHMS;
    uint32_t ratio_den = APP_BATT_VOLTAGE_DIVIDER_R_BOTTOM_OHMS;
    uint64_t battery_mv = ((uint64_t)millivolts * ratio_num) / ratio_den;

    if (battery_mv > (uint32_t)INT32_MAX)
    {
        return INT32_MAX;
    }

    return (int32_t)battery_mv;
}

static int32_t App_SensorConvertNtcTemperature(uint32_t millivolts)
{
    return (int32_t)millivolts;
}

static void App_SensorUpdateMeasure(AppAnalogMeasure *measure,
                                    uint16_t raw,
                                    int32_t (*convert_fn)(uint32_t))
{
    measure->raw = raw;
    measure->millivolts = App_RuntimeRawToMillivolts(raw);
    measure->physical_value = convert_fn(measure->millivolts);
}

bool App_SensorTryGetSnapshot(AppSensorSnapshot *snapshot)
{
    return App_StateTryGetSensorSnapshot(snapshot);
}

void App_SensorTask(void *argument)
{
    AppSensorSnapshot next_snapshot;
    uint16_t raw = 0U;
    bool success = false;

    (void)argument;

    for (;;)
    {
        (void)memset(&next_snapshot, 0, sizeof(next_snapshot));
        success = App_RuntimeReadChannel(&hadc1, ADC_CHANNEL_4, &raw);
        if (success)
        {
            App_SensorUpdateMeasure(&next_snapshot.battery_ntc, raw, App_SensorConvertBatteryNtc);
        }

        if (success)
        {
            success = App_RuntimeReadChannel(&hadc1, ADC_CHANNEL_5, &raw);
            if (success)
            {
                App_SensorUpdateMeasure(&next_snapshot.battery_voltage, raw, App_SensorConvertBatteryVoltage);
            }
        }

        if (success)
        {
            success = App_RuntimeReadAdc2Channel(ADC_CHANNEL_6, &raw);
            if (success)
            {
                App_SensorUpdateMeasure(&next_snapshot.ntc3, raw, App_SensorConvertNtcTemperature);
            }
        }

        if (success)
        {
            success = App_RuntimeReadAdc2Channel(ADC_CHANNEL_7, &raw);
            if (success)
            {
                App_SensorUpdateMeasure(&next_snapshot.ntc2, raw, App_SensorConvertNtcTemperature);
            }
        }

        if (success)
        {
            success = App_RuntimeReadAdc2Channel(ADC_CHANNEL_9, &raw);
            if (success)
            {
                App_SensorUpdateMeasure(&next_snapshot.ntc1, raw, App_SensorConvertNtcTemperature);
            }
        }

        if (success)
        {
            App_StatePublishSensorSnapshot(&next_snapshot, osKernelGetTickCount());
        }

        osDelay(APP_SENSOR_PERIOD_MS);
    }
}