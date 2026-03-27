# PIO Sysclk Stability Validation

Interactive validator for measuring the period between consecutive PPS edges and inferring `clk_sys` scale error from that period.

## What it reports

- staged run summaries from a configurable minimum to maximum run size
- rolling updates printed periodically during long campaigns
- a final overall summary after the full staged run completes

## Suggested settings

- `timeout_ns`: at least 1.5 s for a 1 PPS input
- `min_samples_per_run`: small run size for quick spot checks, e.g. 5
- `max_samples_per_run`: largest run size for the final summary, e.g. 200
- `update_period_s`: periodic progress interval, e.g. 10
- `sm_clk_hz`: high enough for fine resolution, but below the actual `clk_sys`

The staged schedule grows by powers of ten from the minimum run size up to the maximum run size, and repeats smaller runs more times than larger runs so the early feedback arrives quickly while still ending with a deeper final run.