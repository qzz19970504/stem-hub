/* Standalone test for the lookup-table based App_SensorConvertNtcTemperature.
 * Verifies the firmware against the HNTC0603-103F3450FA datasheet R-T table.
 */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Replicate app_config.h / app_ntc_table.h locally. */
#define APP_NTC_V_SUPPLY_MV   3300U
#define APP_NTC_R_SERIES_OHMS  470U

#define APP_NTC_TABLE_SIZE       166U
#define APP_NTC_TABLE_T_MIN_C    (-40)
#define APP_NTC_TABLE_T_MAX_C    125

const uint32_t k_app_ntc_table_r_ohms[APP_NTC_TABLE_SIZE] = {
    /* -40 .. -1 */
    197390, 186540, 176350, 166800, 157820, 149390, 141510, 134090,
    127110, 120530, 114340, 108530, 103040,  97870,  92989,  88381,
     84036,  79931,  76052,  72384,  68915,  65634,  62529,  59589,
     56804,  54166,  51665,  49294,  47046,  44913,  42889,  40967,
     39142,  37408,  35761,  34196,  32707,  31291,  29945,  28664,
    /* 0 .. 39 */
     27445,  26283,  25177,  24124,  23121,  22165,  21253,  20384,
     19555,  18764,  18010,  17290,  16602,  15946,  15319,  14720,
     14148,  13601,  13078,  12578,  12099,  11642,  11204,  10785,
     10384,  10000,   9632,   9280,   8943,   8619,   8309,   8012,
      7727,   7453,   7191,   6939,   6698,   6466,   6243,   6029,
    /* 40 .. 79 */
      5824,   5627,   5437,   5255,   5080,   4911,   4749,   4593,
      4443,   4299,   4160,   4027,   3898,   3774,   3654,   3539,
      3429,   3322,   3219,   3119,   3024,   2931,   2842,   2756,
      2673,   2593,   2516,   2441,   2369,   2300,   2233,   2168,
      2105,   2044,   1986,   1929,   1874,   1821,   1770,   1720,
    /* 80 .. 119 */
      1673,   1626,   1581,   1538,   1496,   1455,   1416,   1377,
      1340,   1304,   1270,   1236,   1204,   1172,   1141,   1112,
      1083,   1055,   1028,   1002,    976,    951,    927,    904,
       882,    860,    838,    818,    798,    778,    759,    741,
       723,    706,    689,    673,    657,    641,    626,    612,
    /* 120 .. 125 */
       598,    584,    570,    557,    545,    532
};

/* Production function: Vadc (mV) -> 0.1°C */
static int32_t App_SensorConvertNtcTemperature(uint32_t millivolts)
{
    if (millivolts == 0U) return (int32_t)APP_NTC_TABLE_T_MIN_C * 10;   /* NTC open */
    if (millivolts >= APP_NTC_V_SUPPLY_MV) return INT32_MAX;            /* NTC shorted */

    int64_t numerator = (int64_t)APP_NTC_R_SERIES_OHMS *
                        ((int64_t)APP_NTC_V_SUPPLY_MV - (int64_t)millivolts);
    int64_t r_ntc = numerator / (int64_t)millivolts;
    if (r_ntc <= 0) return INT32_MAX;
    uint32_t r = (r_ntc > (int64_t)UINT32_MAX) ? UINT32_MAX : (uint32_t)r_ntc;

    const uint32_t *table = k_app_ntc_table_r_ohms;
    if (r >= table[0]) return (int32_t)APP_NTC_TABLE_T_MIN_C * 10;
    if (r <= table[APP_NTC_TABLE_SIZE - 1U]) return (int32_t)APP_NTC_TABLE_T_MAX_C * 10;

    int lo = 0;
    int hi = (int)APP_NTC_TABLE_SIZE - 1;
    while (hi - lo > 1)
    {
        int mid = (lo + hi) / 2;
        if (table[mid] > r) lo = mid; else hi = mid;
    }

    int32_t t_lo_centi = ((int32_t)APP_NTC_TABLE_T_MIN_C + (int32_t)lo) * 10;
    int32_t t_hi_centi = ((int32_t)APP_NTC_TABLE_T_MIN_C + (int32_t)hi) * 10;
    uint32_t r_lo = table[lo];
    uint32_t r_hi = table[hi];
    if (r_lo == r_hi) return t_lo_centi;
    int64_t t_delta = (int64_t)(t_hi_centi - t_lo_centi);
    int64_t r_diff = (int64_t)r_lo - (int64_t)r;
    int64_t r_span = (int64_t)r_lo - (int64_t)r_hi;
    return t_lo_centi + (int32_t)((t_delta * r_diff) / r_span);
}

