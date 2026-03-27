#ifndef VALIDATION_CONFIG_H
#define VALIDATION_CONFIG_H

#include <stdint.h>

// =============================================================================
// Validation Configuration
// =============================================================================
//
//
// Sections:
// 1. Console behavior
// 2. Simple timing instruments
// 3. PPS-based timing instruments
// 4. DDS bench wiring
// 5. Scheduler end-to-end timing path
// 6. Magic numbers and protocol constants
//
// As a rule:
// - top sections are ordinary bench/setup knobs
// - bottom section is implementation detail territory

// =============================================================================
// 1. Validation Console Behavior
// =============================================================================
// This section controls how the USB serial validation console starts up and how
// much text input it accepts. These values do not change the hardware timing
// model; they only affect the operator interface.

// Short command buffer used for single-character commands and small numeric
// values typed into the console.
#define VALIDATION_CONSOLE_LINE_BUFFER_SIZE 16u

// Larger buffer used by general-purpose numeric prompts.
#define VALIDATION_CONSOLE_PROMPT_BUFFER_SIZE 64u

// Poll interval while waiting for a host to open the USB serial connection.
#define VALIDATION_CONSOLE_USB_WAIT_MS 10u

// Extra delay after the host connects so the first menu text is visible.
#define VALIDATION_CONSOLE_USB_SETTLE_MS 50u

// Number of top-level menu entries currently exposed by the validation console.
#define VALIDATION_CONSOLE_MODULE_COUNT 7u

// Menu value meaning: use the configured clk_sys value exactly as requested.
#define VALIDATION_CLOCK_SOURCE_SYSTEM_VALUE 0u

// Menu value meaning: use the PPS-derived effective clk_sys estimate.
#define VALIDATION_CLOCK_SOURCE_EFFECTIVE_VALUE 1u

// =============================================================================
// 2. Simple Timing Instruments
// =============================================================================
// These modules are the easiest to understand because they measure or generate
// one timing relationship at a time.
//
// 2A. Input capture measures the time from one rising edge to another.
// 2B. Output compare waits for a trigger and then emits a delayed pulse.

// -----------------------------------------------------------------------------
// 2A. Input Capture: start edge -> stop edge
// -----------------------------------------------------------------------------
// Use this when you want to answer: "How much time elapsed between these two
// observed signals?"
//
// Typical bench wiring:
// - START pin sees the event begin
// - STOP pin sees the event end
// - the validator reports jitter, minimum, maximum, and average timing

// Default PIO block used by the input-capture validator.
#define VALIDATION_INPUT_CAPTURE_DEFAULT_PIO_INDEX 2u

// Default state machine used by that validator.
#define VALIDATION_INPUT_CAPTURE_DEFAULT_SM 0u

// Rising edge on this GPIO starts the measurement.
#define VALIDATION_INPUT_CAPTURE_DEFAULT_START_PIN 6u

// Rising edge on this GPIO stops the measurement.
#define VALIDATION_INPUT_CAPTURE_DEFAULT_STOP_PIN 8u

// Nominal PIO state-machine clock used for capture timing.
#define VALIDATION_INPUT_CAPTURE_DEFAULT_SM_CLK_HZ 100000000u

// Timeout used when the stop edge never arrives.
#define VALIDATION_INPUT_CAPTURE_DEFAULT_TIMEOUT_NS 100000000u

// Number of successful samples collected before printing one summary block.
#define VALIDATION_INPUT_CAPTURE_DEFAULT_SAMPLE_COUNT 10000u

// -----------------------------------------------------------------------------
// 2B. Output Compare: trigger edge -> delayed output pulse
// -----------------------------------------------------------------------------
// Use this when you want to answer: "After event A happens, can I produce a
// clean pulse on event B after a deterministic delay?"
//
// Typical bench wiring:
// - TRIGGER pin provides the incoming event
// - OUTPUT pin emits the delayed pulse
// - compare_ns is the trigger-to-pulse delay
// - pulse_ns is the pulse width

