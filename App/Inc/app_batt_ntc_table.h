#ifndef APP_BATT_NTC_TABLE_H
#define APP_BATT_NTC_TABLE_H

#include <stdint.h>

/* Battery NTC R-T table, 1°C step, -55..+125°C, 181 entries.
 * 数据源: 3445314105103-3435-1%.xls Sheet1 Column C "Center value"。
 * NTC 规格: R25=10kΩ, B25/85=3435K, ±1%。
 * R 单位: Ω (center/typical 列)
 * 索引: index 0 = -55°C, index 180 = +125°C。
 * 存储用 uint32_t 因为最大 437139 超过 uint16_t 上限。*/
#define APP_BATT_NTC_TABLE_SIZE       181U
#define APP_BATT_NTC_TABLE_T_MIN_C    (-55)
#define APP_BATT_NTC_TABLE_T_MAX_C    125

extern const uint32_t k_app_batt_ntc_table_r_ohms[APP_BATT_NTC_TABLE_SIZE];

#endif
