#ifndef AD9850_VALIDATION_H
#define AD9850_VALIDATION_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Configuration for the interactive AD9850 validation loop.
 */
typedef struct {
    uint spi_index;
    uint32_t spi_baud_hz;
    uint sck_pin;
    uint mosi_pin;
    bool use_fqud_pin;
    uint fqud_pin;
    uint32_t fqud_pulse_us;
    bool use_reset_pin;
    uint reset_pin;
    uint32_t dds_sysclk_hz;
    uint32_t frequency_hz;
    uint8_t phase;
    bool power_down;
} ad9850_validation_config_t;

/**
 * @brief Runs the AD9850 validation loop until the user exits.
 */
void ad9850_validation_run(const ad9850_validation_config_t *config);

#endif
