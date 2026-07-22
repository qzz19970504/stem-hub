/* Standalone test for the DRV8874 IPROPI→current path:
 *   - App_MotorConvertCurrent (mV → mA) — mirrors app_motor_task.c
 *   - App_SensorPublish deci-A rounding + mode-gated zero — mirrors app_sensor_task.c
 *   - App_AtReplySense "MOTOR_I=%lu.%luA" format — mirrors app_at_task.c
 *
 * 公式 (见 app_config.h):
 *   I_LOAD (mA) = V_IPROPI_mV * 1000 / (APP_MOTOR_IPROPI_AIPROPI_UA_PER_A
 *                                       * APP_MOTOR_IPROPI_R19_OHMS)
 *               = V_IPROPI_mV * 1000 / 1125
 * ADC 物理满量程 (Vref=3.3V) ≈ 2.93 A，超过会读到 ADC 饱和残值；本测试假设输入
 * 仍是合法 mV，只验证公式，未模拟 ADC 钳位。
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Mirror of app_config.h so this test compiles without hardware headers. */
#define APP_MOTOR_IPROPI_AIPROPI_UA_PER_A  450U
#define APP_MOTOR_IPROPI_R19_OHMS         2500U

typedef enum
{
    APP_MOTOR_TEST_MODE_SLEEP = 0,
    APP_MOTOR_TEST_MODE_WAKE,
    APP_MOTOR_TEST_MODE_FORWARD,
    APP_MOTOR_TEST_MODE_REVERSE,
    APP_MOTOR_TEST_MODE_BRAKE,
    APP_MOTOR_TEST_MODE_STOP
} AppMotorTestMode;

typedef struct
{
    AppMotorTestMode mode;
    uint32_t current_ma;
} AppMotorTestStatus;

/* ---------- Mirror of App_MotorConvertCurrent() in app_motor_task.c ---------- */
static uint32_t App_MotorConvertCurrent(uint32_t millivolts)
{
    uint64_t denom_mv_per_ma =
        ((uint64_t)APP_MOTOR_IPROPI_AIPROPI_UA_PER_A *
         (uint64_t)APP_MOTOR_IPROPI_R19_OHMS) / 1000ULL; /* = 1125 */
    uint64_t mA = ((uint64_t)millivolts * 1000ULL) / denom_mv_per_ma;
    return (mA > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)mA;
}

/* ---------- Mirror of sensor_task deci-A logic + mode-gated zero ---------- */
static uint32_t App_SensorComputeMotorDeciA(const AppMotorTestStatus *motor)
{
    if (motor == NULL)
    {
        return 0U;
    }
    if ((motor->mode != APP_MOTOR_TEST_MODE_FORWARD)
        && (motor->mode != APP_MOTOR_TEST_MODE_REVERSE))
    {
        return 0U;
    }
    uint32_t deci = (motor->current_ma + 50U) / 100U;
    return (deci > 29U) ? 29U : deci;
}

/* ---------- Mirror of App_AtReplySense MOTOR_I fragment (integer-only) ---------- */
static int FormatMotorI(char *out, size_t out_size, uint32_t motor_deci_a)
{
    return snprintf(out, out_size, "MOTOR_I=%lu.%luA",
                    (unsigned long)(motor_deci_a / 10UL),
                    (unsigned long)(motor_deci_a % 10UL));
}

