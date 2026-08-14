# Semantic Six-Temperature v3.2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add five semantically named component temperatures plus the battery temperature to firmware and host, using the confirmed PA1/PA0 inputs, a strict non-redundant v3.2 protocol, five-channel thermal protection, and a 3x2 temperature UI.

**Architecture:** Keep the existing single-conversion ADC runtime and synchronized rolling filter, expanding its semantic sensor model from three to five protected component channels. The firmware is the sole producer of a strict `release-v3.2` SENSE contract; the host model, chart buffer, fake firmware, and UI consume only that contract. Visual constants are finalized only after the user approves real light/dark renders.

**Tech Stack:** STM32F103C8T6, STM32 HAL, FreeRTOS/CMSIS-RTOS2, C11, MinGW native tests, GNU Arm CMake/Ninja build, STM32CubeProgrammer/ST-Link, UART/RS485, Python 3.11, PySide6, pytest.

---

## File map

Firmware repository `D:\Codes\STM32\stem-hub`:

- `App/Inc/app_types.h`: semantic sensor snapshot fields.
- `App/Inc/app_adc_filter.h`: seven synchronized ADC channels.
- `App/Inc/app_sensor_thermal.h`: five semantic validity/value inputs.
- `App/Inc/app_thermal_guard.h`, `App/Src/app_thermal_guard.c`: count-based fail-safe thermal evaluation.
- `App/Src/app_sensor_thermal.c`: semantic validity adapter for the thermal guard.
- `App/Src/app_sensor_task.c`: physical ADC reads, rolling means, conversion, and snapshot publication.
- `App/Src/app_at_task.c`: strict semantic SENSE reply.
- `App/Inc/app_config.h`: `release-v3.2` and semantic topology documentation.
- `Core/Src/adc.c`, `stem-hub.ioc`: PA0/ADC1 IN0 and PA1/ADC2 IN1 analog configuration.
- `tests/test_thermal_guard.c`, `tests/test_sensor_thermal.c`, `tests/test_adc_filter.c`: pure C behavior tests.
- `tests/test_sensor_contract.py`: pin, protocol-name, and source-contract regression checks.
- `README.md`, `上位机AT命令文档.md`, `钻杆mcu控制功能.md`: firmware, protocol, and hardware documentation.

Host repository `D:\Codes\STM32\stem-hub-host`:

- `stem_hub_host/models.py`: strict semantic `SenseData` parsing.
- `stem_hub_host/controller.py`: exact v3.2 handshake compatibility.
- `stem_hub_host/fake_firmware.py`: v3.2 fixture producer.
- `stem_hub_host/data_buffer.py`: semantic chart channels.
- `stem_hub_host/ui/main_window.py`: semantic mode-temperature aggregation.
- `stem_hub_host/ui/widgets/temp_grid.py`: six tiles in three rows by two columns.
- `stem_hub_host/ui/theme.py`, `stem_hub_host/ui/tab1_console.py`: top/lower row geometry.
- `stem_hub_host/visual_audit.py`, `tools/snap_tab1.py`, `tools/snap_visual_audit.py`: deterministic v3.2 render data.
- `tests/test_at_protocol.py`, `tests/test_connection_flow.py`, `tests/test_behavior_regressions.py`, `tests/test_widget_states.py`, `tests/test_console_layout.py`, `tests/test_output_controls.py`: protocol and UI regressions.
- `tests/golden/visual/*`, `tests/golden/visual/manifest.json`: approved visual baselines.
- `README.md`, `docs/power-path-at-contract.md`: matched-host documentation.

### Task 1: Make the thermal core accept all five semantic component sensors

**Files:**
- Modify: `App/Inc/app_thermal_guard.h`
- Modify: `App/Src/app_thermal_guard.c`
- Modify: `App/Inc/app_sensor_thermal.h`
- Modify: `App/Src/app_sensor_thermal.c`
- Modify: `tests/test_thermal_guard.c`
- Modify: `tests/test_sensor_thermal.c`

- [ ] **Step 1: Write failing five-sensor thermal tests**

Change the guard test to pass a fixed array and prove the fourth and fifth inputs trip and block clearing:

