/* Standalone test for the App_SensorConvertBatteryVoltage logic
 * AND the SENSE reply printf format. Mirrors production code exactly.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define APP_BATT_VOLTAGE_DIVIDER_R_TOP_OHMS    100000U
#define APP_BATT_VOLTAGE_DIVIDER_R_BOTTOM_OHMS  5000U

static int32_t App_SensorConvertBatteryVoltage(uint32_t millivolts)
{
    uint32_t ratio_num = APP_BATT_VOLTAGE_DIVIDER_R_TOP_OHMS + APP_BATT_VOLTAGE_DIVIDER_R_BOTTOM_OHMS;
    uint32_t ratio_den = APP_BATT_VOLTAGE_DIVIDER_R_BOTTOM_OHMS;
    uint64_t battery_mv = ((uint64_t)millivolts * ratio_num) / ratio_den;

    if (battery_mv > (uint32_t)INT32_MAX)
    {
        return INT32_MAX;
    }
    return (int32_t)battery_mv;
}

/* Mirrors the App_AtReplySense format string (integer-only path). */
static void FormatSenseLine(char *out, size_t out_size,
                            long batt_ntc, long batt_mv,
                            long mcu, long lm51770, long mp4317,
                            long drv8874, long charge_mos,
                            unsigned long tick, unsigned long count)
{
    if (batt_mv < 0L)
    {
        batt_mv = 0L;
    }
    unsigned long v_int = (unsigned long)batt_mv / 1000UL;
    unsigned long v_dec = ((unsigned long)batt_mv % 1000UL + 50UL) / 100UL;

    snprintf(out, out_size,
             "+SENSE:BATT_NTC=%ld,BATT_V=%lu.%luV,MCU_C=%ld,LM51770_C=%ld,MP4317_C=%ld,DRV8874_C=%ld,CHARGE_MOS_C=%ld,TICK=%lu,COUNT=%lu\r\n",
             batt_ntc, v_int, v_dec,
             mcu, lm51770, mp4317, drv8874, charge_mos,
             tick, count);
}

int main(void)
{
    /* ---- Conversion math ---- */
    assert(App_SensorConvertBatteryVoltage(0U) == 0);
    assert(App_SensorConvertBatteryVoltage(133U) == 2793);   /* ~2.8V residual */
    assert(App_SensorConvertBatteryVoltage(1762U) == 37002); /* ~37V full */
    assert(App_SensorConvertBatteryVoltage(1000U) == 21000);
    assert(App_SensorConvertBatteryVoltage(2000U) == 42000);
    assert(App_SensorConvertBatteryVoltage(3300U) == 69300);

    /* ---- Reply format (integer-only, no float, newlib-nano safe) ---- */
    char line[160];

    /* 0 mV -> 0.0V */
    FormatSenseLine(line, sizeof(line),
                    0L, 0L, 0L, 0L, 0L, 0L, 0L, 0UL, 0UL);
    printf("0 mV     -> %s", line);
    assert(strstr(line, "BATT_V=0.0V") != NULL);

    /* User's actual serial reading: 2814 mV -> 2.8V */
    FormatSenseLine(line, sizeof(line),
                    0L, 2814L, 156L, 153L, 158L, 151L, 149L,
                    34000UL, 35UL);
    printf("2814 mV  -> %s", line);
    assert(strstr(line, "BATT_V=2.8V") != NULL);

    /* Rounding up: 2850 mV -> 2.9V */
    FormatSenseLine(line, sizeof(line),
                    0L, 2850L, 0L, 0L, 0L, 0L, 0L, 0UL, 0UL);
    printf("2850 mV  -> %s", line);
    assert(strstr(line, "BATT_V=2.9V") != NULL);

    /* Rounding down: 2849 mV -> 2.8V */
    FormatSenseLine(line, sizeof(line),
                    0L, 2849L, 0L, 0L, 0L, 0L, 0L, 0UL, 0UL);
    printf("2849 mV  -> %s", line);
    assert(strstr(line, "BATT_V=2.8V") != NULL);

    /* 37V full: 37002 mV -> 37.0V */
    FormatSenseLine(line, sizeof(line),
                    1234L, 37002L, 1200L, 1180L, 1210L, 1190L, 1170L,
                    4567UL, 8UL);
    printf("37002 mV -> %s", line);
    assert(strstr(line, "BATT_V=37.0V") != NULL);

    /* 3.3V example: 3300 mV -> 3.3V */
    FormatSenseLine(line, sizeof(line),
                    0L, 3300L, 0L, 0L, 0L, 0L, 0L, 0UL, 0UL);
    printf("3300 mV  -> %s", line);
    assert(strstr(line, "BATT_V=3.3V") != NULL);

    /* Full-scale: 69300 mV -> 69.3V */
    FormatSenseLine(line, sizeof(line),
                    0L, 69300L, 0L, 0L, 0L, 0L, 0L, 0UL, 0UL);
    printf("69300 mV -> %s", line);
    assert(strstr(line, "BATT_V=69.3V") != NULL);

    const char *semantic_fields[] = {
        "MCU_C=", "LM51770_C=", "MP4317_C=", "DRV8874_C=", "CHARGE_MOS_C="
    };
    const char *previous = strstr(line, "BATT_V=");
    for (size_t index = 0U; index < sizeof(semantic_fields) / sizeof(semantic_fields[0]); ++index)
    {
        const char *current = strstr(line, semantic_fields[index]);
        assert(current != NULL);
        assert(current > previous);
        previous = current;
    }
    assert(strstr(line, "NTC1") == NULL);
    assert(strstr(line, "NTC2") == NULL);
    assert(strstr(line, "NTC3") == NULL);
    assert(strstr(line, "NTC4") == NULL);
    assert(strstr(line, "NTC5") == NULL);

    printf("OK: conversion + integer format both verified.\n");
    return 0;
}