// Default PIO block used by output compare.
#define VALIDATION_OUTPUT_COMPARE_DEFAULT_PIO_INDEX 1u

// Default state machine used by output compare.
#define VALIDATION_OUTPUT_COMPARE_DEFAULT_SM 0u

// Input GPIO that triggers the delayed pulse.
#define VALIDATION_OUTPUT_COMPARE_DEFAULT_TRIGGER_PIN 6u

// Output GPIO that emits the pulse.
#define VALIDATION_OUTPUT_COMPARE_DEFAULT_OUTPUT_PIN 9u

// Nominal state-machine clock used by the output-compare engine.
#define VALIDATION_OUTPUT_COMPARE_DEFAULT_SM_CLK_HZ 100000000u

// Delay from trigger edge to output pulse start.
#define VALIDATION_OUTPUT_COMPARE_DEFAULT_COMPARE_NS 1000u

// Width of the emitted output pulse.
#define VALIDATION_OUTPUT_COMPARE_DEFAULT_PULSE_NS 1000u

// =============================================================================
// 3. PPS-Based Timing Instruments
// =============================================================================
// These modules assume you have a PPS-like reference signal and want to reason
// about time relative to that reference.
//
// 3A. Alarm timer treats PPS as a scheduling origin and queues future events.
// 3B. Background monitor estimates effective clk_sys from PPS.
// 3C. Legacy staged stability validation performs explicit measurement campaigns.

// -----------------------------------------------------------------------------
// 3A. Alarm Timer: PPS -> absolute future alarm ticks
// -----------------------------------------------------------------------------
// This module is about absolute scheduling after PPS, not relative pulse delay.
// It answers: "After PPS arrives, can I queue events for specific future ticks
// and see them fire where I expect?"

// Default PIO block used by the alarm timer.
#define VALIDATION_ALARM_TIMER_DEFAULT_PIO_INDEX 0u

// Default state machine used by the alarm timer.
#define VALIDATION_ALARM_TIMER_DEFAULT_SM 0u

// GPIO carrying the PPS reference edge.
#define VALIDATION_ALARM_TIMER_DEFAULT_PPS_PIN 6u

// Nominal state-machine clock used by the alarm-timer engine.
#define VALIDATION_ALARM_TIMER_DEFAULT_SM_CLK_HZ 100000000u

// First future alarm tick queued after a PPS rearm.
#define VALIDATION_ALARM_TIMER_DEFAULT_FIRST_TICK 1000u

// Tick spacing between consecutive queued alarms.
#define VALIDATION_ALARM_TIMER_DEFAULT_STEP_TICKS 1000u

// Number of alarms queued by the burst helper.
#define VALIDATION_ALARM_TIMER_DEFAULT_BURST_COUNT 8u

// -----------------------------------------------------------------------------
// 3B. Background Clock Monitor: PPS -> effective clk_sys estimate
// -----------------------------------------------------------------------------
// This monitor runs in the background on core 1. It measures PPS periods and
// publishes an "effective" system clock that other validators can use for time
// conversions and reports.
//
// This does not reprogram clk_sys. It only changes the math used to interpret
// elapsed time when the operator selects the effective clock source.

// Default PIO block used by the background clock monitor.
#define VALIDATION_SYSCLK_MONITOR_DEFAULT_PIO_INDEX 2u

// Default state machine used by the background clock monitor.
#define VALIDATION_SYSCLK_MONITOR_DEFAULT_SM 1u

// GPIO carrying the PPS reference used for clk_sys estimation.
#define VALIDATION_SYSCLK_MONITOR_DEFAULT_PPS_PIN 16u

// Nominal monitor state-machine clock. (kept the same as
// the actual sysclk value to avoid overloading the monitor with stability-induced 
// timing errors). In Hertz.
#define VALIDATION_SYSCLK_MONITOR_DEFAULT_SM_CLK_HZ 150000000u

// Timeout used to decide that a PPS edge was missed.
#define VALIDATION_SYSCLK_MONITOR_DEFAULT_TIMEOUT_NS 1200000000u

