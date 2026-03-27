#include "driver/pio_sysclk_stability/pio_sysclk_stability_monitor.h"

#include "hardware/gpio.h"
#include "pico/critical_section.h"
#include "pico/multicore.h"

#define PIO_SYSCLK_STABILITY_MONITOR_DEFAULT_BATCH 32u
#define PIO_SYSCLK_STABILITY_MONITOR_STARTUP_BLINK_US 250000u
#define PIO_SYSCLK_STABILITY_MONITOR_FAULT_BLINK_US 100000u

typedef struct {
    bool initialized;
    bool launch_requested;
    bool running;
    bool stop_requested;
    bool pps_ready;
    uint32_t configured_sys_clk_hz;
    uint32_t effective_sys_clk_hz;
    uint32_t update_interval_valid_samples;
    uint32_t last_batch_valid_samples;
    uint32_t updates_published;
    uint32_t total_valid_samples;
    uint32_t total_timeouts;
    uint64_t sum_sys_clk_hz;
    uint64_t sum_period_ns;
    uint32_t min_sys_clk_hz;
    uint32_t max_sys_clk_hz;
    uint64_t min_period_ns;
    uint64_t max_period_ns;
} pio_sysclk_stability_monitor_state_t;

static critical_section_t monitor_lock;
static pio_sysclk_stability_monitor_config_t monitor_config;
static pio_sysclk_stability_monitor_state_t monitor_state;

static void pio_sysclk_stability_monitor_set_led(bool enabled)
{
#ifdef PICO_DEFAULT_LED_PIN
    static bool led_initialized = false;

    if (!led_initialized) {
        gpio_init(PICO_DEFAULT_LED_PIN);
        gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
        led_initialized = true;
    }

    gpio_put(PICO_DEFAULT_LED_PIN, enabled ? 1u : 0u);
#else
    (void) enabled;
#endif
}

static void pio_sysclk_stability_monitor_update_led(bool published_once,
                                                    bool fault_active,
                                                    uint64_t now_us,
                                                    uint64_t *last_led_toggle_us,
                                                    bool *led_enabled)
{
    if (last_led_toggle_us == NULL || led_enabled == NULL) {
        return;
    }

    if (fault_active) {
        if ((now_us - *last_led_toggle_us) >= PIO_SYSCLK_STABILITY_MONITOR_FAULT_BLINK_US) {
            *led_enabled = !*led_enabled;
            *last_led_toggle_us = now_us;
            pio_sysclk_stability_monitor_set_led(*led_enabled);
        }
        return;
    }

    if (!published_once) {
        if ((now_us - *last_led_toggle_us) >= PIO_SYSCLK_STABILITY_MONITOR_STARTUP_BLINK_US) {
            *led_enabled = !*led_enabled;
            *last_led_toggle_us = now_us;
            pio_sysclk_stability_monitor_set_led(*led_enabled);
        }
        return;
    }

    if (!*led_enabled) {
        *led_enabled = true;
        pio_sysclk_stability_monitor_set_led(true);
    }
}

