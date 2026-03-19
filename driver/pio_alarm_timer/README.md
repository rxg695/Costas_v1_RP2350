# PIO Alarm Timer (Experimental)

This module implements a command-driven timer/alarm state machine in PIO.

## Intent

- Maintain a free-running 32-bit timer counter in one SM.
- Consume command words from TX FIFO.
- Emit result words to RX FIFO.
- Use RX-not-empty interrupt on CPU side to service alarm outcomes.
- Gate reset/rearm release on PPS rising edge (WAIT PIN 0).

## Command/result contract

Command words (CPU -> PIO TX FIFO):

- `0x00000000`: reset/rearm command
- `T > 0`: alarm tick request

Startup requires an explicit first command from host (`pull block` at entry).

Result words (PIO -> CPU RX FIFO):

- `0xFFFFFFFF`: reset/rearm ACK
- `0x00000000`: late/missed alarm
- `T`: successful alarm at tick `T`

## Current implementation notes

- The core state machine is implemented in `pio_alarm_timer.pio`.
- Counter reset + ACK behavior for `cmd==0` is implemented.
- Reset/rearm path waits for PPS (`wait 0 pin`, `wait 1 pin`) before accepting alarms.
- Alarm match push (`push T`) is implemented for future ticks.
- Counter does not reset between alarm commands.
- Strict in-PIO late classification (`T <= counter`) is limited by instruction budget and compare primitives; host API monotonic guard remains the primary protection against stale/descending commands.

## Build integration

This module exports:

- `pio_alarm_timer.pio`
- `pio_alarm_timer.c`
- `pio_alarm_timer.h`

## API highlights

- `pio_alarm_timer_init(...)`
	- initializes one SM for alarm-timer operation and PPS wait pin.
- `pio_alarm_timer_queue_rearm(...)`
	- queues command `0` and clears host-side monotonic state.
- `pio_alarm_timer_queue_alarm(...)`
	- enqueues `T>0` alarm ticks.
	- guards against non-monotonic values (`next < last`).
	- on regression, issues rearm command and returns
		`PIO_ALARM_TIMER_ENQUEUE_ERR_NON_MONOTONIC`.
- `pio_alarm_timer_try_read_result(...)`
	- returns one RX FIFO result word when available.
- `pio_alarm_timer_decode_result(...)`
	- decodes raw result into typed kind (`REARM_ACK`, `LATE`, `FIRED`).
- `pio_alarm_timer_try_read_decoded_result(...)`
	- convenience helper combining read + decode.
- `pio_alarm_timer_set_rx_irq_callback(...)`
	- enables per-SM RX-not-empty IRQ callback dispatch.
- `pio_alarm_timer_clear_rx_irq_callback(...)`
	- disables per-SM RX IRQ callback dispatch.
