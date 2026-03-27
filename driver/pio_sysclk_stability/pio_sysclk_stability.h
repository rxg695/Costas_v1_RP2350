#ifndef PIO_SYSCLK_STABILITY_H
#define PIO_SYSCLK_STABILITY_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#define PIO_SYSCLK_STABILITY_TIMEOUT_SENTINEL 0xffffffffu

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
    uint pps_pin;
    uint32_t configured_sys_clk_hz;
    uint32_t sm_clk_hz;
    uint32_t timeout_ns;
    uint32_t timeout_loops;
} pio_sysclk_stability_t;

void pio_sysclk_stability_init(pio_sysclk_stability_t *capture,
                               PIO pio,
                               uint sm,
                               uint pps_pin,
                               uint32_t sm_clk_hz,
                               uint32_t timeout_ns);

bool pio_sysclk_stability_poll(pio_sysclk_stability_t *capture,
                               uint32_t *elapsed_ticks,
                               bool *timed_out);

uint64_t pio_sysclk_stability_ticks_to_ns(const pio_sysclk_stability_t *capture,
                                          uint32_t ticks);

int32_t pio_sysclk_stability_ticks_to_ppm(const pio_sysclk_stability_t *capture,
                                          uint32_t ticks);

uint32_t pio_sysclk_stability_estimate_sysclk_hz(const pio_sysclk_stability_t *capture,
                                                 uint32_t ticks);

#endif