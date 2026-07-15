#ifndef APP_NTC_TABLE_H
#define APP_NTC_TABLE_H

#include <stdint.h>

/* HNTC0603-103F3450FA R-T table, 1°C step, -40..+125°C, 166 entries.
 * R 单位: Ω (R_Typ 列，来自 datasheet)
 * 索引: index 0 = -40°C, index 165 = +125°C。
 * 存储用 uint32_t 因为最大 197390 超过 uint16_t 上限。*/
#define APP_NTC_TABLE_SIZE       166U
#define APP_NTC_TABLE_T_MIN_C    (-40)
#define APP_NTC_TABLE_T_MAX_C    125

extern const uint32_t k_app_ntc_table_r_ohms[APP_NTC_TABLE_SIZE];

#endif
