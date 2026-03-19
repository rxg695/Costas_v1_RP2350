#ifndef PIO_TIMER_OUTPUT_COMPARE_VALIDATION_H
#define PIO_TIMER_OUTPUT_COMPARE_VALIDATION_H

#include <stdint.h>

#include "pico/stdlib.h"

typedef struct {
    uint pio_index;
    uint sm;
    uint trigger_pin;
    uint output_pin;
    uint32_t sm_clk_hz;
    uint32_t compare_ns;
    uint32_t pulse_ns;
} pio_timer_output_compare_validation_config_t;

// Runs output-compare validation until user requests stop over USB CDC.
// Returns to caller when user presses 'q'.
void pio_timer_output_compare_validation_run(const pio_timer_output_compare_validation_config_t *config);

#endif
