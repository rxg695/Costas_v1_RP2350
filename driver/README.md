# Driver Modules

This folder contains reusable PIO-based driver modules shared by both production and validation builds.

## Structure

- `CMakeLists.txt` — aggregates driver manifests and exports shared source/PIO lists.
- `<driver_name>/driver_manifest.cmake` — per-driver manifest consumed by `driver/CMakeLists.txt`.

Current drivers:

- [pio_timer_input_capture](pio_timer_input_capture/README.md)
  - Measures elapsed time between configurable start/stop input edges.
- [pio_timer_output_compare](pio_timer_output_compare/README.md)
  - Schedules output pulses from trigger events with programmable delay and pulse width.
- [pio_alarm_timer](pio_alarm_timer/README.md)
  - Experimental command-driven PIO alarm timer with RX FIFO event reporting.

## Build integration

Top-level CMake includes this folder via `add_subdirectory(driver)` and consumes:

- `DRIVER_SOURCES`
- `DRIVER_PIO_FILES`

These are treated as shared driver assets in all build modes.

## Adding a new driver

1. Create a new `driver/<name>/` folder.
2. Add implementation and PIO files.
3. Add `<name>/driver_manifest.cmake` setting:
   - `DRIVER_MODULE_SOURCES`
   - `DRIVER_MODULE_PIO_FILES`
4. Include the manifest from `driver/CMakeLists.txt` and append to aggregate lists.
