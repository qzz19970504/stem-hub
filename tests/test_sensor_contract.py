import re
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]


def read_repository_file(relative_path: str) -> str:
    return (REPOSITORY_ROOT / relative_path).read_text(encoding="utf-8")


def adc_gpio_block(source: str, instance: str, operation: str) -> str:
    msp_function = "HAL_ADC_MspInit" if operation == "HAL_GPIO_Init" else "HAL_ADC_MspDeInit"
    function_start = source.index(f"void {msp_function}")
    function_source = source[function_start:]
    instance_start = function_source.index(f"adcHandle->Instance=={instance}")
    next_instance = function_source.find("adcHandle->Instance==", instance_start + 1)
    instance_block = function_source[instance_start:]
    if next_instance != -1:
        instance_block = function_source[instance_start:next_instance]

    operation_match = re.search(
        rf"{operation}\(GPIOA,\s*(?:&GPIO_InitStruct|(?P<pins>[^;]+))\s*\);",
        instance_block,
    )
    assert operation_match is not None

    if operation == "HAL_GPIO_Init":
        prefix = instance_block[: operation_match.start()]
        pin_assignment = list(
            re.finditer(r"GPIO_InitStruct\.Pin\s*=\s*([^;]+);", prefix)
        )
        assert pin_assignment
        return pin_assignment[-1].group(1)

    return operation_match.group("pins")


def test_firmware_version_is_release_v3_2() -> None:
    config = read_repository_file("App/Inc/app_config.h")
    version = re.search(
        r'^\s*#define\s+APP_FIRMWARE_VERSION\s+"([^"]+)"',
        config,
        re.MULTILINE,
    )
    assert version is not None
    assert version.group(1) == "release-v3.2"


def test_component_temperature_channels_use_physical_adc_pins() -> None:
    sensor_task = read_repository_file("App/Src/app_sensor_task.c")
    assert re.search(
        r"App_RuntimeReadAdc2Channel\(\s*ADC_CHANNEL_1\s*,\s*&raw\s*\)",
        sensor_task,
    )
    assert re.search(
        r"App_RuntimeReadChannel\(\s*&hadc1\s*,\s*ADC_CHANNEL_0\s*,\s*&raw\s*\)",
        sensor_task,
    )


def test_sense_uses_semantic_component_temperature_fields() -> None:
    at_task = read_repository_file("App/Src/app_at_task.c")
    sense_format = re.search(r'"\+SENSE:[^\n]+', at_task)
    assert sense_format is not None
    sense_text = sense_format.group(0)
    semantic_fields = [
        "MCU_C=",
        "LM51770_C=",
        "MP4317_C=",
        "DRV8874_C=",
        "CHARGE_MOS_C=",
    ]
    positions = [sense_text.index(field) for field in semantic_fields]
    assert positions == sorted(positions)
    for numbered_field in ("NTC1_C=", "NTC2_C=", "NTC3_C=", "NTC4_C=", "NTC5_C="):
        assert numbered_field not in sense_text


def test_cubemx_maps_pa0_and_pa1_to_required_adc_instances() -> None:
    ioc = read_repository_file("stem-hub.ioc")
    required_lines = (
        "PA0.Signal=ADCx_IN0",
        "PA1.Signal=ADCx_IN1",
        "SH.ADCx_IN0.0=ADC1_IN0,IN0",
        "SH.ADCx_IN0.ConfNb=1",
        "SH.ADCx_IN1.0=ADC2_IN1,IN1",
        "SH.ADCx_IN1.ConfNb=1",
    )
    for required_line in required_lines:
        assert len(re.findall(rf"^{re.escape(required_line)}$", ioc, re.MULTILINE)) == 1

    assert not re.search(r"^PA0\.Signal=ADC1_IN0$", ioc, re.MULTILINE)
    assert not re.search(r"^PA1\.Signal=ADC2_IN1$", ioc, re.MULTILINE)


def test_generated_adc_gpio_groups_include_pa0_and_pa1() -> None:
    adc_source = read_repository_file("Core/Src/adc.c")
    for operation in ("HAL_GPIO_Init", "HAL_GPIO_DeInit"):
        adc1_pins = adc_gpio_block(adc_source, "ADC1", operation)
        adc2_pins = adc_gpio_block(adc_source, "ADC2", operation)
        assert "GPIO_PIN_0" in adc1_pins
        assert "GPIO_PIN_1" in adc2_pins
