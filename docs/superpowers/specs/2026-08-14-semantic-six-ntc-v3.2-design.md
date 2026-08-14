# Semantic Six-Temperature Telemetry v3.2 Design

**Date:** 2026-08-14

**Repositories:** `stem-hub`, `stem-hub-host`

**Matched branch:** `codex/semantic-six-ntc-v3.2`

## Goal

Extend the firmware and host from four displayed temperatures (battery plus three
numbered NTCs) to six displayed temperatures (battery plus five semantically
named component sensors). The firmware and host move together to one current,
non-redundant protocol. No numbered NTC telemetry fields or legacy parser aliases
remain in the v3.2 implementation.

## Hardware mapping

Physical package pins are authoritative where the original channel description
conflicted with the STM32F103C8T6 LQFP48 pinout.

| Temperature | Physical input | ADC channel |
|---|---|---|
| Battery | PA4, pin 14 | ADC1 IN4 |
| MCU | PB1, pin 19 | ADC2 IN9 |
| LM51770 | PA7, pin 17 | ADC2 IN7 |
| MP4317 | PA6, pin 16 | ADC2 IN6 |
| DRV8874 | PA1, pin 11 | ADC2 IN1 |
| LM51770 charge MOS | PA0, pin 10 | ADC1 IN0 |

The five component sensors use the existing HNTC0603-103F3450FA lookup table
and the existing `3V3 -- NTC -- ADC -- 470 ohm -- GND` divider. The battery NTC
keeps its separate 3435K lookup table.

## Firmware data model and acquisition

- Replace numbered snapshot/filter/thermal names with `mcu`, `lm51770`,
  `mp4317`, `drv8874`, and `charge_mos` names.
- Add PA0 and PA1 as analog inputs in CubeMX configuration and generated ADC
  GPIO initialization while retaining the current dynamically selected,
  single-conversion ADC access pattern.
- Expand the synchronized rolling window from five ADC values to seven:
  battery NTC, battery voltage, and five component NTCs.
- Publish a snapshot only after all seven readings in a sampling cycle succeed.
  If a non-protected battery channel alone fails, use preview means for the five
  protected component channels without advancing the published window.
- Reuse one conversion function and one NTC table for all five component sensors.
- Keep static RAM growth minimal because the merged baseline already uses about
  94% of the 20 KiB RAM region.

## Thermal protection

MCU, LM51770, MP4317, DRV8874, and charge-MOS temperatures all participate in
the existing fail-safe thermal latch. Any protected channel above 60.0 C, any
invalid conversion, or any protected ADC read failure trips the latch. All five
must be valid and at or below 55.0 C to clear it. Battery NTC remains display-only.
The existing output-stop, motor-sleep, command guard, and manual recovery
semantics remain unchanged.

## AT protocol

`AT+SENSE?` returns the fields below in this order:

```text
+SENSE:BATT_NTC=<temp>,BATT_V=<volts>,MCU_C=<temp>,LM51770_C=<temp>,MP4317_C=<temp>,DRV8874_C=<temp>,CHARGE_MOS_C=<temp>,MOTOR_I=<amps>,TICK=<tick>,COUNT=<count>,STK_AT=<bytes>,STK_SENSOR=<bytes>,STK_MOTOR=<bytes>,TX_SP=<count>,TX_LS=<count>
```

Firmware version becomes `release-v3.2`. The v3.2 host accepts only the semantic
fields above. It does not parse `NTC1_C` through `NTC5_C`, and the firmware does
not emit both old and new fields.

## Host model and UI

- Replace numbered `SenseData` attributes and plot channels with semantic names.
- Update fake firmware, parsing tests, buffers, charts, labels, and all consumers
  to the v3.2 contract.
- Arrange the temperature card as three rows by two columns:

```text
BATTERY   | MCU
LM51770   | MP4317
DRV8874   | CHG MOS
```

- Reduce the console page top-row share from 56.5% to an initial target near
  52%, giving the lower row enough height for three temperature rows. Fine-tune
  tile padding, gauge height, and the exact split from real 2048x1080 light and
  dark renders.
- Present the actual renders to the user before finalizing the visual constants
  and golden images. Protocol and firmware work may continue independently, but
  the UI styling is not final until the render is approved.

## Verification

1. Use test-first changes for semantic parsing/formatting, seven-channel rolling
   acquisition, five-channel thermal decisions, and the 3x2 widget layout.
2. Run every firmware native C test with warnings as errors and build Debug with
   GNU Arm.
3. Run the complete host pytest suite, Python compilation, visual regression,
   and application smoke test.
4. Render connected console screenshots in both themes for user approval.
5. Flash the verified ELF through the connected ST-Link using SWD under reset.
6. Query `AT+VERSION?` and `AT+SENSE?` over the detected RS485 serial port and
   verify the v3.2 version plus every semantic temperature field.
7. Return all outputs and the motor to the safe OFF/SLEEP state.
8. Update firmware and host documentation, commit both matched branches, then
   merge each branch locally into its respective `master` only after all gates
   pass.

## Rejected alternatives

- Emitting both numbered and semantic fields was rejected because it creates a
  redundant wire contract.
- Accepting legacy numbered fields in the v3.2 host was rejected because the
  user requested both sides to follow only the latest interface.
- Adding a second `AT+SENSE2?` command was rejected because it duplicates query
  behavior and increases long-term protocol maintenance.
