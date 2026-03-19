# PIO Alarm Timer Validation Logic

This validation module provides an interactive test loop for `driver/pio_alarm_timer`.

## Purpose

Validate command/result behavior of the PIO alarm timer:

- `0` command -> rearm ACK (`0xFFFFFFFF`)
- increasing `T` alarms -> fired results (`T`)
- stale/late alarms -> `0`
- descending enqueue request -> host-side monotonic guard + automatic rearm

## Runtime controls

- `r`: queue rearm command
- `a`: queue one alarm at next tick
- `b`: queue burst of alarms
- `d`: queue descending tick intentionally (guard test)
- `q`: return to main validation menu

## Observability

Results are consumed through the driver RX IRQ callback API and printed as:

- `alarm_result rearm_ack=N`
- `alarm_result late=N`
- `alarm_result fired=N last_tick=T`
