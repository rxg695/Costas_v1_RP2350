#ifndef PIO_TIMER_INPUT_CAPTURE_VALIDATION_H
#define PIO_TIMER_INPUT_CAPTURE_VALIDATION_H

#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/pio.h"

typedef struct {
	uint start_pin;
	uint stop_pin;
	uint sm_clk_hz;
	uint32_t timeout_ns;
	uint32_t sample_count;
	PIO pio;
	uint sm;
} pio_timer_input_capture_validation_config_t;

// Runs input-capture validation until user requests stop over USB CDC.
// Returns to caller when user presses 'q'.
// Requires stdio to be initialized by the caller.
void pio_timer_input_capture_validation_run(const pio_timer_input_capture_validation_config_t *config);

#endif
