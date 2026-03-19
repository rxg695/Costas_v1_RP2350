# Input Capture Validation Logic

This document describes the validation loop implemented in `pio_timer_input_capture_validation.c`.

Companion API header: `pio_timer_input_capture_validation.h`.

## Purpose

Runs continuous validation of the `pio_timer_input_capture` driver by:

- collecting measured intervals (`start_pin` rising to `stop_pin` rising),
- counting timeout events,
- reporting block-level and run-level jitter statistics.

Parameters are provided by `pio_timer_input_capture_validation_config_t`.

## Runtime flow

1. Wait for USB CDC to be opened.
2. Initialize `pio_timer_input_capture` with validation pins and timing config.
3. Poll capture results in a tight loop.
4. For each valid sample:
   - convert ticks to ns,
   - update min/max,
   - update running mean/stddev (Welford),
   - emit block summary every `SAMPLE_COUNT` samples.
5. For each timeout:
   - increment block and total timeout counters.
6. Check USB CDC input for `q` and return to caller when requested.

## Invocation

This logic is called from `src/validation/main_validation.c` through:

- `pio_timer_input_capture_validation_run(&config)`

Config fields:

- `start_pin`, `stop_pin`
- `sm_clk_hz`
- `timeout_ns`
- `sample_count`
- `pio`, `sm`

## Statistics reported

Per block (`SAMPLE_COUNT` valid samples):

- average delay (ns)
- stddev (ns)
- min/max (ticks + ns)
- jitter (ticks + ns)
- block timeout count

Run-wide (cumulative):

- total valid samples
- min/max (ticks + ns)
- jitter (ticks + ns)
- total timeout count

## Configuration constants

- `SM_CLK_HZ` = state machine timing base
- `TIMEOUT_NS` = timeout window per measurement
- `CAPTURE_START_PIN` / `CAPTURE_STOP_PIN` = validation pin pair
- `SAMPLE_COUNT` = block report size