int main(void)
{
    /* ---------------------------------------------------------------------
     * 1. mV → mA 公式校核
     *    公式: I_mA = mV * 1000 / (450 * 2500) = mV * 1000 / 1125
     * --------------------------------------------------------------------- */
    assert(App_MotorConvertCurrent(0U) == 0U);
    assert(App_MotorConvertCurrent(112U) == 99U);    /* ≈0.099 A */
    assert(App_MotorConvertCurrent(113U) == 100U);   /* =0.100 A，刚好 */
    assert(App_MotorConvertCurrent(1125U) == 1000U); /* =1.000 A */
    assert(App_MotorConvertCurrent(1238U) == 1100U); /* 1238*1000/1125 = 1100.4 → 1100 */
    assert(App_MotorConvertCurrent(2250U) == 2000U); /* =2.000 A */
    assert(App_MotorConvertCurrent(3300U) == 2933U); /* ≈2.933 A — ADC 物理上限 */
    assert(App_MotorConvertCurrent(3700U) == 3288U); /* 3.288 A — 仅公式验证，硬件读不到 */

    /* ---------------------------------------------------------------------
     * 2. dA 转换 + 模式门控 + 钳位
     * --------------------------------------------------------------------- */
    /* 电机停 → 一律 0 (用户要求: 停机归零) */
    AppMotorTestStatus stopped = {APP_MOTOR_TEST_MODE_SLEEP, 823U};
    assert(App_SensorComputeMotorDeciA(&stopped) == 0U);
    AppMotorTestStatus braking = {APP_MOTOR_TEST_MODE_BRAKE, 500U};
    assert(App_SensorComputeMotorDeciA(&braking) == 0U);
    AppMotorTestStatus waking = {APP_MOTOR_TEST_MODE_WAKE, 200U};
    assert(App_SensorComputeMotorDeciA(&waking) == 0U);
    assert(App_SensorComputeMotorDeciA(NULL) == 0U);

    /* 电机运行 → mA → 0.1 A 四舍五入 */
    AppMotorTestStatus fwd_823 = {APP_MOTOR_TEST_MODE_FORWARD, 823U};
    assert(App_SensorComputeMotorDeciA(&fwd_823) == 8U);  /* 8.23 → 8 */
    AppMotorTestStatus rev_150 = {APP_MOTOR_TEST_MODE_REVERSE, 150U};
    assert(App_SensorComputeMotorDeciA(&rev_150) == 2U);  /* 1.50 → 2 */
    AppMotorTestStatus rev_49  = {APP_MOTOR_TEST_MODE_REVERSE, 49U};
    assert(App_SensorComputeMotorDeciA(&rev_49) == 0U);   /* 0.49 → 0 */
    AppMotorTestStatus rev_50  = {APP_MOTOR_TEST_MODE_REVERSE, 50U};
    assert(App_SensorComputeMotorDeciA(&rev_50) == 1U);   /* 0.50 → 1 */
    AppMotorTestStatus fwd_2933 = {APP_MOTOR_TEST_MODE_FORWARD, 2933U};
    assert(App_SensorComputeMotorDeciA(&fwd_2933) == 29U); /* 钳到 29 dA */
    AppMotorTestStatus fwd_5000 = {APP_MOTOR_TEST_MODE_FORWARD, 5000U};
    assert(App_SensorComputeMotorDeciA(&fwd_5000) == 29U); /* 仍钳到 29 */

    /* ---------------------------------------------------------------------
     * 3. MOTOR_I=... 格式化串
     * --------------------------------------------------------------------- */
    char buf[32];
    FormatMotorI(buf, sizeof(buf), 0U);
    printf("deci=0  -> %s\n", buf);
    assert(strcmp(buf, "MOTOR_I=0.0A") == 0);

    FormatMotorI(buf, sizeof(buf), 8U);
    printf("deci=8  -> %s\n", buf);
    assert(strcmp(buf, "MOTOR_I=0.8A") == 0);

    FormatMotorI(buf, sizeof(buf), 10U);
    printf("deci=10 -> %s\n", buf);
    assert(strcmp(buf, "MOTOR_I=1.0A") == 0);

    FormatMotorI(buf, sizeof(buf), 15U);
    printf("deci=15 -> %s\n", buf);
    assert(strcmp(buf, "MOTOR_I=1.5A") == 0);

    FormatMotorI(buf, sizeof(buf), 29U);
    printf("deci=29 -> %s\n", buf);
    assert(strcmp(buf, "MOTOR_I=2.9A") == 0);

    printf("OK: motor IPROPI conversion + mode-gating + reply format all verified.\n");
    return 0;
}
