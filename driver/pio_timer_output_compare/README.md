# PIO Timer Output Compare

Driver for generating a pulse after a trigger edge using a PIO state machine.

## What it does

- waits for a rising edge on `trigger_pin`
- counts down a host-supplied delay
- drives `output_pin` high for a host-supplied pulse width
- returns to idle or keeps consuming queued events in continuous mode

## Public types and constants

### `pio_timer_output_compare_mode_t`

- `PIO_TIMER_OUTPUT_COMPARE_MODE_ONE_SHOT`
- `PIO_TIMER_OUTPUT_COMPARE_MODE_CONTINUOUS`

### Queue-related constants

- `PIO_TIMER_OUTPUT_COMPARE_TX_FIFO_WORDS = 8`
- `PIO_TIMER_OUTPUT_COMPARE_WORDS_PER_EVENT = 2`
- `PIO_TIMER_OUTPUT_COMPARE_STOP_COMPARE_TICKS = 0`

The driver joins the FIFO for TX use, so one event always consumes two words: delay first, pulse width second.

## Modes

- one-shot: each arm request produces one trigger-to-pulse event
- continuous: the PIO program consumes a stream of `(compare_ticks, pulse_ticks)` pairs until it sees the stop sentinel

Continuous mode is the path intended for low-jitter symbol timing because the CPU no longer has to re-arm the state machine for every edge.

## API reference

### `pio_timer_output_compare_init(...)`

Initializes one state machine with:

- the selected PIO block and SM index
- a loaded program offset
- runtime-selected trigger and output pins
- a requested state-machine clock
- the desired playback mode

The driver configures the output pin through `SET PINS` and maps the trigger pin through the SM input base.

### `pio_timer_output_compare_arm(...)`

Queues one event. In one-shot mode this means “arm the next trigger.” In continuous mode it is just an alias for queueing one more event in the stream.

### `pio_timer_output_compare_queue_event(...)`

Blocking enqueue of one `(compare_ticks, pulse_ticks)` pair.

### `pio_timer_output_compare_try_queue_event(...)`

Non-blocking enqueue helper. Returns `false` if there is not enough room for both words.

### `pio_timer_output_compare_queue_stop(...)`

Queues the stop sentinel used in continuous mode. When the PIO program consumes it, playback ends and the SM returns to waiting for the next trigger edge.

### `pio_timer_output_compare_try_queue_stop(...)`

Non-blocking version of the stop helper.

### `pio_timer_output_compare_ns_to_ticks(...)`

Converts nanoseconds to state-machine ticks using the requested SM clock. This helper is suitable for setup code and validation tools, but the caller still needs to account for fixed PIO overhead if sub-cycle accuracy matters.

## Timing model

Both the delay and pulse width are expressed in state-machine loop ticks. In practice:

- delay is approximately `compare_ticks / sm_clk_hz`
- pulse width is approximately `pulse_ticks / sm_clk_hz`

There is still fixed overhead around trigger detection and pin updates, so treat the conversion helpers as setup aids, not exact cycle-level proofs.

## FIFO limits

The driver joins the FIFOs for TX use, which gives 8 words with the current RP2 FIFO layout. Each queued event consumes 2 words, so four full events fit before the host has to feed more data.

## Typical usage

### One-shot use

1. initialize the state machine
2. convert the desired delay and pulse width to ticks
3. call `pio_timer_output_compare_arm(...)`
4. drive the trigger pin and observe the resulting pulse

### Continuous use

1. initialize the state machine in continuous mode
2. queue the event stream
3. optionally keep feeding events as FIFO space becomes available
4. queue the stop sentinel when the sequence should end

## Limits

`compare_ticks` and `pulse_ticks` are 32-bit values. Very long delays are possible at low state-machine clocks, but any conversion from nanoseconds must still fit in `uint32_t`.

## Integration notes

- The driver does not own DMA or refill policy. Higher-level code must keep the FIFO fed in continuous mode.
- `compare_ticks = 0` is reserved for the stop sentinel in continuous mode, so callers should not use it as a normal event marker there.
- The same API is used by both simple validation code and the scheduler, which is why both blocking and non-blocking queue helpers exist.
