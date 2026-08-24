from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN_HEADER = (ROOT / "Core/Inc/main.h").read_text(encoding="utf-8")
GPIO_SOURCE = (ROOT / "Core/Src/gpio.c").read_text(encoding="utf-8")
IOC = (ROOT / "stem-hub.ioc").read_text(encoding="utf-8")
RUNTIME_SOURCE = (ROOT / "App/Src/app_runtime.c").read_text(encoding="utf-8")
MOTOR_HEADER = (ROOT / "App/Inc/app_motor.h").read_text(encoding="utf-8")
MOTOR_SOURCE = (ROOT / "App/Src/app_motor_task.c").read_text(encoding="utf-8")
AT_TASK_SOURCE = (ROOT / "App/Src/app_at_task.c").read_text(encoding="utf-8")
OUTPUT_HEADER = (ROOT / "App/Inc/app_output.h").read_text(encoding="utf-8")
OUTPUT_SOURCE = (ROOT / "App/Src/app_output_task.c").read_text(encoding="utf-8")
LED_SOURCE = (ROOT / "App/Src/app_led_task.c").read_text(encoding="utf-8")
CONFIG_HEADER = (ROOT / "App/Inc/app_config.h").read_text(encoding="utf-8")


def require(text: str, source: str, description: str) -> None:
    if text not in source:
        raise AssertionError(description)


def test_pc13_and_pc14_have_durable_pin_mappings() -> None:
    require(
        "#define MOTOR_BYPASS_Pin GPIO_PIN_13",
        MAIN_HEADER,
        "PC13 motor bypass mapping missing",
    )
    require(
        "#define MOTOR_BYPASS_GPIO_Port GPIOC",
        MAIN_HEADER,
        "PC13 motor bypass port missing",
    )
    require(
        "#define CHARGE_BYPASS_Pin GPIO_PIN_14",
        MAIN_HEADER,
        "PC14 charge bypass mapping missing",
    )
    require(
        "#define CHARGE_BYPASS_GPIO_Port GPIOC",
        MAIN_HEADER,
        "PC14 charge bypass port missing",
    )


def test_bypass_pins_are_initialized_low_as_push_pull_outputs() -> None:
    require(
        "MOTOR_BYPASS_Pin|CHARGE_BYPASS_Pin, GPIO_PIN_RESET",
        GPIO_SOURCE,
        "both bypass pins must be written low before configuration",
    )
    require(
        "GPIO_InitStruct.Pin = MOTOR_BYPASS_Pin|CHARGE_BYPASS_Pin;",
        GPIO_SOURCE,
        "both bypass pins must share an explicit GPIO configuration",
    )
    require("GPIO_MODE_OUTPUT_PP", GPIO_SOURCE, "bypass pins must be push-pull")
    require("GPIO_NOPULL", GPIO_SOURCE, "bypass pins must not use pulls")
    require("GPIO_SPEED_FREQ_LOW", GPIO_SOURCE, "bypass pins must use low speed")


def test_cubemx_metadata_preserves_bypass_outputs() -> None:
    require(
        "PC13-TAMPER-RTC.GPIO_Label=MOTOR_BYPASS",
        IOC,
        "CubeMX PC13 label missing",
    )
    require(
        "PC13-TAMPER-RTC.Signal=GPIO_Output",
        IOC,
        "CubeMX PC13 output missing",
    )
    require(
        "PC14-OSC32_IN.GPIO_Label=CHARGE_BYPASS",
        IOC,
        "CubeMX PC14 label missing",
    )
    require(
        "PC14-OSC32_IN.Signal=GPIO_Output",
        IOC,
        "CubeMX PC14 output missing",
    )