// Number of valid PPS samples averaged before publishing a new estimate.
#define VALIDATION_SYSCLK_MONITOR_DEFAULT_BATCH_SIZE 32u

// -----------------------------------------------------------------------------
// 3C. Legacy Staged Stability Validation parameters
// -----------------------------------------------------------------------------
// This section is for the old validation module for the PPS-derived effective 
// clock stability. It is not used by the new background monitor, 
// are deprecated, and will be removed soon. 

// Default PIO block used by the sysclk_stability module.
#define VALIDATION_SYSCLK_STABILITY_DEFAULT_PIO_INDEX 2u

// Default state machine used by the sysclk_stability module.
#define VALIDATION_SYSCLK_STABILITY_DEFAULT_SM 1u

// PPS pin.
#define VALIDATION_SYSCLK_STABILITY_DEFAULT_PPS_PIN 16u

// Nominal state-machine clock used during validator PPS runs (kept the same as
// the actual sysclk value to avoid overloading the monitor with stability-induced 
// timing errors). In Hertz.
#define VALIDATION_SYSCLK_STABILITY_DEFAULT_SM_CLK_HZ 150000000u

// Timeout used during validator PPS runs, in nanoseconds.
#define VALIDATION_SYSCLK_STABILITY_DEFAULT_TIMEOUT_NS 1500000000u

// Smallest run size in the validator sequence, number of samples.
#define VALIDATION_SYSCLK_STABILITY_DEFAULT_MIN_SAMPLES_PER_RUN 5u

// Largest run size in the validator sequence, number of samples.
#define VALIDATION_SYSCLK_STABILITY_DEFAULT_MAX_SAMPLES_PER_RUN 200u

// How often progress is printed during the validator sequence (in seconds, not ticks).
#define VALIDATION_SYSCLK_STABILITY_DEFAULT_UPDATE_PERIOD_S 10u

// =============================================================================
// 4. DDS Bench Wiring: AD9850
// =============================================================================
// This section describes the normal AD9850 bench assumptions.
//
// Mental model:
// - SPI shifts the serial frame into the DDS
// - FQ_UD latches that frame into active output state
// - RESET optionally forces a known startup condition
// - dds_sysclk_hz is the reference clock used to compute FTW values

// Default SPI peripheral used for AD9850 communication.
#define VALIDATION_AD9850_DEFAULT_SPI_INDEX 0u

// Fallback SPI peripheral if no valid choice is supplied.
#define VALIDATION_AD9850_FALLBACK_SPI_INDEX 0u

// Normal fast SPI speed for bench validation.
#define VALIDATION_AD9850_DEFAULT_SPI_BAUD_HZ 10000000u

// Conservative fallback SPI speed for uncertain wiring or unspecified config.
#define VALIDATION_AD9850_FALLBACK_SPI_BAUD_HZ 1000000u

// GPIO mapped to the AD9850 serial clock input.
#define VALIDATION_AD9850_DEFAULT_SCK_PIN 6u

// GPIO mapped to the AD9850 serial data input.
#define VALIDATION_AD9850_DEFAULT_MOSI_PIN 7u

// GPIO used to pulse FQ_UD and latch the written frame.
#define VALIDATION_AD9850_DEFAULT_FQUD_PIN 8u

// Width of the FQ_UD pulse in microseconds.
#define VALIDATION_AD9850_DEFAULT_FQUD_PULSE_US 1u

// GPIO used for optional RESET control.
#define VALIDATION_AD9850_DEFAULT_RESET_PIN 9u

// Reference clock feeding the DDS core. FTW calculations assume this value.
#define VALIDATION_AD9850_DEFAULT_DDS_SYSCLK_HZ 125000000u

// Startup output frequency used when the validator begins.
#define VALIDATION_AD9850_DEFAULT_FREQUENCY_HZ 1000000u

// Startup phase word used when the validator begins.
#define VALIDATION_AD9850_DEFAULT_PHASE 0u

