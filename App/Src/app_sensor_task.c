#include "app_sensor.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "app_adc_filter.h"
#include "app_batt_ntc_table.h"
#include "app_config.h"
#include "app_motor.h"
#include "app_ntc_table.h"
#include "app_output.h"
#include "app_runtime.h"
#include "app_state.h"
#include "app_task_safety.h"
#include "app_thermal_guard.h"

typedef enum
{
    APP_SENSOR_FILTER_BATTERY_NTC = 0,
    APP_SENSOR_FILTER_BATTERY_VOLTAGE,
    APP_SENSOR_FILTER_NTC1,
    APP_SENSOR_FILTER_NTC2,
    APP_SENSOR_FILTER_NTC3
} AppSensorFilterIndex;

static AppAdcRollingMean
    g_sensor_filters[APP_ADC_ROLLING_CHANNEL_COUNT];
static uint16_t
    g_sensor_cycle_samples[APP_ADC_ROLLING_CHANNEL_COUNT];
static uint16_t
    g_sensor_cycle_means[APP_ADC_ROLLING_CHANNEL_COUNT];
static AppThermalGuard g_sensor_thermal_guard;

static bool App_SensorSetThermalState(bool active, void *context)
{
    (void)context;
    return App_StateSetThermalProtectionActive(active);
}

static void App_SensorRequestOutputStop(void *context)
{
    (void)context;
    (void)App_OutputEnqueueThermalStop();
}

static void App_SensorRequestMotorSleep(void *context)
{
    (void)context;
    (void)App_MotorEnqueueThermalSleep();
}

static const AppTaskSafetyCallbacks g_sensor_safety_callbacks = {
    .set_thermal_state = App_SensorSetThermalState,
    .request_output_stop = App_SensorRequestOutputStop,
    .request_motor_sleep = App_SensorRequestMotorSleep,
    .context = NULL,
};

/* 电池 NTC 温度换算 (查表法):
 *  1) 拓扑 3V3 -- NTC -- Vadc -- 470Ω -- GND，先由 Vadc 反推 Rntc
 *  2) 在 R-T 表 (k_app_batt_ntc_table_r_ohms) 中二分查找 + 线性插值得温度
 *  返回值: 0.1°C 分辨率的有符号整数 (例如 253 表示 25.3°C)
 *  边界 (按物理意义):
 *    - Vadc == 0          (NTC 开路/虚焊): 钳位到 -550 (= -55.0°C, 表下限)
 *    - Vadc >= V_SUPPLY   (NTC 短路):      返回 INT32_MAX 表示异常
 *    - Rntc 超出表范围   : 钳位到表上下限
 *  备注: 电池 NTC 型号与 NTC1_C/NTC2_C/NTC3_C 不同
 *        (本表 R25=10kΩ, B25/85=3435K, 范围 -55..+125°C, 表在 app_batt_ntc_table.h)，
 *        拓扑与 470Ω 串联电阻相同 (复用 APP_NTC_V_SUPPLY_MV / APP_NTC_R_SERIES_OHMS)。*/