def test_runtime_safe_initialization_clears_both_bypasses() -> None:
    require(
        "HAL_GPIO_WritePin(MOTOR_BYPASS_GPIO_Port, MOTOR_BYPASS_Pin, GPIO_PIN_RESET);",
        RUNTIME_SOURCE,
        "runtime safe initialization must clear PC13",
    )
    require(
        "HAL_GPIO_WritePin(CHARGE_BYPASS_GPIO_Port, CHARGE_BYPASS_Pin, GPIO_PIN_RESET);",
        RUNTIME_SOURCE,
        "runtime safe initialization must clear PC14",
    )


def test_motor_task_owns_and_revalidates_pc13_requests() -> None:
    require(
        "bool App_MotorEnqueueBypass(bool enabled);",
        MOTOR_HEADER,
        "motor bypass queue API missing",
    )
    require(
        "bool App_MotorEnqueueBypass(bool enabled)",
        MOTOR_SOURCE,
        "motor bypass queue implementation missing",
    )
    require(
        "APP_MOTOR_REQUEST_SET_BYPASS",
        MOTOR_SOURCE,
        "motor task must consume typed bypass requests",
    )
    require(
        "App_ResistorBypassMotorActivationAllowed",
        MOTOR_SOURCE,
        "motor owner must revalidate the applied running mode",
    )
    apply_start = MOTOR_SOURCE.index("static void App_MotorApplyBypassRequest")
    apply_end = MOTOR_SOURCE.index("void App_MotorTask", apply_start)
    apply_source = MOTOR_SOURCE[apply_start:apply_end]
    require(
        "App_StateTryGetThermalProtectionActive",
        apply_source,
        "motor owner must revalidate thermal state before raising PC13",
    )
    require(
        "!thermal_active",
        apply_source,
        "motor owner must reject activation while thermal protection is active",
    )


def test_motor_transitions_and_stall_restore_pc13_low() -> None:
    require(
        "App_ResistorBypassMotorTransitionRequiresReset",
        MOTOR_SOURCE,
        "motor mode transitions must use the tested reset policy",
    )
    require(
        "App_MotorSetBypass(false);",
        MOTOR_SOURCE,
        "motor safety paths must clear PC13",
    )
    stall_start = MOTOR_SOURCE.index("if (App_MotorStallGuardUpdate")
    bypass_reset = MOTOR_SOURCE.index("App_MotorSetBypass(false);", stall_start)
    bridge_disable = MOTOR_SOURCE.index("App_MotorSetOutputs(", stall_start)
    assert bypass_reset < bridge_disable, "stall braking must clear PC13 before EN"


def test_at_task_returns_state_error_before_queueing_pc13_on() -> None:
    require(
        "case APP_AT_COMMAND_SET_MOTOR_BYPASS:",
        AT_TASK_SOURCE,
        "AT handler must dispatch the motor bypass command",
    )
    require(
        'App_RuntimeSendError("STATE")',
        AT_TASK_SOURCE,
        "invalid motor activation must return ERROR:STATE",
    )
    require(
        "App_MotorEnqueueBypass",
        AT_TASK_SOURCE,
        "valid motor bypass requests must use the motor owner queue",
    )


def test_pc13_has_no_unapproved_production_writer() -> None:
    allowed_paths = {
        ROOT / "Core/Src/gpio.c",
        ROOT / "App/Src/app_runtime.c",
        ROOT / "App/Src/app_motor_task.c",
    }
    write_token = "HAL_GPIO_WritePin(MOTOR_BYPASS_GPIO_Port"

    for source_path in ROOT.glob("**/*.c"):
        if source_path in allowed_paths or "build" in source_path.parts:
            continue
        source = source_path.read_text(encoding="utf-8", errors="ignore")
        assert write_token not in source, f"unapproved PC13 writer: {source_path}"


