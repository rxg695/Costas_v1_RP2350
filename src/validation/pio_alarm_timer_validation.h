#ifndef PIO_ALARM_TIMER_VALIDATION_H
#define PIO_ALARM_TIMER_VALIDATION_H

#include <stdint.h>

#include "pico/stdlib.h"

typedef struct {
    uint pio_index;
    uint sm;
    uint pps_pin;
    uint32_t sm_clk_hz;
    uint32_t first_alarm_tick;
    uint32_t alarm_step_ticks;
    uint32_t burst_count;
} pio_alarm_timer_validation_config_t;

// Runs interactive validation for pio_alarm_timer until user exits.
void pio_alarm_timer_validation_run(const pio_alarm_timer_validation_config_t *config);

#endif