// Startup power-down state.
#define VALIDATION_AD9850_DEFAULT_POWER_DOWN 0u

// Frequency increment used by the interactive +/- commands.
#define VALIDATION_AD9850_DEFAULT_STEP_HZ 1000u

// =============================================================================
// 5. Scheduler Validation: PPS -> Alarm -> DDS Load -> Output Pulse
// =============================================================================
// This section is the closest thing to an end-to-end system description in the
// validation firmware.
//
// The scheduler validator ties together:
// - PPS as the master reference
// - alarm scheduling for when a symbol update should happen
// - AD9850 SPI writes for the next symbol frequency
// - an output pulse that, in the current validation wiring, is also the AD9850
//   FQ_UD latch pulse
//
// Important timing terms:
// - dt0_us: delay from PPS to the first symbol event
// - dts_us: spacing between later symbol events
// - load_offset_us: offset between the alarm/DDS-load side and the output pulse
//
// A new developer should start here if they want to understand how the project
// intends to sequence symbol timing relative to PPS.

// PIO block used for scheduler-driven output compare.
#define VALIDATION_SCHEDULER_DEFAULT_OUTPUT_COMPARE_PIO_INDEX 1u

// PIO block used for scheduler-driven alarm timing.
#define VALIDATION_SCHEDULER_DEFAULT_ALARM_TIMER_PIO_INDEX 0u

// State machine used for scheduler output compare.
#define VALIDATION_SCHEDULER_DEFAULT_OUTPUT_COMPARE_SM 1u

// State machine used for scheduler alarm timing.
#define VALIDATION_SCHEDULER_DEFAULT_ALARM_TIMER_SM 3u

// Trigger pin used in the scheduler validation setup.
#define VALIDATION_SCHEDULER_DEFAULT_TRIGGER_PIN 16u

// Output pin used in the scheduler validation setup. On the current bench this
// pulse also serves as the AD9850 FQ_UD latch pulse.
#define VALIDATION_SCHEDULER_DEFAULT_OUTPUT_PIN 8u

// PPS reference pin used in the scheduler validation setup.
#define VALIDATION_SCHEDULER_DEFAULT_PPS_PIN 16u

// Nominal scheduler state-machine clock.
#define VALIDATION_SCHEDULER_DEFAULT_SM_CLK_HZ 100000000u

// Width of the scheduler-generated pulse.
#define VALIDATION_SCHEDULER_DEFAULT_OUTPUT_PULSE_US 1u

// Time from PPS to the first symbol pulse.
#define VALIDATION_SCHEDULER_DEFAULT_DT0_US 20u

// Number of symbols in the generated default test sequence.
#define VALIDATION_SCHEDULER_DEFAULT_SYMBOL_COUNT 8u

// Spacing between later symbols in that sequence.
#define VALIDATION_SCHEDULER_DEFAULT_DTS_US 100000u

// Offset between the alarm/DDS-load event and the pulse schedule.
#define VALIDATION_SCHEDULER_DEFAULT_LOAD_OFFSET_US 10

// Default frequency table used to generate a quick scheduler run.
#define VALIDATION_SCHEDULER_DEFAULT_FREQ_HZ_LIST {14151500u, 14150900u, 14151800u, 14150600u, 14152400u, 14152100u, 14151200u, 0u}

// Number of entries in the default scheduler frequency list above.
#define VALIDATION_SCHEDULER_DEFAULT_FREQ_COUNT 8u

// =============================================================================
// 6. Advanced / Magic Values
// =============================================================================
// Stop here unless you are intentionally changing low-level behavior.
//
// These values describe implementation details such as:
// - protocol field widths
// - byte-reversal masks
// - internal queue assumptions
// - tick-to-time conversion factors
// - growth rules for staged measurement campaigns

// RP2350 has three PIO blocks and the validation code assumes all three exist.
#define VALIDATION_PIO_INSTANCE_COUNT 3u

// Shared timebase constants used by conversion math.
#define VALIDATION_USEC_PER_SEC 1000000ull
#define VALIDATION_NSEC_PER_SEC 1000000000ull

