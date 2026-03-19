# Costas Array Generator for GPS PPS-Disciplined Ionospheric Sounding

This project implements a microcontroller-centered transmission timing engine for HF ionospheric sounding.

The device is intended to:

- lock transmission timing to GPS PPS,
- generate and transmit Costas-array symbols through an HF rig,
- measure actual RF-on timing at the rig output,
- later append timestamp telemetry (FSK) tied to the measured RF event.

## Mission Context

The MCU orchestrates multiple subsystems:

- HF rig control (PTT, frequency, mode selection such as USB-D)
- AD9850 programming (frequency/phase word loading + `FQ_UD` strobes)
- PPS-referenced transmit timeline generation (output compare scheduling)
- RF-on event timestamping (input capture from comparator/discrete detector)

Future prototypes may add ADC-based RF envelope characterization to reduce timing walk vs RF power level.

## End-to-End Timing Sequence

Nominal high-level sequence per transmission:

1. Wait for GPS PPS edge.
2. Apply PPS-to-array delay.
3. Assert PTT.
4. Wait amplifier/rig settle interval.
5. Emit Costas array symbol updates through AD9850 (`FQ_UD` scheduling + preloaded words).
6. Detect RF-on with comparator path and timestamp relative to PPS.
7. Continue/finish symbol schedule.
8. Encode timestamp telemetry as FSK and transmit after Costas block.
9. Deassert PTT and return to idle.

## Software Architecture (Target)

The architecture is layered so low-level timing primitives remain reusable while policy lives in dedicated schedulers/controllers.

### 1) Drivers (low-level hardware primitives)

- `driver/pio_timer_output_compare`: trigger-to-pulse timing primitive (PIO-based)
- `driver/pio_timer_input_capture`: edge-to-edge timing capture with timeout (PIO-based)
- (planned) AD9850 transport driver (bitbang/SPI-assisted)
- (planned) rig control GPIO/UART/CAT abstraction
- (planned) GPS PPS + timebase interface

### 2) Schedulers and Timeline Engine (mid-level)

- (planned) transmission event scheduler (sequence of compare events)
- (planned) Costas symbol scheduler (word preload + `FQ_UD` pulse plan)
- (planned) guard intervals and safety interlocks (PTT holdoff, timeout abort)

### 3) Application Control (high-level)

- mission state machine (`IDLE -> ARM -> TX -> MEASURE -> TELEMETRY -> COMPLETE`)
- timestamp estimation and quality metrics
- runtime configuration (profiles, symbol sets, settle times)

### 4) Validation and Test Harness

- `src/validation/main_validation.c`: interactive USB CDC menu
- module-specific validation entrypoints for input capture and output compare
- scope/logic-analyzer assisted verification workflows (AD2)

## Current Repository Status

Implemented now:

- PIO output compare driver with runtime pin selection and programmable delay/pulse widths
- PIO input capture driver with configurable pins and timeout behavior
- Dedicated validation build mode and interactive USB validation menu
- CMake presets/tasks for normal vs validation builds

Not implemented yet (top priorities):

- full AD9850 scheduler path for Costas symbols
- rig/PTT control state machine integration
- RF-on timestamp integration into full transmission flow
- telemetry encoding/transmission stage

## Build Modes

- Production mode: `normal` preset (`PIO_TIMER_CAPTURE_VALIDATION=OFF`)
- Validation mode: `validation` preset (`PIO_TIMER_CAPTURE_VALIDATION=ON`)

Typical commands:

- Configure production: `cmake --preset normal`
- Build production: `cmake --build --preset build-normal`
- Configure validation: `cmake --preset validation`
- Build validation: `cmake --build --preset build-validation`

## Architecture Roadmap

### Phase 1 — Deterministic timing foundation

- finalize scheduler module interface (separate from drivers)
- support burst/series event programming for output compare consumers
- define common timestamp units and rollover-safe arithmetic

### Phase 2 — AD9850 waveform control integration

- implement AD9850 word queueing and `FQ_UD` strobe control
- define Costas symbol plan format (frequency table + dwell/symbol length)
- verify symbol timing against PPS with logic analyzer

### Phase 3 — Full TX control path

- add PTT + settle timing into unified timeline
- integrate RF-on capture into mission execution
- handle abort/fault paths (no RF-on, GPS fault, timing overruns)

### Phase 4 — Measurement and telemetry

- estimate PPS-to-RF delay per shot
- implement FSK telemetry frame generation and TX insertion
- add quality fields (timeout count, jitter, confidence)

### Phase 5 — Hardware-assisted calibration (future)

- integrate ADC sampling for RF envelope slope/amplitude
- compensate comparator crossing walk vs power level
- maintain calibration tables across operating conditions

## Next Steps (Practical, Near-Term)

1. Write a dedicated scheduler design note (API, queue model, timing ownership, ISR/DMA policy).
2. Implement scheduler module skeleton with unit-testable logic (host-side where possible).
3. Add validation menu path for scheduler-driven multi-event sequences.
4. Define AD9850 transaction timing contract (word load and `FQ_UD` ordering guarantees).
5. Build first integrated PPS -> PTT -> Costas-start demonstrator.

## Hardware Integration Notes (Planned)

Final hardware is expected to integrate:

- MCU
- AD9850 DDS chain
- GPS receiver (PPS + serial time data)
- RF sampler/comparator front-end for RF-on detect
- HF rig interface circuitry

Detailed circuit design is currently TODO.
