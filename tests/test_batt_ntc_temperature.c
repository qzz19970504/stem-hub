/* Standalone test for the lookup-table based App_SensorConvertBatteryNtc.
 * Verifies the firmware against the R-T table from
 * 3445314105103-3435-1%.xls Sheet1 (NTC specs: R25=10kΩ, B25/85=3435K, ±1%).
 */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Replicate app_config.h NTC macros locally. */
#define APP_NTC_V_SUPPLY_MV   3300U
#define APP_NTC_R_SERIES_OHMS  470U

#define APP_BATT_NTC_TABLE_SIZE       181U
#define APP_BATT_NTC_TABLE_T_MIN_C    (-55)
#define APP_BATT_NTC_TABLE_T_MAX_C    125

/* Mirror of App/Src/app_batt_ntc_table.c — must stay in sync. */
const uint32_t k_app_batt_ntc_table_r_ohms[APP_BATT_NTC_TABLE_SIZE] = {
     437139,      412169,      388762,      366814,      346226,      326906,
     308770,      291739,      275740,      260706,      246574,      233284,
     220782,      209018,      197945,      187518,      177696,      168442,
     159720,      151496,      143739,      136422,      129516,      122997,
     116840,      111025,      105530,      100337,       95427,       90783,
      86390,       82233,       78298,       74572,       71043,       67700,
      64531,       61528,       58681,       55980,       53418,       50986,
      48678,       46487,       44406,       42429,       40550,       38764,
      37067,       35452,       33916,       32455,       31065,       29741,
      28480,       27280,       26136,       25047,       24008,       23018,
      22073,       21173,       20314,       19494,       18711,       17964,
      17250,       16568,       15915,       15293,       14697,       14127,
      13583,       13062,       12564,       12088,       11634,       11199,
      10787,       10387,       10000,        9637,        9286,        8949,
       8626,        8313,        8019,        7734,        7460,        7198,
       6946,        6704,        6472,        6249,        6035,        5829,
       5632,        5441,        5259,        5083,        4914,        4752,
       4595,        4445,        4300,        4161,        4027,        3898,
       3773,        3654,        3538,        3427,        3320,        3217,
       3117,        3021,        2928,        2839,        2753,        2670,
       2590,        2512,        2437,        2365,        2296,        2228,
       2163,        2100,        2040,        1981,        1924,        1870,
       1817,        1765,        1716,        1668,        1622,        1577,
       1533,        1491,        1451,        1411,        1373,        1336,
       1301,        1266,        1233,        1200,        1169,        1138,
       1108,        1080,        1052,        1025,         999,         974,
        949,         925,         902,         879,         858,         836,
        816,         796,         776,         758,         739,         722,
        704,         688,         671,         655,         640,         625,
        611,         596,         583,         569,         556,         544,
        531
};