```c
int32_t temperatures[5] = {400, 400, 400, 601, 400};
assert(App_ThermalGuardUpdate(&guard, temperatures, 5U)
       == APP_THERMAL_TRIPPED);
temperatures[3] = 550;
temperatures[4] = 551;
assert(App_ThermalGuardUpdate(&guard, temperatures, 5U)
       == APP_THERMAL_NO_CHANGE);
temperatures[4] = 550;
assert(App_ThermalGuardUpdate(&guard, temperatures, 5U)
       == APP_THERMAL_CLEARED);
```

Change `NormalInputs()` to initialize `mcu`, `lm51770`, `mp4317`, `drv8874`, and `charge_mos` validity/value pairs, then independently invalidate DRV8874 and charge MOS and expect `APP_THERMAL_TRIPPED`.

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```powershell
$gcc='D:\Toolchains\MinGW\bin\gcc.exe'
& $gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc tests/test_thermal_guard.c App/Src/app_thermal_guard.c -o "$env:TEMP\test_thermal_guard.exe"
& $gcc -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc tests/test_sensor_thermal.c App/Src/app_sensor_thermal.c App/Src/app_thermal_guard.c -o "$env:TEMP\test_sensor_thermal.exe"
```

Expected: compilation fails because the count-based guard signature and semantic input members do not exist.

- [ ] **Step 3: Implement the minimal count-based guard and semantic adapter**

Use this public guard signature:

```c
AppThermalTransition App_ThermalGuardUpdate(
    AppThermalGuard *guard,
    const int32_t *temperatures_deci_c,
    size_t temperature_count);
```

Iterate all entries once when inactive and once when active. A null pointer or a count other than five is fail-safe and trips/keeps the guard active. Define `AppSensorThermalInputs` with these exact members:

```c
bool mcu_valid;
bool lm51770_valid;
bool mp4317_valid;
bool drv8874_valid;
bool charge_mos_valid;
int32_t mcu_temperature_deci_c;
int32_t lm51770_temperature_deci_c;
int32_t mp4317_temperature_deci_c;
int32_t drv8874_temperature_deci_c;
int32_t charge_mos_temperature_deci_c;
```

`App_SensorThermalGuardUpdate` builds a five-element array in that order and substitutes `INT32_MAX` for each invalid sensor. Battery validity remains outside the protection decision.

- [ ] **Step 4: Rebuild and run both focused tests**

Run the Step 2 commands followed by both executables. Expected: both exit 0 with no warnings.

- [ ] **Step 5: Commit the thermal slice**

```powershell
git add -- App/Inc/app_thermal_guard.h App/Src/app_thermal_guard.c App/Inc/app_sensor_thermal.h App/Src/app_sensor_thermal.c tests/test_thermal_guard.c tests/test_sensor_thermal.c
git commit -m "feat: protect five semantic temperature sensors"
```

### Task 2: Add PA1/PA0 acquisition and the strict semantic firmware contract

**Files:**
- Create: `tests/test_sensor_contract.py`
- Modify: `tests/test_adc_filter.c`
- Modify: `tests/test_batt_voltage_conversion.c`
- Modify: `App/Inc/app_types.h`
- Modify: `App/Inc/app_adc_filter.h`
- Modify: `App/Inc/app_config.h`
- Modify: `App/Src/app_sensor_task.c`
- Modify: `App/Src/app_at_task.c`
- Modify: `Core/Src/adc.c`
- Modify: `stem-hub.ioc`

- [ ] **Step 1: Write failing source-contract and rolling-count tests**

Add `assert(APP_ADC_ROLLING_CHANNEL_COUNT == 7U);` to `test_adc_filter.c`. Add a pytest contract that reads the real sources and asserts:

```python
def test_semantic_temperature_contract() -> None:
    assert '#define APP_FIRMWARE_VERSION "release-v3.2"' in CONFIG
    assert "ADC_CHANNEL_1" in SENSOR_TASK
    assert "ADC_CHANNEL_0" in SENSOR_TASK
    assert "MCU_C=%s" in AT_TASK
    assert "LM51770_C=%s" in AT_TASK
    assert "MP4317_C=%s" in AT_TASK
    assert "DRV8874_C=%s" in AT_TASK
    assert "CHARGE_MOS_C=%s" in AT_TASK
    for numbered_name in ("NTC1_C", "NTC2_C", "NTC3_C", "NTC4_C", "NTC5_C"):
        assert numbered_name not in AT_TASK

def test_physical_pin_contract() -> None:
    assert "PA0.Signal=ADC1_IN0" in IOC
    assert "PA1.Signal=ADC2_IN1" in IOC
    assert "GPIO_PIN_0|GPIO_PIN_4|GPIO_PIN_5" in ADC_SOURCE
    assert "GPIO_PIN_1|GPIO_PIN_6|GPIO_PIN_7" in ADC_SOURCE
```