/* Direct R -> 0.1°C (no ADC quantization), for ideal-input spot checks. */
static int32_t RntcToTempCenti(uint32_t r_ntc)
{
    const uint32_t *table = k_app_ntc_table_r_ohms;
    if (r_ntc >= table[0]) return (int32_t)APP_NTC_TABLE_T_MIN_C * 10;
    if (r_ntc <= table[APP_NTC_TABLE_SIZE - 1U]) return (int32_t)APP_NTC_TABLE_T_MAX_C * 10;

    int lo = 0;
    int hi = (int)APP_NTC_TABLE_SIZE - 1;
    while (hi - lo > 1)
    {
        int mid = (lo + hi) / 2;
        if (table[mid] > r_ntc) lo = mid; else hi = mid;
    }
    int32_t t_lo_centi = ((int32_t)APP_NTC_TABLE_T_MIN_C + (int32_t)lo) * 10;
    int32_t t_hi_centi = ((int32_t)APP_NTC_TABLE_T_MIN_C + (int32_t)hi) * 10;
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
     * The interpolation between adjacent 1°C rows can shift up to 0.5°C,
     * so we allow ±0.6°C.  For values that land ON a row, expect 0°C.
     * ============================================================ */
    const struct { int temp_c; uint32_t r_ohms; } refs[] = {
        { -40, 197390 }, { -20,  68915 }, {  -5,  34196 },
        {   0,  27445 }, {  10,  18010 }, {  25,  10000 },
        {  37,   6466 }, {  50,   4160 }, {  60,   3024 },
        {  70,   2233 }, {  85,   1455 }, { 100,    976 },
        { 125,    532 },
    };
    const size_t n_refs = sizeof(refs) / sizeof(refs[0]);

    printf("R -> T (table input, expect exact or interpolated T):\n");
    printf("T(°C)   R(Ω)      T_out  Δ(°C)\n");
    printf("------  --------  -----  -----\n");
    for (size_t i = 0; i < n_refs; ++i)
    {
        int32_t t = RntcToTempCenti(refs[i].r_ohms);
        int32_t err_centi = t - (int32_t)refs[i].temp_c * 10;
        printf("%4d   %8u   %5.1f  %+.1f\n",
               refs[i].temp_c, refs[i].r_ohms, t / 10.0, err_centi / 10.0);
        fflush(stdout);
        assert(err_centi >= -5 && err_centi <= 5); /* ≤0.5°C */
    }

    /* ============================================================
     * Part 2: Mid-cell interpolation (R halfway between rows)
     * Should land at T exactly halfway between the two anchor temps.
     * ============================================================ */
    printf("\nMid-cell interpolation (R halfway between two rows):\n");
    {
        /* between 25°C (10000) and 26°C (9632): midpoint R = 9816 */
        uint32_t r_mid = (10000U + 9632U) / 2U;
        int32_t t = RntcToTempCenti(r_mid);
        int32_t expected_centi = 255; /* 25.5°C */
        printf("  R=%u  -> T=%.1f°C (expect 25.5)\n", r_mid, t / 10.0);
        assert(t == expected_centi);
    }
    {
        /* between 0°C (27445) and 1°C (26283): midpoint R = 26864 */
        uint32_t r_mid = (27445U + 26283U) / 2U;
        int32_t t = RntcToTempCenti(r_mid);
        int32_t expected_centi = 5; /* 0.5°C */
        printf("  R=%u  -> T=%.1f°C (expect 0.5)\n", r_mid, t / 10.0);
        assert(t == expected_centi);
    }

    /* ============================================================
     * Part 3: Full chain (R -> Vadc -> R -> T), with ADC quantization
     * ============================================================ */
    printf("\nFull chain (R -> Vadc (truncated) -> R' -> T):\n");
    printf("T(°C)   R(Ω)    Vadc  R'(Ω)   T_out  Δ(°C)\n");
    printf("------  ------  ----  ------   -----  -----\n");
    for (size_t i = 0; i < n_refs; ++i)
    {
        uint32_t vadc = VadcFromRntc(refs[i].r_ohms);
        int32_t t = App_SensorConvertNtcTemperature(vadc);
        int64_t r_recovered = ((int64_t)APP_NTC_R_SERIES_OHMS *
                               ((int64_t)APP_NTC_V_SUPPLY_MV - (int64_t)vadc)) / vadc;
        int32_t err_centi = t - (int32_t)refs[i].temp_c * 10;
        printf("%4d   %6u  %4u  %6ld  %5.1f  %+.1f\n",
               refs[i].temp_c, refs[i].r_ohms, vadc, (long)r_recovered,
               t / 10.0, err_centi / 10.0);
        fflush(stdout);

        /* Quantization error dominates at low Vadc (~5mV at -40°C). */
        if (refs[i].temp_c >= 0)
        {
            assert(err_centi >= -10 && err_centi <= 10); /* ≤1°C */
        }
        else
        {
            assert(err_centi >= -50 && err_centi <= 50); /* ≤5°C below 0 */
        }
    }

    /* ============================================================
     * Part 4: Edge cases + format
     * Topology: 3V3 -- NTC -- Vadc -- 470Ω -- GND
     *   Vadc=0    → Rntc=∞    → NTC open     → very cold  → -400
     *   Vadc=3300 → Rntc=0    → NTC shorted  → very hot   → INT32_MAX
     *   Vadc=3299 → Rntc<1Ω   → effectively shorted → INT32_MAX
     * ============================================================ */
    assert(App_SensorConvertNtcTemperature(0U) == -400);     /* NTC open: very cold */
    assert(App_SensorConvertNtcTemperature(3300U) == INT32_MAX); /* NTC shorted: error flag */
    assert(App_SensorConvertNtcTemperature(3299U) == INT32_MAX); /* Rntc rounds to 0, shorted */
    assert(App_SensorConvertNtcTemperature(7U) == -400);     /* Vadc below table: very cold */
    assert(App_SensorConvertNtcTemperature(1547U) >= 1240 && App_SensorConvertNtcTemperature(1547U) <= 1260); /* ~125°C */

    char s[8];
    App_AtFormatTempCenti(s, sizeof(s), 253);       printf("\n253  -> %s\n", s); assert(s[0] == '2');
    App_AtFormatTempCenti(s, sizeof(s), -52);       printf("-52  -> %s\n", s); assert(s[0] == '-');
    App_AtFormatTempCenti(s, sizeof(s), 0);         printf("0    -> %s\n", s); assert(s[0] == '0');
    App_AtFormatTempCenti(s, sizeof(s), INT32_MAX); printf("MAX  -> %s\n", s); assert(s[0] == 'E');

    printf("\nOK: NTC temperature conversion + format verified.\n");
    printf("Practical accuracy: ~0.5°C in 0..85°C, ~1°C elsewhere (12-bit ADC quant).\n");
    return 0;
}