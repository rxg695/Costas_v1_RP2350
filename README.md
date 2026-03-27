# Costas_V1_RP2350

This repository is the RP2350-targeted continuation of the earlier RP2040 timing-engine work for PPS-disciplined Costas-array transmission experiments.

The long-term goal is to start a transmission from GPS PPS, step the AD9850 through a symbol plan, and measure when RF actually appears at the rig output.

## What is in the repository

- PIO timing drivers for output compare, input capture, and alarm scheduling
- An AD9850 driver built on the Pico SDK SPI peripheral
- A scheduler prototype that coordinates the alarm timer, output compare, and DDS writes
- A validation firmware build with a USB serial menu for hardware bring-up

## Current status

Working today:

- output compare driver with programmable delay and pulse width
- input capture driver with timeout handling
- alarm timer driver with host-side monotonic checks
- AD9850 write path, serial-enable sequence, and non-blocking transfer support
- scheduler validation for sequence building and timer orchestration

Still to be integrated:

- rig/PTT control
- full transmit state machine
- RF-on timestamp capture in the production path
- telemetry after the Costas burst

## Layout

- `driver/` reusable hardware-facing modules
- `src/scheduler/` scheduler prototype and public API
- `src/validation/` validation menu and module-specific test code
- `build/` normal build output
- `build_validation/` validation build output

## Build presets

- `normal` builds the production entry point in `src/main.c`
- `validation` builds the USB menu entry point in `src/validation/main_validation.c`

Typical commands:

- `cmake --preset normal`
- `cmake --build --preset build-normal`
- `cmake --preset validation`
- `cmake --build --preset build-validation`

The workspace already includes matching VS Code tasks for configure, build, and flash steps.

## Timing model

The intended transmit flow is:

1. wait for PPS
2. apply a programmable delay
3. key the rig
4. wait for the RF chain to settle
5. step through the Costas symbol plan
6. capture the actual RF-on time
7. append telemetry if required

Only part of that path is implemented end to end. The current codebase mainly provides the timing primitives and validation harness needed to build toward it.

In the current scheduler validation path, the generated output pulse is intentionally reused as the AD9850 `FQ_UD` latch pulse. Validation therefore checks both pulse timing and DDS-latch timing with the same signal.

## Validation workflow

Build the `validation` preset, flash it, open the USB serial console, and select a module from the menu. The validation firmware is the fastest way to confirm pin mapping, timing assumptions, and PIO program behavior on hardware.

Available modules cover:

- input capture
- output compare
- alarm timer
- AD9850 transport
- scheduler orchestration

## Near-term work

1. tighten first-event behavior in the scheduler
2. add PTT and rig-settle timing to the scheduler path
3. feed RF-on capture back into the transmit sequence
4. define the telemetry format and send path

## TODO

- Document the intentional shared-pin scheduler validation path where the output pulse is also the AD9850 `FQ_UD` pulse. The AD9850 driver can still bit-bang `FQ_UD` for non-timing-critical operations, but the timing-critical path is driven by the PIO output-compare state machine.
- Add a thorough scheduler timeline and timing-logic write-up that explains the full event chain, including the PPS rearm path, alarm scheduling, output-compare timing, SPI transfer timing, and the design assumption that each DDS write must complete before the next alarm boundary.
- Refactor PIO interrupt ownership and scheduler/alarm registries so multiple subsystems can coexist more cleanly instead of relying on one exclusive owner per PIO interrupt line.
- Audit and remove the remaining RP2040-era workspace and tooling assumptions. The main source tree is already targeted at `pico2`/RP2350, but IntelliSense, debug configuration, and host-specific task wiring still assume an ARM-only Cortex-Debug flow in places.

## Hardware notes

The expected hardware stack is:

- RP2350 board
- AD9850 DDS stage
- GPS receiver with PPS
- RF detector or comparator for RF-on sensing
- rig control interface

Board-level integration details are still evolving, so the software currently keeps hardware assumptions narrow and pushes most verification into the validation build.
