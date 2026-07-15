#ifndef APP_CONFIG_H
#define APP_CONFIG_H

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

#endif