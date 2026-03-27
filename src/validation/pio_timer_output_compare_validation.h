#ifndef PIO_TIMER_OUTPUT_COMPARE_VALIDATION_H
#define PIO_TIMER_OUTPUT_COMPARE_VALIDATION_H

#include <stdint.h>

#include "pico/stdlib.h"

/**
 * @brief Configuration for the output-compare validation loop.
 */
typedef struct {
    uint pio_index;
    uint sm;
    uint trigger_pin;
    uint output_pin;
    bool continuous_mode;
    uint32_t sm_clk_hz;
    uint32_t timing_sm_clk_hz;
    uint32_t compare_ns;
    uint32_t pulse_ns;
} pio_timer_output_compare_validation_config_t;

/**
 * @brief Runs the output-compare validation loop until the user exits.
 */
void pio_timer_output_compare_validation_run(const pio_timer_output_compare_validation_config_t *config);

#endif
