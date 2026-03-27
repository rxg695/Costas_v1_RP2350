# AD9850 Driver

Low-level AD9850 support built on the Pico SDK SPI peripheral.

## Responsibilities

- build the 40-bit AD9850 frame
- convert frequency in hertz to FTW
- perform the AD9850 serial-enable sequence
- write frames in blocking or non-blocking form
- optionally pulse `FQ_UD` and `RESET`

## Out of scope

This layer does not own symbol scheduling, queue management, or timing policy. It just moves bytes and control pulses.

## Public types

### `ad9850_frame_t`

Packed 5-byte AD9850 frame in transmit order.

- `bytes[0..3]`: FTW, least-significant byte first
- `bytes[4]`: control byte with phase in bits `7:3` and power-down in bit `2`

The AD9850 serial port expects those fields in that byte order, but each byte is
shifted least-significant bit first on the wire. The Pico SDK SPI block only
supports MSB-first shifting on this target, so the driver bit-reverses each
byte before transmission.

### `ad9850_driver_config_t`

Static configuration for one driver instance.

- `spi`: `spi0` or `spi1`
- `spi_baud_hz`: SPI clock used for frame writes
- `sck_pin`, `mosi_pin`: GPIO pins mapped to the SPI peripheral
- `use_fqud_pin`, `fqud_pin`: optional latch pin control
- `fqud_pulse_us`: high time of the `FQ_UD` pulse in microseconds
- `use_reset_pin`, `reset_pin`: optional hardware reset control
- `dds_sysclk_hz`: DDS reference clock used by FTW conversion

### `ad9850_driver_t`

Runtime state owned by the caller after initialization. The struct tracks whether the serial interface has been enabled, and whether a non-blocking transfer is currently active.

## Frame layout

The transmitted bytes are:

1. FTW bits 7:0
2. FTW bits 15:8
3. FTW bits 23:16
4. FTW bits 31:24
5. control byte with phase in bits 7:3 and power-down in bit 2

On the wire, each of those bytes is transmitted least-significant bit first as
required by the AD9850 serial-load format.

## API reference

### `ad9850_driver_init(...)`

Initializes SPI and any optional control pins. The driver starts in a locked state, so writes are still rejected until the serial-enable sequence is run.

Returns `false` if the config is incomplete or obviously invalid.

### `ad9850_driver_serial_enable(...)`

Runs the AD9850 startup sequence required by this driver:

1. pulse `RESET` if that pin is enabled
2. manually pulse `W_CLK` once and pulse `FQ_UD` to latch the strapped parallel startup word

On success, the driver unlocks the write path.

### `ad9850_driver_make_frame(...)`

Builds a 40-bit frame from:

- a 32-bit FTW
- a 5-bit phase value
- a power-down flag

Returns `false` if the phase is out of range or the output pointer is null.

### `ad9850_driver_frequency_hz_to_ftw(...)`

Converts a requested output frequency to an FTW using the configured DDS clock:

`FTW = floor((frequency_hz * 2^32) / dds_sysclk_hz)`

This helper is appropriate when the caller wants the driver to derive FTWs from human-scale frequency values instead of supplying frames directly.

### `ad9850_driver_write_frame_blocking(...)`

Writes one frame over SPI and waits for the transfer to complete.

Returns `false` if:

- the driver is not initialized
- serial-enable has not been run
- the frame pointer is null
- the SPI write does not complete successfully

### `ad9850_driver_apply_frame_blocking(...)`

Writes a frame and optionally pulses `FQ_UD` afterward. This is the easiest API for bench testing and simple bring-up code.

The `FQ_UD` pulse width comes from `ad9850_driver_config_t.fqud_pulse_us` and is shared by the blocking, startup, and non-blocking paths.

### `ad9850_driver_start_apply_nonblocking(...)`

Starts a non-blocking write sequence. The caller must later advance it with `ad9850_driver_service_nonblocking(...)` until `ad9850_driver_take_nonblocking_result(...)` reports completion.

This is the API the scheduler uses to overlap DDS writes with the rest of the timing path.

### `ad9850_driver_service_nonblocking(...)`

Advances the in-progress transfer by pushing bytes whenever the SPI peripheral can accept them. Safe for IRQ-driven call sites.

### `ad9850_driver_take_nonblocking_result(...)`

Reports whether the last non-blocking transfer completed successfully. It returns `false` when no new completion result is available yet.

### `ad9850_driver_reset(...)`

Pulses the optional reset pin and clears the serial-enabled state. After calling this, the host must run `ad9850_driver_serial_enable(...)` again before any more writes.

## Typical call sequences

### Blocking setup and apply

1. initialize the driver
2. run `ad9850_driver_serial_enable(...)`
3. convert the requested frequency to FTW
4. build a frame
5. apply the frame with `ad9850_driver_apply_frame_blocking(...)`

### Non-blocking update path

1. initialize the driver
2. run `ad9850_driver_serial_enable(...)`
3. build the next frame
4. call `ad9850_driver_start_apply_nonblocking(...)`
5. service the transfer until a completion result is available

If the reset path is used later, the serial-enable sequence must be run again before more writes are accepted.

## Why the write guard exists

The AD9850 serial interface expects a defined reset and latch sequence. The driver keeps writes locked until `ad9850_driver_serial_enable(...)` succeeds so the calling code cannot quietly skip that step.

## Integration notes

- `FQ_UD` is optional in the struct, but `ad9850_driver_serial_enable(...)` currently expects it to be present.
- The driver does not validate whether the chosen pins are legal SPI pins for the selected peripheral. That remains a board-level responsibility.
- The non-blocking path is single-transfer only. Callers must wait for completion before starting the next frame.
