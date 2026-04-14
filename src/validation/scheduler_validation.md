# Scheduler Validation

Interactive test module for the scheduler prototype in `src/scheduler/`.

## Submenu modes

- `a` build the output-compare and alarm sequences and print them
- `b` run initialization only and print the resulting state
- `c` initialize, prepare, and preload the scheduler
- `d` run timer orchestration through to completion
- `e` run the fuller integration path with interactive control
- `f` auto-run Costas bursts until stopped, using the current top-level clock-source selection

## Default inputs

- `dt0 = 15 us`
- `symbol_count = 8`
- `dts = 10 us`
- `load_offset = 7 us`
- `ad9850_spi_baud_hz = 10 MHz`
- default frequencies: `1500 900 1800 600 2400 2100 1200 0`

Default pin mapping:

- `SCK = GP6`
- `MOSI = GP7`
- `FQ_UD = GP5`
- `RESET = GP9`
- `PPS = GP8`

In the default validation setup, the scheduler output pulse is also the AD9850 `FQ_UD` latch pulse. That shared-pin arrangement is intentional for timing validation.

## Sequence model

For `N` symbols, the validation code builds:

- output sequence: `out[0] = dt0`, then `out[k] = out[k - 1] + dts[k - 1]`
- alarm sequence: `alarm[0] = dt0 - load_offset`, then `alarm[k] = alarm[k - 1] + dts[k - 1]`, plus one final trailing marker

The printed output sequence is the intended pulse-start schedule. The continuous output-compare driver uses a different internal representation:

- the first output event is queued with the full `dt0`
- each later output event is queued with `dts[k] - pulse_width`

That subtraction is required because continuous output compare measures each later delay from the end of the previous pulse, while the scheduler request is expressed as pulse-start to pulse-start spacing.

With that correction in place, the observed `FQ_UD` pulse spacing should match the requested symbol spacing instead of drifting late by one pulse width per symbol.

This module is useful for checking scheduler state transitions and timing assumptions before integrating more application logic around it.
