#ifndef PIO_SYSCLK_STABILITY_VALIDATION_H
#define PIO_SYSCLK_STABILITY_VALIDATION_H

#include <stdint.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

typedef struct {
    uint pps_pin;
    uint sm_clk_hz;
    uint32_t timeout_ns;
    uint32_t min_samples_per_run;
    uint32_t max_samples_per_run;
    uint32_t update_period_s;
    PIO pio;
    uint sm;
} pio_sysclk_stability_validation_config_t;

void pio_sysclk_stability_validation_run(const pio_sysclk_stability_validation_config_t *config);

#endif