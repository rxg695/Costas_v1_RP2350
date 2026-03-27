#ifndef PIO_ALARM_TIMER_VALIDATION_H
#define PIO_ALARM_TIMER_VALIDATION_H

#include <stdint.h>

#include "pico/stdlib.h"

/**
 * @brief Configuration for the alarm-timer validation loop.
 */
typedef struct {
    uint pio_index;
    uint sm;
    uint pps_pin;
    uint32_t sm_clk_hz;
    uint32_t timing_sm_clk_hz;
    uint32_t first_alarm_tick;
    uint32_t alarm_step_ticks;
    uint32_t burst_count;
} pio_alarm_timer_validation_config_t;

/**
 * @brief Runs the alarm-timer validation loop until the user exits.
 */
void pio_alarm_timer_validation_run(const pio_alarm_timer_validation_config_t *config);

#endif