static void pio_sysclk_stability_monitor_core1_entry(void)
{
    pio_sysclk_stability_t capture;
    pio_sysclk_stability_init(&capture,
                              monitor_config.pio,
                              monitor_config.sm,
                              monitor_config.pps_pin,
                              monitor_config.sm_clk_hz,
                              monitor_config.timeout_ns);

    critical_section_enter_blocking(&monitor_lock);
    monitor_state.running = true;
    monitor_state.stop_requested = false;
    monitor_state.pps_ready = false;
    monitor_state.configured_sys_clk_hz = capture.configured_sys_clk_hz;
    monitor_state.effective_sys_clk_hz = capture.configured_sys_clk_hz;
    critical_section_exit(&monitor_lock);

    pio_sysclk_stability_monitor_set_led(false);

    uint64_t batch_sum_sys_clk_hz = 0u;
    uint32_t batch_valid_samples = 0u;
    uint64_t last_led_toggle_us = time_us_64();
    bool led_enabled = false;
    bool fault_active = false;

    while (true) {
        bool stop_requested = false;
        uint64_t now_us = time_us_64();

        critical_section_enter_blocking(&monitor_lock);
        stop_requested = monitor_state.stop_requested;
        bool published_once = monitor_state.updates_published != 0u;
        critical_section_exit(&monitor_lock);

        if (stop_requested) {
            break;
        }

        uint32_t elapsed_ticks = 0u;
        bool timed_out = false;
        if (!pio_sysclk_stability_poll(&capture, &elapsed_ticks, &timed_out)) {
            pio_sysclk_stability_monitor_update_led(published_once,
                                                    fault_active,
                                                    now_us,
                                                    &last_led_toggle_us,
                                                    &led_enabled);
            tight_loop_contents();
            continue;
        }

        critical_section_enter_blocking(&monitor_lock);
        if (timed_out) {
            monitor_state.total_timeouts++;
            monitor_state.pps_ready = false;
            batch_sum_sys_clk_hz = 0u;
            batch_valid_samples = 0u;
            critical_section_exit(&monitor_lock);
            fault_active = true;
            pio_sysclk_stability_monitor_update_led(published_once,
                                                    fault_active,
                                                    now_us,
                                                    &last_led_toggle_us,
                                                    &led_enabled);
            continue;
        }

        fault_active = false;
        monitor_state.pps_ready = true;

        uint32_t estimated_sys_clk_hz =
            pio_sysclk_stability_estimate_sysclk_hz(&capture, elapsed_ticks);
        uint64_t period_ns = pio_sysclk_stability_ticks_to_ns(&capture, elapsed_ticks);
        batch_sum_sys_clk_hz += estimated_sys_clk_hz;
        batch_valid_samples++;
        monitor_state.total_valid_samples++;
        monitor_state.sum_sys_clk_hz += estimated_sys_clk_hz;
        monitor_state.sum_period_ns += period_ns;

        if (monitor_state.total_valid_samples == 1u) {
            monitor_state.min_sys_clk_hz = estimated_sys_clk_hz;
            monitor_state.max_sys_clk_hz = estimated_sys_clk_hz;
            monitor_state.min_period_ns = period_ns;
            monitor_state.max_period_ns = period_ns;
        } else {
            if (estimated_sys_clk_hz < monitor_state.min_sys_clk_hz) {
                monitor_state.min_sys_clk_hz = estimated_sys_clk_hz;
            }
            if (estimated_sys_clk_hz > monitor_state.max_sys_clk_hz) {
                monitor_state.max_sys_clk_hz = estimated_sys_clk_hz;
            }
            if (period_ns < monitor_state.min_period_ns) {
                monitor_state.min_period_ns = period_ns;
            }
            if (period_ns > monitor_state.max_period_ns) {
                monitor_state.max_period_ns = period_ns;
            }
        }

        if (batch_valid_samples >= monitor_state.update_interval_valid_samples) {
            monitor_state.effective_sys_clk_hz =
                (uint32_t) ((batch_sum_sys_clk_hz + (batch_valid_samples / 2u)) / batch_valid_samples);
            monitor_state.last_batch_valid_samples = batch_valid_samples;
            monitor_state.updates_published++;
            batch_sum_sys_clk_hz = 0u;
            batch_valid_samples = 0u;
            published_once = true;
        }
        critical_section_exit(&monitor_lock);

        pio_sysclk_stability_monitor_update_led(published_once,
                                                fault_active,
                                                now_us,
                                                &last_led_toggle_us,
                                                &led_enabled);
    }

    pio_sysclk_stability_monitor_set_led(false);

    critical_section_enter_blocking(&monitor_lock);
    monitor_state.running = false;
    monitor_state.launch_requested = false;
    critical_section_exit(&monitor_lock);
}

bool pio_sysclk_stability_monitor_start(const pio_sysclk_stability_monitor_config_t *config)
{
    if (config == NULL || config->pio == NULL || config->sm_clk_hz == 0u) {
        return false;
    }

    if (!monitor_state.initialized) {
        critical_section_init(&monitor_lock);
        monitor_state.initialized = true;
    }

    critical_section_enter_blocking(&monitor_lock);
    if (monitor_state.running || monitor_state.launch_requested) {
        critical_section_exit(&monitor_lock);
        return false;
    }

    monitor_config = *config;
    if (monitor_config.update_interval_valid_samples == 0u) {
        monitor_config.update_interval_valid_samples = PIO_SYSCLK_STABILITY_MONITOR_DEFAULT_BATCH;
    }

    monitor_state.launch_requested = true;
    monitor_state.running = false;
    monitor_state.stop_requested = false;
    monitor_state.pps_ready = false;
    monitor_state.configured_sys_clk_hz = 0u;
    monitor_state.effective_sys_clk_hz = 0u;
    monitor_state.update_interval_valid_samples = monitor_config.update_interval_valid_samples;
    monitor_state.last_batch_valid_samples = 0u;
    monitor_state.updates_published = 0u;
    monitor_state.total_valid_samples = 0u;
    monitor_state.total_timeouts = 0u;
    monitor_state.sum_sys_clk_hz = 0u;
    monitor_state.sum_period_ns = 0u;
    monitor_state.min_sys_clk_hz = 0u;
    monitor_state.max_sys_clk_hz = 0u;
    monitor_state.min_period_ns = 0u;
    monitor_state.max_period_ns = 0u;
    critical_section_exit(&monitor_lock);

    multicore_reset_core1();
    multicore_launch_core1(pio_sysclk_stability_monitor_core1_entry);
    return true;
}

