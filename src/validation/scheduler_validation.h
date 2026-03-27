#ifndef SCHEDULER_VALIDATION_H
#define SCHEDULER_VALIDATION_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Configuration shared across scheduler validation modes.
 */
typedef struct {
    uint output_compare_pio_index;
    uint alarm_timer_pio_index;
    uint output_compare_sm;
    uint alarm_timer_sm;
    uint trigger_pin;
    uint output_pin;
    uint pps_pin;
    uint32_t sm_clk_hz;
    uint32_t timing_sys_clk_hz;
    uint32_t configured_sys_clk_hz;
    uint32_t output_pulse_us;

    uint ad9850_spi_index;
    uint32_t ad9850_spi_baud_hz;
    uint ad9850_sck_pin;
    uint ad9850_mosi_pin;
    bool ad9850_use_fqud_pin;
    uint ad9850_fqud_pin;
    uint32_t ad9850_fqud_pulse_us;
    bool ad9850_use_reset_pin;
    uint ad9850_reset_pin;
    uint32_t ad9850_sysclk_hz;

    uint32_t dt0_us;
    uint32_t symbol_count;
    uint32_t dts_us;
    int32_t load_offset_us;
} scheduler_validation_config_t;

/**
 * @brief Runs the interactive scheduler validation submenu.
 */
void scheduler_validation_run(const scheduler_validation_config_t *config);

#endif
