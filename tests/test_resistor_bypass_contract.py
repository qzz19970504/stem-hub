from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAIN_HEADER = (ROOT / "Core/Inc/main.h").read_text(encoding="utf-8")
GPIO_SOURCE = (ROOT / "Core/Src/gpio.c").read_text(encoding="utf-8")
IOC = (ROOT / "stem-hub.ioc").read_text(encoding="utf-8")
RUNTIME_SOURCE = (ROOT / "App/Src/app_runtime.c").read_text(encoding="utf-8")


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
