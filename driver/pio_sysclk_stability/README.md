# PIO Sysclk Stability

Driver for measuring the period between consecutive rising edges on one PPS-like input and inferring `clk_sys` scale error from that period.

## What it measures

- input condition: rising edge on `pps_pin`
- next event: the following rising edge on the same pin
- result: elapsed ticks across one full input period or a timeout sentinel

## Intended use

This driver is meant for measuring one-second PPS periods against a PIO state machine clock derived from `clk_sys`.

From the measured period it can report:

- apparent PPS period in nanoseconds using the configured nominal `sm_clk_hz`
- ppm error relative to exactly 1 second
- inferred actual `clk_sys` based on the configured nominal `clk_sys`

## Integration notes

- This is a dedicated validator-oriented driver and does not replace the existing two-pin input-capture path.
- The measurement resolution is one decrement per two PIO state-machine cycles.
- For PPS use, choose a timeout comfortably above 1 second, such as 1.5 seconds.