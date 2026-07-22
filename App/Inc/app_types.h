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
    AppAnalogMeasure ntc1;
    AppAnalogMeasure ntc2;
    AppAnalogMeasure ntc3;
    uint32_t sample_tick;
    uint32_t sample_counter;
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
} AppIoStatus;

typedef struct
{
    AppMotorMode mode;
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
    APP_OUTPUT_TARGET_MP4317
} AppOutputTarget;

typedef struct
{
    AppOutputTarget target;
    bool enabled;
} AppOutputRequest;

#endif