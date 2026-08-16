#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 固件版本号。上位机通过 AT+VERSION? 查询此字符串用于握手；
 * bump 版本只需改这一行。保持简短——回包整体长度受
 * APP_AT_PROTOCOL_MAX_LINE_LENGTH (96) 约束。*/
#define APP_FIRMWARE_VERSION "release-v3.2"

#define APP_CHARGE_MIN_ON_TIME_SECONDS 1U
#define APP_CHARGE_MAX_ON_TIME_SECONDS 60U
#define APP_CHARGE_CYCLE_TIME_SECONDS 60U
#define APP_CHARGE_DEFAULT_ON_TIME_SECONDS 10U
#define APP_THERMAL_TRIP_TEMPERATURE_DECI_C 600
#define APP_THERMAL_CLEAR_TEMPERATURE_DECI_C 550

#define APP_AT_PROTOCOL_MAX_LINE_LENGTH 96U
#define APP_UART_TUNNEL_CHUNK_SIZE 32U
#define APP_UART1_RING_BUFFER_SIZE 256U
#define APP_UART_BRIDGE_RING_BUFFER_SIZE 256U
#define APP_UART1_LINE_BUFFER_SIZE 128U
#define APP_UART_TX_TIMEOUT_MS 100U
#define APP_ADC_TIMEOUT_MS 10U
#define APP_SENSOR_PERIOD_MS 1000U
#define APP_MOTOR_MONITOR_PERIOD_MS 100U
#define APP_THERMAL_CONSUMER_CHECK_PERIOD_MS 100U
#define APP_MOTOR_WAKE_DELAY_MS 5U
#define APP_MOTOR_DIRECTION_DEADTIME_MS 20U
#define APP_MOTOR_STALL_STARTUP_BLANKING_MS 300U
#define APP_MOTOR_STALL_PERSISTENCE_MS 100U
#define APP_MOTOR_STALL_MIN_CURRENT_MA 1000U
#define APP_MOTOR_STALL_MAX_CURRENT_MA 30000U
#define APP_MOTOR_STALL_DEFAULT_CURRENT_MA 4000U
#define APP_ADC_VREF_MV 3300U
#define APP_ADC_MAX_VALUE 4095U
#define APP_MOTOR_OVERCURRENT_THRESHOLD_MA 3000U

/* DRV8874 IPROPI 电流采样 (ADC2_IN8):
 * 拓扑: IPROPI -- [R19] -- GND。IPROPI 灌电流与低侧 MOSFET 电流按
 * AIPROPI (µA/A) 比例镜像。I_LOAD = V_IPROPI / (AIPROPI × R19)。
 * A=450µA/A, R19=220Ω ⇒ 0.099 V/A。Vref=3.3V ⇒ 可测上限 ≈ 33.3 A。
 * AT+MOTOR? 输出 CURRENT_MA = millivolts * 1000 / 99；
 * AT+SENSE? 输出 MOTOR_I = deci-A 整数 (= mA / 100 四舍五入)。
 * 见 app_motor_current.c。*/
#define APP_MOTOR_IPROPI_AIPROPI_UA_PER_A  450U
#define APP_MOTOR_IPROPI_R19_OHMS          220U
#define APP_MOTOR_CURRENT_MAX_DECI_A        333U

/* Battery voltage divider: VBAT -- [R_TOP] --+-- [R_BOTTOM] -- GND
 *                                          |
 *                                          +--- ADC1 IN5
 * Vadc = VBAT * R_BOTTOM / (R_TOP + R_BOTTOM)
 * VBAT  = Vadc * (R_TOP + R_BOTTOM) / R_BOTTOM
 * 当前硬件: R_TOP = 100kΩ, R_BOTTOM = 5kΩ, 倍率 = 105/5 = 21。
 * 37V 满电时 ADC 端电压约 1.76V，落在 0-3.3V 量程内。*/
#define APP_BATT_VOLTAGE_DIVIDER_R_TOP_OHMS 100000U
#define APP_BATT_VOLTAGE_DIVIDER_R_BOTTOM_OHMS 5000U

/* Protected component NTC topology:
 *   MCU       -> ADC2 IN9 (PB1)
 *   LM51770   -> ADC2 IN7 (PA7)
 *   MP4317    -> ADC2 IN6 (PA6)
 *   DRV8874   -> ADC2 IN1 (PA1, physical pin 11)
 *   Charge MOS near LM51770 -> ADC1 IN0 (PA0, physical pin 10)
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