Update the mirrored SENSE formatter test to expect only `MCU_C`, `LM51770_C`, `MP4317_C`, `DRV8874_C`, and `CHARGE_MOS_C` after `BATT_V`.

- [ ] **Step 2: Run tests and verify RED**

Run:

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests/test_sensor_contract.py -q
& 'D:\Toolchains\MinGW\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror -Itests/stubs -IApp/Inc tests/test_adc_filter.c App/Src/app_adc_filter.c -o "$env:TEMP\test_adc_filter.exe"
```

Expected: pytest reports the missing semantic/pin contract and the C test fails on channel count 5.

- [ ] **Step 3: Implement semantic snapshot and seven-channel sampling**

Replace `ntc1/2/3` in `AppSensorSnapshot` with:

```c
AppAnalogMeasure mcu_temperature;
AppAnalogMeasure lm51770_temperature;
AppAnalogMeasure mp4317_temperature;
AppAnalogMeasure drv8874_temperature;
AppAnalogMeasure charge_mos_temperature;
```

Set `APP_ADC_ROLLING_CHANNEL_COUNT` to `7U`. Use semantic filter enum values and read these channels in `App_SensorTask`:

```c
App_RuntimeReadChannel(&hadc1, ADC_CHANNEL_4, &raw);  /* battery NTC */
App_RuntimeReadChannel(&hadc1, ADC_CHANNEL_5, &raw);  /* battery voltage */
App_RuntimeReadAdc2Channel(ADC_CHANNEL_9, &raw);      /* MCU */
App_RuntimeReadAdc2Channel(ADC_CHANNEL_7, &raw);      /* LM51770 */
App_RuntimeReadAdc2Channel(ADC_CHANNEL_6, &raw);      /* MP4317 */
App_RuntimeReadAdc2Channel(ADC_CHANNEL_1, &raw);      /* DRV8874, PA1 pin 11 */
App_RuntimeReadChannel(&hadc1, ADC_CHANNEL_0, &raw);  /* charge MOS, PA0 pin 10 */
```

Require all seven channels to publish. Permit preview-only thermal evaluation only when all five protected channels are valid; do not advance any rolling window on a partial cycle.

- [ ] **Step 4: Configure physical analog pins and version**

In `stem-hub.ioc`, add PA0/ADC1_IN0 and PA1/ADC2_IN1 without enabling scan mode. In `Core/Src/adc.c`, initialize/deinitialize PA0 with the ADC1 PA4/PA5 group and PA1 with the ADC2 PA6/PA7 group. Change `APP_FIRMWARE_VERSION` to `release-v3.2` and update the topology comment with all semantic channels.

- [ ] **Step 5: Emit only the semantic SENSE line**

Format five temperature strings and emit exactly:

```c
"+SENSE:BATT_NTC=%s,BATT_V=%lu.%luV,MCU_C=%s,LM51770_C=%s,MP4317_C=%s,DRV8874_C=%s,CHARGE_MOS_C=%s,MOTOR_I=%lu.%luA,TICK=%lu,COUNT=%lu,STK_AT=%lu,STK_SENSOR=%lu,STK_MOTOR=%lu,TX_SP=%lu,TX_LS=%lu\r\nOK\r\n"
```

Do not retain numbered aliases in structs, local variables, format strings, or comments describing the current interface.

- [ ] **Step 6: Run focused tests and Debug build**

Run the Step 2 tests, `test_batt_voltage_conversion`, and the two thermal tests. Configure/build with bundled CMake, GNU Arm, and Ninja. Expected: all focused tests pass, ELF/HEX/BIN are regenerated, RAM remains below 100%, and Flash remains below 100%.

- [ ] **Step 7: Commit the firmware contract slice**

```powershell
git add -- tests/test_sensor_contract.py tests/test_adc_filter.c tests/test_batt_voltage_conversion.c App/Inc/app_types.h App/Inc/app_adc_filter.h App/Inc/app_config.h App/Src/app_sensor_task.c App/Src/app_at_task.c Core/Src/adc.c stem-hub.ioc
git commit -m "feat: add semantic six-temperature telemetry"
```

### Task 3: Move the host to the strict v3.2 semantic protocol

**Files:**
- Modify: `stem_hub_host/models.py`
- Modify: `stem_hub_host/controller.py`
- Modify: `stem_hub_host/fake_firmware.py`
- Modify: `stem_hub_host/data_buffer.py`
- Modify: `stem_hub_host/ui/main_window.py`
- Modify: `stem_hub_host/visual_audit.py`
- Modify: `tools/snap_tab1.py`
- Modify: `tools/snap_visual_audit.py`
- Modify: `tests/test_at_protocol.py`
- Modify: `tests/test_connection_flow.py`
- Modify: `tests/test_behavior_regressions.py`
- Modify: `tests/test_output_controls.py`

- [ ] **Step 1: Write failing strict parser and handshake tests**

Use this v3.2 fixture:

```python
line = (
    "+SENSE:BATT_NTC=25.1C,BATT_V=37.0V,MCU_C=24.9C,"
    "LM51770_C=35.2C,MP4317_C=ERR,DRV8874_C=41.3C,"
    "CHARGE_MOS_C=39.8C,MOTOR_I=1.2A,TICK=12345,COUNT=42,"
    "STK_AT=200,STK_SENSOR=180,STK_MOTOR=160,TX_SP=0,TX_LS=0"
)
```

Assert all five semantic attributes. Add a test that the old numbered line returns `None`, and connection tests that accept exactly `release-v3.2` but reject `release-v3.1`.

- [ ] **Step 2: Run focused pytest and verify RED**

Run:

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests/test_at_protocol.py tests/test_connection_flow.py -q
```

