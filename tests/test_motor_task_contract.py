from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = (ROOT / "App/Inc/app_config.h").read_text(encoding="utf-8")
MOTOR_TASK = (ROOT / "App/Src/app_motor_task.c").read_text(encoding="utf-8")


def require(text: str, source: str, description: str) -> None:
    if text not in source:
        raise AssertionError(description)


require(
    "#define APP_MOTOR_MONITOR_PERIOD_MS 10U",
    CONFIG,
    "motor current must be sampled every 10 ms",
)
require(
    '#include "app_motor_stall_guard.h"',
    MOTOR_TASK,
    "motor task must use the tested stall timing guard",
)
require(
    '#include "app_motor_current.h"',
    MOTOR_TASK,
    "motor task must keep using the calibrated current conversion",
)
require(
    "App_MotorStallGuardStart(&stall_guard",
    MOTOR_TASK,
    "each forward/reverse start must restart the startup blanking window",
)
require(
    "App_MotorStallGuardStop(&stall_guard)",
    MOTOR_TASK,
    "non-running and fault states must stop the stall guard",
)
require(
    "App_MotorStallGuardUpdate(&stall_guard",
    MOTOR_TASK,
    "current samples must feed the stall timing guard",
)
require(
    "App_StateTryGetStallCurrentMa(&stall_current_ma)",
    MOTOR_TASK,
    "motor task must read the configured threshold",
)
require(
    "App_MotorStoreStatus(APP_MOTOR_MODE_WAKE, 0U, overcurrent_latched)",
    MOTOR_TASK,
    "non-running commands must preserve a previous stall latch",
)
require(
    "App_MotorStoreStatus(mode, 0U, false)",
    MOTOR_TASK,
    "a new forward/reverse command must clear the previous stall latch",
)
require(
    "false,\n                                            0U,\n                                            0U",
    MOTOR_TASK,
    "an unavailable ADC or threshold sample must be invalid evidence",
)
require(
    "App_MotorStoreStatus(APP_MOTOR_MODE_BRAKE, current_ma, true)",
    MOTOR_TASK,
    "a confirmed stall must brake and latch overcurrent",
)

if "APP_MOTOR_OVERCURRENT_THRESHOLD_MA" in CONFIG or "APP_MOTOR_OVERCURRENT_THRESHOLD_MA" in MOTOR_TASK:
    raise AssertionError("the obsolete fixed 3 A threshold must be removed")

print("OK: motor task stall-protection contract verified.")