// Saturation limit used when a conversion must clamp to a 32-bit result.
#define VALIDATION_U32_MAX_VALUE 0xFFFFFFFFu

// Input capture advances one logical tick per two state-machine cycles, so its
// nanosecond conversion uses 2e9 rather than 1e9.
#define VALIDATION_INPUT_CAPTURE_TICK_NUMERATOR_NS 2000000000ull

// Output compare tracks one program-load slot per PIO block.
#define VALIDATION_OUTPUT_COMPARE_VALIDATION_PIO_COUNT VALIDATION_PIO_INSTANCE_COUNT

// Assumed TX FIFO capacity for output compare.
#define VALIDATION_OUTPUT_COMPARE_TX_FIFO_CAPACITY_WORDS 8u

// One output-compare event consists of compare delay plus pulse width.
#define VALIDATION_OUTPUT_COMPARE_EVENT_WORDS 2u

// Convenience burst size used by the output-compare validator.
#define VALIDATION_OUTPUT_COMPARE_BURST_QUEUE_COUNT 4u

// Alarm timer also tracks one program-load slot per PIO block.
#define VALIDATION_ALARM_TIMER_VALIDATION_PIO_COUNT VALIDATION_PIO_INSTANCE_COUNT

// One alarm-timer tick currently corresponds to five state-machine cycles.
#define VALIDATION_ALARM_TIMER_CYCLES_PER_TICK 5u

// Zero-sample staged PPS runs are promoted to this minimum sample count.
#define VALIDATION_SYSCLK_STABILITY_MIN_SAMPLES_FALLBACK 1u

// Each staged PPS phase grows by this multiplier.
#define VALIDATION_SYSCLK_STABILITY_RUN_SCALE_FACTOR 10u

// Smaller staged PPS phases repeat more often according to this divisor.
#define VALIDATION_SYSCLK_STABILITY_REPETITION_DIVISOR 2ull

// Scheduler request ticks are internally scaled before reaching output compare.
#define VALIDATION_SCHEDULER_OUTPUT_SCALE 5u

// AD9850 phase field is five bits wide.
#define VALIDATION_AD9850_PHASE_MASK 0x1Fu

// Generic byte-extraction mask for FTW/debug formatting.
#define VALIDATION_AD9850_BYTE_MASK 0xFFu

// FTW and frame layout sizes used by AD9850 packing logic.
#define VALIDATION_AD9850_FTW_BYTE_COUNT 4u
#define VALIDATION_AD9850_FRAME_BYTE_COUNT 5u
#define VALIDATION_AD9850_FTW_WIDTH_BITS 32u

// Byte extraction shifts for a 32-bit FTW.
#define VALIDATION_AD9850_SHIFT_8 8u
#define VALIDATION_AD9850_SHIFT_16 16u
#define VALIDATION_AD9850_SHIFT_24 24u

// These masks and shifts implement per-byte bit reversal because the AD9850
// expects LSB-first serial data while the SPI controller shifts MSB-first.
#define VALIDATION_AD9850_REVERSE_MASK_A 0xF0u
#define VALIDATION_AD9850_REVERSE_MASK_B 0x0Fu
#define VALIDATION_AD9850_REVERSE_MASK_C 0xCCu
#define VALIDATION_AD9850_REVERSE_MASK_D 0x33u
#define VALIDATION_AD9850_REVERSE_MASK_E 0xAAu
#define VALIDATION_AD9850_REVERSE_MASK_F 0x55u
#define VALIDATION_AD9850_REVERSE_SHIFT_NIBBLE 4u
#define VALIDATION_AD9850_REVERSE_SHIFT_PAIR 2u
#define VALIDATION_AD9850_REVERSE_SHIFT_BIT 1u

// Helper constants for selecting spi1 and for binary debug formatting.
#define VALIDATION_AD9850_SPI_INDEX_1 1u
#define VALIDATION_AD9850_BINARY_MSB_INDEX 7

#endif