bool pio_sysclk_stability_monitor_stop(void)
{
    if (!monitor_state.initialized) {
        return false;
    }

    critical_section_enter_blocking(&monitor_lock);
    bool was_active = monitor_state.running || monitor_state.launch_requested;
    monitor_state.stop_requested = true;
    critical_section_exit(&monitor_lock);

    if (!was_active) {
        return false;
    }

    while (pio_sysclk_stability_monitor_is_running()) {
        tight_loop_contents();
    }

    multicore_reset_core1();
    pio_sysclk_stability_monitor_set_led(false);
    return true;
}

bool pio_sysclk_stability_monitor_is_running(void)
{
    if (!monitor_state.initialized) {
        return false;
    }

    critical_section_enter_blocking(&monitor_lock);
    bool running = monitor_state.running || monitor_state.launch_requested;
    critical_section_exit(&monitor_lock);
    return running;
}

uint32_t pio_sysclk_stability_monitor_get_effective_sys_clk_hz(void)
{
    if (!monitor_state.initialized) {
        return 0u;
    }

    critical_section_enter_blocking(&monitor_lock);
    uint32_t effective_sys_clk_hz = monitor_state.effective_sys_clk_hz;
    critical_section_exit(&monitor_lock);
    return effective_sys_clk_hz;
}

bool pio_sysclk_stability_monitor_get_snapshot(pio_sysclk_stability_monitor_snapshot_t *snapshot_out)
{
    if (!monitor_state.initialized || snapshot_out == NULL) {
        return false;
    }

    critical_section_enter_blocking(&monitor_lock);
    snapshot_out->running = monitor_state.running;
    snapshot_out->pps_ready = monitor_state.pps_ready;
    snapshot_out->configured_sys_clk_hz = monitor_state.configured_sys_clk_hz;
    snapshot_out->effective_sys_clk_hz = monitor_state.effective_sys_clk_hz;
    snapshot_out->update_interval_valid_samples = monitor_state.update_interval_valid_samples;
    snapshot_out->last_batch_valid_samples = monitor_state.last_batch_valid_samples;
    snapshot_out->updates_published = monitor_state.updates_published;
    snapshot_out->total_valid_samples = monitor_state.total_valid_samples;
    snapshot_out->total_timeouts = monitor_state.total_timeouts;
    if (monitor_state.total_valid_samples == 0u) {
        snapshot_out->average_sys_clk_hz = 0u;
        snapshot_out->min_sys_clk_hz = 0u;
        snapshot_out->max_sys_clk_hz = 0u;
        snapshot_out->average_period_ns = 0u;
        snapshot_out->min_period_ns = 0u;
        snapshot_out->max_period_ns = 0u;
    } else {
        snapshot_out->average_sys_clk_hz =
            (uint32_t) ((monitor_state.sum_sys_clk_hz + (monitor_state.total_valid_samples / 2u)) /
                        monitor_state.total_valid_samples);
        snapshot_out->min_sys_clk_hz = monitor_state.min_sys_clk_hz;
        snapshot_out->max_sys_clk_hz = monitor_state.max_sys_clk_hz;
        snapshot_out->average_period_ns =
            (monitor_state.sum_period_ns + (monitor_state.total_valid_samples / 2u)) /
            monitor_state.total_valid_samples;
        snapshot_out->min_period_ns = monitor_state.min_period_ns;
        snapshot_out->max_period_ns = monitor_state.max_period_ns;
    }
    critical_section_exit(&monitor_lock);

    return true;
}