#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include "app_at_protocol.h"

typedef struct
{
    uint16_t raw;
    uint32_t millivolts;
    int32_t physical_value;
} AppAnalogMeasure;

typedef struct
{
    AppAnalogMeasure battery_ntc;
    AppAnalogMeasure battery_voltage;
    AppAnalogMeasure mcu_temperature;
    AppAnalogMeasure lm51770_temperature;
    AppAnalogMeasure mp4317_temperature;
    AppAnalogMeasure drv8874_temperature;
    AppAnalogMeasure charge_mos_temperature;
    uint32_t sample_tick;
    uint32_t sample_counter;
    /* DRV8874 IPROPI 电流 (来自 motor 状态)：
     *   0.1 A 分辨率，上限钳在 333 (= 33.3 A)，ADC 物理满量程已在
     *   sensor_task 里换算时限制；
     *   电机不在 FWD/REV 时一律报告 0（不显示上次残值）。*/
    uint32_t motor_current_a_deci;
} AppSensorSnapshot;

typedef struct
{
    AppMotorMode mode;
    uint32_t current_ma;
    bool overcurrent_latched;
    bool drv_fault_active;
} AppMotorStatus;

typedef struct
{
    bool led_master_enabled;
    bool nmos1_enabled;
    bool nmos2_enabled;
    bool uvlo_enabled;
    bool mp4317_enabled;
    bool motor_bypass_enabled;
    bool charge_bypass_enabled;
} AppIoStatus;

typedef enum
{
    APP_MOTOR_REQUEST_SET_MODE = 0,
    APP_MOTOR_REQUEST_SET_BYPASS
} AppMotorRequestType;

typedef struct
{
    AppMotorRequestType type;
    union
    {
        AppMotorMode mode;
        bool bypass_enabled;
    } data;
} AppMotorRequest;

typedef struct
{
    bool enabled;
} AppLedRequest;

typedef enum
{
    APP_OUTPUT_TARGET_NMOS1 = 0,
    APP_OUTPUT_TARGET_NMOS2,
    APP_OUTPUT_TARGET_UVLO,
    APP_OUTPUT_TARGET_MP4317,
    APP_OUTPUT_TARGET_CHARGE_BYPASS
} AppOutputTarget;

typedef enum
{
    APP_OUTPUT_REQUEST_SET_TARGET = 0,
    APP_OUTPUT_REQUEST_SET_POWER_MODE,
    APP_OUTPUT_REQUEST_THERMAL_STOP,
    APP_OUTPUT_REQUEST_SET_CHARGE_BYPASS
} AppOutputRequestType;

typedef struct
{
    AppOutputRequestType type;
    union
    {
        struct
        {
            AppOutputTarget target;
            bool enabled;
        } target;
        AppPowerMode power_mode;
        bool charge_bypass_enabled;
    } data;
} AppOutputRequest;

#endif