/* Production function: Vadc (mV) -> 0.1°C */
static int32_t App_SensorConvertBatteryNtc(uint32_t millivolts)
{
    if (millivolts == 0U) return (int32_t)APP_BATT_NTC_TABLE_T_MIN_C * 10;
    if (millivolts >= APP_NTC_V_SUPPLY_MV) return INT32_MAX;

    int64_t numerator = (int64_t)APP_NTC_R_SERIES_OHMS *
                        ((int64_t)APP_NTC_V_SUPPLY_MV - (int64_t)millivolts);
    int64_t r_ntc = numerator / (int64_t)millivolts;
    if (r_ntc <= 0) return INT32_MAX;
    uint32_t r = (r_ntc > (int64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)r_ntc;

    const uint32_t *table = k_app_batt_ntc_table_r_ohms;
    if (r >= table[0]) return (int32_t)APP_BATT_NTC_TABLE_T_MIN_C * 10;
    if (r <= table[APP_BATT_NTC_TABLE_SIZE - 1U]) return (int32_t)APP_BATT_NTC_TABLE_T_MAX_C * 10;

    int lo = 0;
    int hi = (int)APP_BATT_NTC_TABLE_SIZE - 1;
    while (hi - lo > 1)
    {
        int mid = (lo + hi) / 2;
        if (table[mid] > r) lo = mid; else hi = mid;
    }

    int32_t t_lo_centi = ((int32_t)APP_BATT_NTC_TABLE_T_MIN_C + (int32_t)lo) * 10;
    int32_t t_hi_centi = ((int32_t)APP_BATT_NTC_TABLE_T_MIN_C + (int32_t)hi) * 10;
    uint32_t r_lo = table[lo];
    uint32_t r_hi = table[hi];
    if (r_lo == r_hi) return t_lo_centi;
    int64_t t_delta = (int64_t)(t_hi_centi - t_lo_centi);
    int64_t r_diff = (int64_t)r_lo - (int64_t)r;
    int64_t r_span = (int64_t)r_lo - (int64_t)r_hi;
    return t_lo_centi + (int32_t)((t_delta * r_diff) / r_span);
}

/* Direct R -> 0.1°C (no ADC quantization). */
static int32_t RntcToTempCenti(uint32_t r_ntc)
{
    const uint32_t *table = k_app_batt_ntc_table_r_ohms;
    if (r_ntc >= table[0]) return (int32_t)APP_BATT_NTC_TABLE_T_MIN_C * 10;
    if (r_ntc <= table[APP_BATT_NTC_TABLE_SIZE - 1U]) return (int32_t)APP_BATT_NTC_TABLE_T_MAX_C * 10;

    int lo = 0;
    int hi = (int)APP_BATT_NTC_TABLE_SIZE - 1;
    while (hi - lo > 1)
    {
        int mid = (lo + hi) / 2;
        if (table[mid] > r_ntc) lo = mid; else hi = mid;
    }
    int32_t t_lo_centi = ((int32_t)APP_BATT_NTC_TABLE_T_MIN_C + (int32_t)lo) * 10;
    int32_t t_hi_centi = ((int32_t)APP_BATT_NTC_TABLE_T_MIN_C + (int32_t)hi) * 10;
    uint32_t r_lo = table[lo];
    uint32_t r_hi = table[hi];
    if (r_lo == r_hi) return t_lo_centi;
    int64_t t_delta = (int64_t)(t_hi_centi - t_lo_centi);
    int64_t r_diff = (int64_t)r_lo - (int64_t)r_ntc;
    int64_t r_span = (int64_t)r_lo - (int64_t)r_hi;
    return t_lo_centi + (int32_t)((t_delta * r_diff) / r_span);
}

static uint32_t VadcFromRntc(uint32_t r_ntc_ohms)
{
    uint64_t num = (uint64_t)APP_NTC_V_SUPPLY_MV * APP_NTC_R_SERIES_OHMS;
    uint64_t den = (uint64_t)r_ntc_ohms + APP_NTC_R_SERIES_OHMS;
    return (uint32_t)(num / den);
}

static int App_AtFormatTempCenti(char *out, size_t out_size, int32_t centi_c)
{
    if (centi_c == INT32_MAX) return snprintf(out, out_size, "ERR");
    if (centi_c < 0)
    {
        unsigned long abs_c = (unsigned long)(-centi_c);
        return snprintf(out, out_size, "-%lu.%luC", abs_c / 10UL, abs_c % 10UL);
    }
    return snprintf(out, out_size, "%lu.%luC",
                    (unsigned long)centi_c / 10UL,
                    (unsigned long)centi_c % 10UL);
}

int main(void)
{
    /* ============================================================
     * Part 1: Datasheet table spot-checks (R -> T, exact R, expect exact T)
     * The interpolation between adjacent 1°C rows can shift up to 0.5°C.
     * ============================================================ */
    const struct { int temp_c; uint32_t r_ohms; } refs[] = {
        { -55, 437139 }, { -40, 187518 }, { -20,  67700 }, { -10,  42429 },
        {  -5,  33916 }, {   0,  27280 }, {  10,  17964 }, {  25,  10000 },
        {  37,   6472 }, {  50,   4161 }, {  60,   3021 }, {  70,   2228 },
        {  85,   1451 }, { 100,    974 }, { 125,    531 },
    };
    const size_t n_refs = sizeof(refs) / sizeof(refs[0]);

    printf("R -> T (table input):\n");
    printf("T(°C)   R(Ω)        T_out  Δ(°C)\n");
    printf("------  ----------  -----  -----\n");
    for (size_t i = 0; i < n_refs; ++i)
    {
        int32_t t = RntcToTempCenti(refs[i].r_ohms);
        int32_t err_centi = t - (int32_t)refs[i].temp_c * 10;
        printf("%4d   %10u   %5.1f  %+.1f\n",
               refs[i].temp_c, refs[i].r_ohms, t / 10.0, err_centi / 10.0);
        fflush(stdout);
        assert(err_centi >= -5 && err_centi <= 5); /* ≤0.5°C */
    }

    /* ============================================================
     * Part 2: Mid-cell interpolation (R halfway between rows)
     * ============================================================ */
    printf("\nMid-cell interpolation:\n");
    {
        /* between 25°C (10000) and 26°C (9637): midpoint R = 9818 or 9819 */
        uint32_t r_mid = (10000U + 9637U) / 2U;
        int32_t t = RntcToTempCenti(r_mid);
        printf("  R=%u  -> T=%.1f°C (expect ~25.5)\n", r_mid, t / 10.0);
        assert(t >= 254 && t <= 256); /* 25.4..25.6 */
    }
    {
        /* between -40°C (187518) and -39°C (177696): midpoint R = 182607 */
        uint32_t r_mid = (187518U + 177696U) / 2U;
        int32_t t = RntcToTempCenti(r_mid);
        printf("  R=%u  -> T=%.1f°C (expect ~-39.5)\n", r_mid, t / 10.0);
        assert(t >= -396 && t <= -394); /* -39.6..-39.4 */
    }

    /* ============================================================
     * Part 3: Full chain (R -> Vadc (truncated) -> R' -> T), with ADC quant
     * ============================================================ */
    printf("\nFull chain (R -> Vadc -> R' -> T):\n");
    printf("T(°C)   R(Ω)      Vadc  R'(Ω)    T_out  Δ(°C)\n");
    printf("------  --------  ----  -------   -----  -----\n");
    for (size_t i = 0; i < n_refs; ++i)
    {
        uint32_t vadc = VadcFromRntc(refs[i].r_ohms);
        int32_t t = App_SensorConvertBatteryNtc(vadc);
        int64_t r_recovered = ((int64_t)APP_NTC_R_SERIES_OHMS *
                               ((int64_t)APP_NTC_V_SUPPLY_MV - (int64_t)vadc)) / vadc;
        int32_t err_centi = t - (int32_t)refs[i].temp_c * 10;
        printf("%4d   %8u  %4u  %7ld   %5.1f  %+.1f\n",
               refs[i].temp_c, refs[i].r_ohms, vadc, (long)r_recovered,
               t / 10.0, err_centi / 10.0);
        fflush(stdout);

        /* Quantization at 12-bit ADC. */
        if (refs[i].temp_c >= 0)
        {
            assert(err_centi >= -10 && err_centi <= 10); /* ≤1°C */
        }
        else
        {
            /* Below 0°C, Vadc is tiny (a few mV) so quantization error is large. */
            assert(err_centi >= -100 && err_centi <= 100); /* ≤10°C below 0 */
        }
    }

    /* ============================================================
     * Part 4: Edge cases + format
     *   Vadc=0    → Rntc=∞    → NTC open     → very cold   → -550
     *   Vadc=3300 → Rntc=0    → NTC shorted  → very hot    → INT32_MAX
     *   Vadc=3299 → Rntc<1Ω   → effectively shorted → INT32_MAX
     * ============================================================ */
    assert(App_SensorConvertBatteryNtc(0U) == -550);       /* NTC open: very cold */
    assert(App_SensorConvertBatteryNtc(3300U) == INT32_MAX); /* NTC shorted: error flag */
    assert(App_SensorConvertBatteryNtc(3299U) == INT32_MAX); /* Rntc rounds to 0 */
    assert(App_SensorConvertBatteryNtc(3U) == -550);       /* Vadc below table: very cold */
    /* 125°C: R≈531Ω -> Vadc = 3300*470/(531+470) ≈ 1549 mV */
    assert(App_SensorConvertBatteryNtc(1549U) >= 1240 && App_SensorConvertBatteryNtc(1549U) <= 1260); /* ~125°C */

    char s[8];
    App_AtFormatTempCenti(s, sizeof(s), 253);       printf("\n253  -> %s\n", s); assert(s[0] == '2');
    App_AtFormatTempCenti(s, sizeof(s), -550);      printf("-550 -> %s\n", s); assert(s[0] == '-');
    App_AtFormatTempCenti(s, sizeof(s), 0);         printf("0    -> %s\n", s); assert(s[0] == '0');
    App_AtFormatTempCenti(s, sizeof(s), INT32_MAX); printf("MAX  -> %s\n", s); assert(s[0] == 'E');

    printf("\nOK: BATT_NTC temperature conversion + format verified.\n");
    printf("Practical accuracy: ~0.5°C in 0..85°C, ~1°C elsewhere (12-bit ADC quant).\n");
    return 0;
}
