#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 固件版本号。上位机通过 AT+VERSION? 查询此字符串用于握手；
 * bump 版本只需改这一行。保持简短——回包整体长度受
 * APP_AT_PROTOCOL_MAX_LINE_LENGTH (48) 约束。*/
#define APP_FIRMWARE_VERSION "release-v2.1"

#define APP_UART1_RING_BUFFER_SIZE 256U
#define APP_UART1_LINE_BUFFER_SIZE 128U
#define APP_UART_TX_TIMEOUT_MS 100U
#define APP_ADC_TIMEOUT_MS 10U
#define APP_SENSOR_PERIOD_MS 1000U
#define APP_MOTOR_MONITOR_PERIOD_MS 100U
#define APP_MOTOR_WAKE_DELAY_MS 5U
#define APP_MOTOR_DIRECTION_DEADTIME_MS 20U
#define APP_ADC_VREF_MV 3300U
#define APP_ADC_MAX_VALUE 4095U
#define APP_MOTOR_OVERCURRENT_THRESHOLD_MA 3000U

/* Battery voltage divider: VBAT -- [R_TOP] --+-- [R_BOTTOM] -- GND
 *                                          |
 *                                          +--- ADC1 IN5
 * Vadc = VBAT * R_BOTTOM / (R_TOP + R_BOTTOM)
 * VBAT  = Vadc * (R_TOP + R_BOTTOM) / R_BOTTOM
 * 当前硬件: R_TOP = 100kΩ, R_BOTTOM = 5kΩ, 倍率 = 105/5 = 21。
 * 37V 满电时 ADC 端电压约 1.76V，落在 0-3.3V 量程内。*/
#define APP_BATT_VOLTAGE_DIVIDER_R_TOP_OHMS 100000U
#define APP_BATT_VOLTAGE_DIVIDER_R_BOTTOM_OHMS 5000U

/* NTC temperature sensing (NTC1/NTC2/NTC3 on ADC2 IN9/IN7/IN6).
 * 拓扑: 3V3 -- NTC -- Vadc -- [R_SERIES] -- GND
 * Vadc = V_SUPPLY * R_SERIES / (R_NTC + R_SERIES)
 * R_NTC = R_SERIES * (V_SUPPLY - Vadc) / Vadc
 * 当前 NTC 型号: HNTC0603-103F3450FA (R25=10kΩ, B25/85=3450K, ±1%)
 * 工作范围: -40°C ~ +125°C
 * 实际换算在 app_sensor_task.c 中用查表法 (R-T 表见 app_ntc_table.c)，
 * 不使用 B 方程——B 方程在该 datasheet 区间外偏差过大 (±100°C 级)。*/
#define APP_NTC_V_SUPPLY_MV    3300U
#define APP_NTC_R_SERIES_OHMS   470U
#define APP_NTC_R25_OHMS      10000U /* datasheet 标称值，仅作记录 */

#endif