Expected: failures mention missing semantic `SenseData` fields and the old `release-v3.` prefix policy.

- [ ] **Step 3: Implement the strict model, producer, and compatibility gate**

Define these `SenseData` temperature attributes:

```python
batt_ntc: str
mcu_c: str
lm51770_c: str
mp4317_c: str
drv8874_c: str
charge_mos_c: str
```

Require the complete set of v3.2 field keys before constructing `SenseData`; do not use `.get()` defaults for required fields. Set `POWER_PROTOCOL_VERSION = "release-v3.2"`, require exact equality, and set `FakeFirmware.VERSION` to the same value. Make fake SENSE output only semantic fields.

- [ ] **Step 4: Rename every host consumer**

Replace numbered channel keys with `mcu_c`, `lm51770_c`, `mp4317_c`, `drv8874_c`, and `charge_mos_c` in `DataBuffer.CHANNELS`, ingestion, the main-window temperature aggregate, deterministic visual fixtures, and test constructors. Preserve `batt_ntc`, `batt_v`, and `motor_i` behavior.

- [ ] **Step 5: Run host protocol, behavior, and buffer tests**

Run:

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests/test_at_protocol.py tests/test_connection_flow.py tests/test_behavior_regressions.py tests/test_output_controls.py tests/test_sampling_rate.py -q
```

Expected: all selected tests pass with no numbered temperature attributes remaining under `stem_hub_host` or `tests`.

- [ ] **Step 6: Commit the host contract slice**

```powershell
git add -- stem_hub_host/models.py stem_hub_host/controller.py stem_hub_host/fake_firmware.py stem_hub_host/data_buffer.py stem_hub_host/ui/main_window.py stem_hub_host/visual_audit.py tools/snap_tab1.py tools/snap_visual_audit.py tests/test_at_protocol.py tests/test_connection_flow.py tests/test_behavior_regressions.py tests/test_output_controls.py
git commit -m "feat: adopt strict semantic telemetry v3.2"
```

### Task 4: Build the 3x2 temperature UI and move the upper row upward

**Files:**
- Modify: `stem_hub_host/ui/widgets/temp_grid.py`
- Modify: `stem_hub_host/ui/theme.py`
- Modify: `tests/test_widget_states.py`
- Modify: `tests/test_console_layout.py`

- [ ] **Step 1: Write failing tile, position, and row-ratio tests**

Assert the exact tile order and positions:

```python
assert [tile.title_label.text() for tile in grid._tiles()] == [
    "BATTERY", "MCU", "LM51770", "MP4317", "DRV8874", "CHG MOS",
]
assert grid.grid.indexOf(grid.tile_battery) >= 0
assert grid.grid.getItemPosition(grid.grid.indexOf(grid.tile_charge_mos)) == (2, 1, 1, 1)
assert theme.TOP_ROW_STRETCH == 520
assert theme.BOTTOM_ROW_STRETCH == 480
```

Update the value test to provide six v3.2 values and assert every label. Retain the existing animation-stop and theme-refresh assertions for all six tiles.

- [ ] **Step 2: Run focused UI tests and verify RED**

Run:

```powershell
$env:QT_QPA_PLATFORM='offscreen'
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests/test_widget_states.py tests/test_console_layout.py -q
```

Expected: failures report missing semantic tiles and the old 565/435 row ratio.

- [ ] **Step 3: Implement the responsive 3x2 grid**

Expose `self.grid` for deterministic position tests. Create semantic tile attributes and add them at `(0,0)` through `(2,1)` in the approved order. Return a six-item tuple from `_tiles()` and bind each to the matching `SenseData` attribute. Change the row stretch constants to 520/480. Reduce only temperature-tile vertical padding/gauge height if needed to avoid clipping; do not shrink fonts below the current readable sizes unless the render proves it necessary.

- [ ] **Step 4: Run focused and full host tests**

Run the Step 2 command, then:

```powershell
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m compileall -q stem_hub_host tools
& 'D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe' -m pytest tests -q
```

Expected: Python compilation and the full suite pass except visual golden comparisons that intentionally await approved baselines.

- [ ] **Step 5: Commit the renderable UI slice**

```powershell
git add -- stem_hub_host/ui/widgets/temp_grid.py stem_hub_host/ui/theme.py tests/test_widget_states.py tests/test_console_layout.py
git commit -m "feat: show six temperatures in a three-by-two grid"
```

### Task 5: Render both themes and obtain user visual approval

**Files:**
- Generate: `docs/semantic-six-ntc-v3.2-dark.png`
- Generate: `docs/semantic-six-ntc-v3.2-light.png`

- [ ] **Step 1: Generate deterministic 2048x1080 renders**

Run:

```powershell
$python='D:\Codes\STM32\stem-hub-host\env\release\Scripts\python.exe'
& $python tools/snap_tab1.py docs/semantic-six-ntc-v3.2-dark.png dark
& $python tools/snap_tab1.py docs/semantic-six-ntc-v3.2-light.png light
```

Expected: both PNGs exist at 2048x1080 and show six unclipped temperature tiles.

- [ ] **Step 2: Inspect renders locally**

Check that the separator moved upward, all six titles and values are legible, both bottom panels align, the AT console input remains visible, and no gauge/value overlaps occur.

- [ ] **Step 3: Present both PNGs and pause for the user**

Show the two absolute-path images. Do not update golden baselines or claim the UI final until the user explicitly approves or requests spacing changes.

- [ ] **Step 4: Apply requested visual-only adjustments**

If changes are requested, modify only `theme.py` and `temp_grid.py`, rerun the focused UI tests, regenerate both PNGs, and repeat Step 3. Commit approved constants with:

```powershell
git add -- stem_hub_host/ui/theme.py stem_hub_host/ui/widgets/temp_grid.py docs/semantic-six-ntc-v3.2-dark.png docs/semantic-six-ntc-v3.2-light.png
git commit -m "polish: finalize six-temperature console layout"
```

### Task 6: Update visual baselines and user documentation

**Files:**
- Modify: `tests/golden/visual/*`
- Modify: `tests/golden/visual/manifest.json`
- Modify: `D:\Codes\STM32\stem-hub\README.md`
- Modify: `D:\Codes\STM32\stem-hub\上位机AT命令文档.md`
- Modify: `D:\Codes\STM32\stem-hub\钻杆mcu控制功能.md`
- Modify: `D:\Codes\STM32\stem-hub-host\README.md`
- Modify: `D:\Codes\STM32\stem-hub-host\docs\power-path-at-contract.md`

- [ ] **Step 1: Regenerate approved visual baselines**

Run `tools/update_visual_baselines.py` using the release Python environment, then run `tests/test_visual_regression.py`. Expected: manifest entries match new images and visual regression passes.

- [ ] **Step 2: Update firmware and protocol documentation**

Document the exact six temperature labels, PA1/ADC2 IN1 and PA0/ADC1 IN0 mapping, HNTC0603 topology for the five component sensors, seven-channel synchronized rolling window, five-channel 60/55 C protection, strict SENSE line, and `release-v3.2` handshake. Remove current numbered-interface statements rather than keeping duplicate legacy tables.

- [ ] **Step 3: Update host documentation**

Document exact v3.2 matching, semantic chart channels, the 3x2 temperature layout, and the strict incompatibility with older numbered SENSE payloads.

- [ ] **Step 4: Verify documentation consistency**

Run searches for `release-v3.1`, `NTC1_C`, `NTC2_C`, `NTC3_C`, `NTC4_C`, and `NTC5_C` across current source/docs/tests. Hits are allowed only in historical dated design/plan files that explicitly describe the superseded contract.

- [ ] **Step 5: Commit documentation and baselines in each repository**

Use exact-path `git add` commands; never stage the firmware `.settings` files. Commit firmware docs with `docs: document semantic thermal telemetry v3.2` and host docs/baselines with `docs: align host with thermal telemetry v3.2`.

### Task 7: Full software, hardware, and RS485 verification

**Files:**
- Generate ignored artifacts under firmware `build/Debug`
- Generate ignored host test/build artifacts

- [ ] **Step 1: Run every firmware native test fresh**

Compile all 14 existing C tests plus the new Python source-contract test with warnings as errors, execute each binary, and require zero failures. Do not reuse pre-change executables.

- [ ] **Step 2: Run a fresh Debug firmware build**

Use bundled CMake 4.0.1, GNU Arm 13.3.1, and Ninja 1.13.1. Require exit 0, record RAM/Flash percentages, and confirm `build/Debug/stem-hub.elf`, `.hex`, and `.bin` exist.

- [ ] **Step 3: Run the complete host verification**

Run Python compilation, `pytest tests -q`, visual regression, and a five-second `--fake` application smoke test. Require no crash and no unexpected warnings.

- [ ] **Step 4: Flash through the connected ST-Link**

Run:

```powershell
& "$env:USERPROFILE\.agents\skills\stm32-cubemx-flash\scripts\flash_stm32.ps1" -ProjectRoot 'D:\Codes\STM32\stem-hub' -Configuration Debug -FirmwarePath 'D:\Codes\STM32\stem-hub\build\Debug\stem-hub.elf'
```

Expected: one probe, STM32F103 target, `Download verified successfully`, and reset.

- [ ] **Step 5: Discover the RS485 COM port and verify v3.2**

Enumerate `Get-CimInstance Win32_SerialPort` and select the connected USB serial adapter by current device identity, not a stale COM assumption. Query at the project baud rate and require:

```text
AT+VERSION? -> +VERSION:release-v3.2 then OK
AT+SENSE?   -> one semantic SENSE line containing all five component fields then OK
```

Require a rising `COUNT`, plausible parsed numeric temperatures or explicit `ERR`, and no numbered `NTC1_C` through `NTC5_C` tokens.

- [ ] **Step 6: Return the board to a safe state**

Send `AT+POWER=OFF`, `AT+NMOS1=OFF`, `AT+NMOS2=OFF`, and `AT+MOTOR=SLEEP`; require `OK` for each. Do not claim heated-sensor threshold validation unless the physical sensors are deliberately and safely heated.

### Task 8: Final audit, commits, and local master integration

- [ ] **Step 1: Audit every requirement against current evidence**

Verify physical pins, six temperatures, semantic-only protocol, five protected channels, v3.2 exact handshake, matching branches, 3x2 UI, approved render, RS485 behavior, ST-Link verification, documentation, and safe final hardware state.

- [ ] **Step 2: Confirm clean intended branch state**

Firmware may show only the user's two pre-existing `.settings` modifications; host must be clean. Confirm every feature change is committed on `codex/semantic-six-ntc-v3.2` in both repositories.

- [ ] **Step 3: Merge firmware locally into `master` and reverify**

Switch firmware to `master`, merge with `--no-ff`, rerun all firmware tests/build, and leave the `.settings` modifications untouched and uncommitted.

- [ ] **Step 4: Merge host locally into `master` and reverify**

Switch host to `master`, merge with `--no-ff`, rerun Python compilation and the complete pytest suite.

- [ ] **Step 5: Report exact evidence**

Report merge commit IDs, test counts, RAM/Flash, probe/target/image verification, COM port, v3.2 version reply, the semantic SENSE field set, safe-state command results, documentation paths, and any limitation on physical temperature stimulation.