static int32_t App_SensorConvertBatteryNtc(uint32_t millivolts)
{
    if (millivolts == 0U)
    {
        return (int32_t)APP_BATT_NTC_TABLE_T_MIN_C * 10;
    }
    if (millivolts >= APP_NTC_V_SUPPLY_MV)
    {
        return INT32_MAX;
    }

    /* Rntc = R_series * (V_supply - Vadc) / Vadc，单位 Ω
     * 用 int64 防溢出 (本表最大 Rntc ≈ 437kΩ @ -55°C)。*/
    int64_t numerator = (int64_t)APP_NTC_R_SERIES_OHMS *
                        ((int64_t)APP_NTC_V_SUPPLY_MV - (int64_t)millivolts);
    int64_t r_ntc = numerator / (int64_t)millivolts;
    if (r_ntc <= 0)
    {
        return INT32_MAX;
    }
    uint32_t r = (r_ntc > (int64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)r_ntc;

    /* 二分查找: 找 i 使 R[i] > r >= R[i+1] (表单调递减) */
    const uint32_t *table = k_app_batt_ntc_table_r_ohms;
    if (r >= table[0])
    {
        return (int32_t)APP_BATT_NTC_TABLE_T_MIN_C * 10;
    }
    if (r <= table[APP_BATT_NTC_TABLE_SIZE - 1U])
    {
        return (int32_t)APP_BATT_NTC_TABLE_T_MAX_C * 10;
    }

    int lo = 0;
    int hi = (int)APP_BATT_NTC_TABLE_SIZE - 1;
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
    int32_t t_lo_centi = ((int32_t)APP_BATT_NTC_TABLE_T_MIN_C + (int32_t)lo) * 10;
    int32_t t_hi_centi = ((int32_t)APP_BATT_NTC_TABLE_T_MIN_C + (int32_t)hi) * 10;
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
                                    uint16_t filtered_raw,
                                    int32_t (*convert_fn)(uint32_t))
{
    measure->raw = filtered_raw;
    measure->millivolts = App_RuntimeRawToMillivolts(filtered_raw);
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

    App_ThermalGuardInit(&g_sensor_thermal_guard,
                         APP_THERMAL_TRIP_TEMPERATURE_DECI_C,
                         APP_THERMAL_CLEAR_TEMPERATURE_DECI_C);

    for (;;)
    {
        App_RuntimeNoteSensorLoop();
        (void)memset(&next_snapshot, 0, sizeof(next_snapshot));
        success = App_RuntimeReadChannel(&hadc1, ADC_CHANNEL_4, &raw);
        if (success)
        {
            g_sensor_cycle_samples[APP_SENSOR_FILTER_BATTERY_NTC] = raw;
        }
        else
        {
            App_RuntimeNoteSensorAdc1ReadFail();
        }

        if (success)
        {
            success = App_RuntimeReadChannel(&hadc1, ADC_CHANNEL_5, &raw);
            if (success)
            {
                g_sensor_cycle_samples[APP_SENSOR_FILTER_BATTERY_VOLTAGE] = raw;
            }
            else
            {
                App_RuntimeNoteSensorAdc1ReadFail();
            }
        }

        if (success)
        {
            success = App_RuntimeReadAdc2Channel(ADC_CHANNEL_6, &raw);
            if (success)
            {
                g_sensor_cycle_samples[APP_SENSOR_FILTER_NTC3] = raw;
            }
            else
            {
                App_RuntimeNoteSensorAdc2ReadFail();
            }
        }

        if (success)
        {
            success = App_RuntimeReadAdc2Channel(ADC_CHANNEL_7, &raw);
            if (success)
            {
                g_sensor_cycle_samples[APP_SENSOR_FILTER_NTC2] = raw;
            }
            else
            {
                App_RuntimeNoteSensorAdc2ReadFail();
            }
        }

        if (success)
        {
            success = App_RuntimeReadAdc2Channel(ADC_CHANNEL_9, &raw);
            if (success)
            {
                g_sensor_cycle_samples[APP_SENSOR_FILTER_NTC1] = raw;
            }
            else
            {
                App_RuntimeNoteSensorAdc2ReadFail();
            }
        }

        if (success)
        {
            success = App_AdcRollingMeanPushCycle(
                g_sensor_filters,
                g_sensor_cycle_samples,
                APP_ADC_ROLLING_CHANNEL_COUNT,
                g_sensor_cycle_means);
        }

        if (success)
        {
            App_SensorUpdateMeasure(
                &next_snapshot.battery_ntc,
                g_sensor_cycle_means[APP_SENSOR_FILTER_BATTERY_NTC],
                App_SensorConvertBatteryNtc);
            App_SensorUpdateMeasure(
                &next_snapshot.battery_voltage,
                g_sensor_cycle_means[APP_SENSOR_FILTER_BATTERY_VOLTAGE],
                App_SensorConvertBatteryVoltage);
            App_SensorUpdateMeasure(
                &next_snapshot.ntc1,
                g_sensor_cycle_means[APP_SENSOR_FILTER_NTC1],
                App_SensorConvertNtcTemperature);
            App_SensorUpdateMeasure(
                &next_snapshot.ntc2,
                g_sensor_cycle_means[APP_SENSOR_FILTER_NTC2],
                App_SensorConvertNtcTemperature);
            App_SensorUpdateMeasure(
                &next_snapshot.ntc3,
                g_sensor_cycle_means[APP_SENSOR_FILTER_NTC3],
                App_SensorConvertNtcTemperature);

            AppThermalTransition thermal_transition = App_ThermalGuardUpdate(
                &g_sensor_thermal_guard,
                next_snapshot.ntc1.physical_value,
                next_snapshot.ntc2.physical_value,
                next_snapshot.ntc3.physical_value);
            App_TaskSafetyHandleThermalTransition(
                thermal_transition,
                &g_sensor_safety_callbacks);
            if ((thermal_transition == APP_THERMAL_NO_CHANGE)
                && g_sensor_thermal_guard.active)
            {
                /* A transient mutex conflict must not leave the shared latch
                 * false after the original stop requests have been sent. */
                (void)App_StateSetThermalProtectionActive(true);
            }

            /* DRV8874 IPROPI 电流快照：仅当电机在 FWD/REV 时取 mA→dA 转换；
             * 其他模式 (SLEEP/WAKE/BRAKE/STOP/UNKNOWN) 一律写 0，
             * 避免"电机已停但 SENSE 仍报残留电流"的误读。ADC 物理满量程 ≈2.93 A，
             * 这里再钳一道到 29 dA 防万一读到的是过流瞬间的残值。*/
            AppMotorStatus motor;
            uint32_t motor_deci = 0U;
            if (App_MotorTryGetStatus(&motor)
                && ((motor.mode == APP_MOTOR_MODE_FORWARD)
                    || (motor.mode == APP_MOTOR_MODE_REVERSE)))
            {
                motor_deci = (motor.current_ma + 50U) / 100U;
                if (motor_deci > 29U)
                {
                    motor_deci = 29U;
                }
            }
            next_snapshot.motor_current_a_deci = motor_deci;

            uint32_t tick = (uint32_t)osKernelGetTickCount();
            App_StatePublishSensorSnapshot(&next_snapshot, tick);
            App_RuntimeNoteSensorPublish(tick);
        }

        osDelay(APP_SENSOR_PERIOD_MS);
    }
}
