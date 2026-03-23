# Driver Modules

This directory holds the reusable hardware-facing pieces shared by the normal and validation builds.

## Included drivers

- [pio_timer_input_capture/README.md](pio_timer_input_capture/README.md): measures the time between a start edge and a stop edge
- [pio_timer_output_compare/README.md](pio_timer_output_compare/README.md): emits a delayed pulse after a trigger
- [pio_alarm_timer/README.md](pio_alarm_timer/README.md): command-driven timer with PPS-based rearm behavior
- [ad9850_driver/README.md](ad9850_driver/README.md): AD9850 transport over the Pico SDK SPI layer

Together these modules provide the low-level timing and DDS control pieces used by the scheduler layer in [src/scheduler/README.md](../src/scheduler/README.md).

## Build integration

Each driver directory provides a `driver_manifest.cmake` file. `driver/CMakeLists.txt` includes those manifests and collects the exported source files and `.pio` files into the lists used by the top-level build.

## Adding a driver

1. Create `driver/<name>/`.
2. Add the implementation, header, and any `.pio` file.
3. Export them from `driver_manifest.cmake`.
4. Include that manifest from `driver/CMakeLists.txt`.

Keep new drivers focused on hardware behavior. Scheduling policy and mission logic belong higher in the tree.