def test_output_task_owns_and_revalidates_pc14_requests() -> None:
    require(
        "bool App_OutputEnqueueChargeBypass(bool enabled);",
        OUTPUT_HEADER,
        "charge bypass queue API missing",
    )
    require(
        "bool App_OutputEnqueueChargeBypass(bool enabled)",
        OUTPUT_SOURCE,
        "charge bypass queue implementation missing",
    )
    require(
        "APP_OUTPUT_REQUEST_SET_CHARGE_BYPASS",
        OUTPUT_SOURCE,
        "output task must consume typed charge bypass requests",
    )
    require(
        "App_ResistorBypassChargeActivationAllowed",
        OUTPUT_SOURCE,
        "output owner must revalidate the actual charge output phase",
    )
    require(
        "App_OutputStateRequestAllowed",
        OUTPUT_SOURCE,
        "output owner must revalidate thermal safety",
    )


def test_periodic_charge_phase_transitions_preserve_pc14_latch() -> None:
    apply_start = OUTPUT_SOURCE.index("static void App_OutputApplyPowerAction")
    apply_end = OUTPUT_SOURCE.index(
        "static void App_OutputShutDownAuxiliaryOutputs", apply_start
    )


def test_power_request_safety_side_effects_run_without_a_physical_mode_change() -> None:
    apply_start = OUTPUT_SOURCE.index("static void App_OutputApplyPowerAction")
    apply_end = OUTPUT_SOURCE.index(
        "static void App_OutputShutDownAuxiliaryOutputs", apply_start
    )
    apply_source = OUTPUT_SOURCE[apply_start:apply_end]
    assert "if (!action.apply_mode)" not in apply_source
    require(
        "if (action.apply_mode && App_OutputPowerRequestAllowed(action.mode))",
        apply_source,
        "only the physical power-path write may be skipped for an unchanged mode",
    )
    require(
        "App_StateSetPowerStatus(requested_power_mode, charge_phase);",
        apply_source,
        "the confirmed request state must update even when GPIO power mode is unchanged",
    )
    apply_source = OUTPUT_SOURCE[apply_start:apply_end]
    require(
        "requested_power_mode != APP_POWER_MODE_CHARGE",
        apply_source,
        "PC14 may clear only when leaving requested CHARGE mode",
    )


def test_drive_children_are_revalidated_and_cleared_by_firmware() -> None:
    require(
        "App_PowerPathAllowsAuxiliaryOutput(io_status.power_mode)",
        OUTPUT_SOURCE,
        "NMOS owner must reject ON requests outside DRIVE mode",
    )
    require(
        "App_LedEnqueueState(false)",
        OUTPUT_SOURCE,
        "leaving DRIVE must request LIGHTS off",
    )
    require(
        "App_PowerPathAllowsAuxiliaryOutput(io_status.power_mode)",
        LED_SOURCE,
        "LIGHTS owner must reject ON requests outside DRIVE mode",
    )


def test_output_query_and_v33_version_are_exposed() -> None:
    require(
        "case APP_AT_COMMAND_QUERY_OUTPUT:",
        AT_TASK_SOURCE,
        "AT task must reply to AT+OUTPUT?",
    )
    require(
        'APP_FIRMWARE_VERSION "release-v3.3"',
        CONFIG_HEADER,
        "firmware version must advertise the v3.3 protocol",
    )


def test_at_task_returns_state_error_before_queueing_pc14_on() -> None:
    require(
        "case APP_AT_COMMAND_SET_CHARGE_BYPASS:",
        AT_TASK_SOURCE,
        "AT handler must dispatch the charge bypass command",
    )
    require(
        "App_OutputEnqueueChargeBypass",
        AT_TASK_SOURCE,
        "valid charge bypass requests must use the output owner queue",
    )


def test_pc14_has_no_unapproved_production_writer() -> None:
    allowed_paths = {
        ROOT / "Core/Src/gpio.c",
        ROOT / "App/Src/app_runtime.c",
        ROOT / "App/Src/app_output_task.c",
    }
    write_token = "HAL_GPIO_WritePin(CHARGE_BYPASS_GPIO_Port"

    for source_path in ROOT.glob("**/*.c"):
        if source_path in allowed_paths or "build" in source_path.parts:
            continue
        source = source_path.read_text(encoding="utf-8", errors="ignore")
        assert write_token not in source, f"unapproved PC14 writer: {source_path}"
