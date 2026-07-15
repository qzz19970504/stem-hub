#include "app_sensor.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "app_ntc_table.h"
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

/* NTC 温度换算 (查表法):
 *  1) 拓扑 3V3 -- NTC -- Vadc -- 470Ω -- GND，先由 Vadc 反推 Rntc
 *  2) 在 R-T 表 (k_app_ntc_table_r_ohms) 中二分查找 + 线性插值得温度
 *  返回值: 0.1°C 分辨率的有符号整数 (例如 253 表示 25.3°C，-52 表示 -5.2°C)
 *  边界 (按物理意义):
 *    - Vadc == 0          (NTC 开路/虚焊): 钳位到 -400 (= -40.0°C, datasheet 下限)
 *    - Vadc >= V_SUPPLY   (NTC 短路):      返回 INT32_MAX 表示异常
 *    - Rntc 超出表范围   : 钳位到 datasheet 上下限
 *  备注: 不用 B 方程，因为 B=3450 是 25/85°C 线性化，在该区间外会算出
 *        方向相反/偏差 100°C 级别的错误 (datasheet 第 5-8 页 R-T 表为准)。*/
static int32_t App_SensorConvertNtcTemperature(uint32_t millivolts)
{
    if (millivolts == 0U)
    {
        return (int32_t)APP_NTC_TABLE_T_MIN_C * 10;
    }
    if (millivolts >= APP_NTC_V_SUPPLY_MV)
    {
        return INT32_MAX;
    }

    /* Rntc = R_series * (V_supply - Vadc) / Vadc，单位 Ω
     * 用 int64 防溢出 (V_supply=3300, R_series=470, 最大 Rntc ≈ 1.55MΩ) */
    int64_t numerator = (int64_t)APP_NTC_R_SERIES_OHMS *
                        ((int64_t)APP_NTC_V_SUPPLY_MV - (int64_t)millivolts);
    int64_t r_ntc = numerator / (int64_t)millivolts;
    if (r_ntc <= 0)
    {
        return INT32_MAX;
    }
    uint32_t r = (r_ntc > (int64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)r_ntc;

    /* 二分查找: 找 i 使 R[i] > r >= R[i+1] (表单调递减) */
    const uint32_t *table = k_app_ntc_table_r_ohms;
    if (r >= table[0])
    {
        return (int32_t)APP_NTC_TABLE_T_MIN_C * 10;
    }
    if (r <= table[APP_NTC_TABLE_SIZE - 1U])
    {
        return (int32_t)APP_NTC_TABLE_T_MAX_C * 10;
    }

    int lo = 0;
    int hi = (int)APP_NTC_TABLE_SIZE - 1;
    while (hi - lo > 1)
    {
        int mid = (lo + hi) / 2;
        if (table[mid] > r)
        {
            lo = mid;
        }
        else
        {
            hi = mid;
        }
    }

    /* 线性插值:
     *   t = T_lo + (T_hi - T_lo) * (R_lo - r) / (R_lo - R_hi) */
    int32_t t_lo_centi = ((int32_t)APP_NTC_TABLE_T_MIN_C + (int32_t)lo) * 10;
    int32_t t_hi_centi = ((int32_t)APP_NTC_TABLE_T_MIN_C + (int32_t)hi) * 10;
    uint32_t r_lo = table[lo];
    uint32_t r_hi = table[hi];
    if (r_lo == r_hi)
    {
        return t_lo_centi;
    }
    int64_t t_delta = (int64_t)(t_hi_centi - t_lo_centi);
    int64_t r_diff = (int64_t)r_lo - (int64_t)r; /* 正数 */
    int64_t r_span = (int64_t)r_lo - (int64_t)r_hi; /* 正数 */
    int32_t centi_c = t_lo_centi + (int32_t)((t_delta * r_diff) / r_span);
    return centi_c;
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