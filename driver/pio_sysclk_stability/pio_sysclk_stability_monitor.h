#ifndef PIO_SYSCLK_STABILITY_MONITOR_H
#define PIO_SYSCLK_STABILITY_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/pio_sysclk_stability/pio_sysclk_stability.h"

typedef struct {
    PIO pio;
    uint sm;
    uint pps_pin;
    uint32_t sm_clk_hz;
    uint32_t timeout_ns;
    uint32_t update_interval_valid_samples;
} pio_sysclk_stability_monitor_config_t;

typedef struct {
    bool running;
    bool pps_ready;
    uint32_t configured_sys_clk_hz;
    uint32_t effective_sys_clk_hz;
    uint32_t update_interval_valid_samples;
    uint32_t last_batch_valid_samples;
    uint32_t updates_published;
    uint32_t total_valid_samples;
    uint32_t total_timeouts;
    uint32_t average_sys_clk_hz;
    uint32_t min_sys_clk_hz;
    uint32_t max_sys_clk_hz;
    uint64_t average_period_ns;
    uint64_t min_period_ns;
    uint64_t max_period_ns;
} pio_sysclk_stability_monitor_snapshot_t;

bool pio_sysclk_stability_monitor_start(const pio_sysclk_stability_monitor_config_t *config);

bool pio_sysclk_stability_monitor_stop(void);

bool pio_sysclk_stability_monitor_is_running(void);

uint32_t pio_sysclk_stability_monitor_get_effective_sys_clk_hz(void);

bool pio_sysclk_stability_monitor_get_snapshot(pio_sysclk_stability_monitor_snapshot_t *snapshot_out);

#endif