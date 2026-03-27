#include <math.h>
#include <stdio.h>

#include "driver/pio_sysclk_stability/pio_sysclk_stability.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"
#include "src/validation/validation_config.h"
#include "src/validation/pio_sysclk_stability_validation.h"

typedef struct {
    bool has_sample;
    uint64_t valid_samples;
    uint64_t timeout_count;
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t min_sysclk_hz;
    uint64_t max_sysclk_hz;
    double mean_ns;
    double m2_ns;
} pio_sysclk_stability_stats_t;

static void stats_reset(pio_sysclk_stability_stats_t *stats)
{
    stats->has_sample = false;
    stats->valid_samples = 0u;
    stats->timeout_count = 0u;
    stats->min_ns = 0u;
    stats->max_ns = 0u;
    stats->min_sysclk_hz = 0u;
    stats->max_sysclk_hz = 0u;
    stats->mean_ns = 0.0;
    stats->m2_ns = 0.0;
}

static void stats_add_timeout(pio_sysclk_stability_stats_t *stats)
{
    stats->timeout_count++;
}

static void stats_add_sample(pio_sysclk_stability_stats_t *stats,
                             uint64_t elapsed_ns,
                             uint32_t estimated_sysclk_hz)
{
    if (!stats->has_sample) {
        stats->min_ns = elapsed_ns;
        stats->max_ns = elapsed_ns;
        stats->min_sysclk_hz = estimated_sysclk_hz;
        stats->max_sysclk_hz = estimated_sysclk_hz;
        stats->has_sample = true;
    } else {
        if (elapsed_ns < stats->min_ns) {
            stats->min_ns = elapsed_ns;
        }
        if (elapsed_ns > stats->max_ns) {
            stats->max_ns = elapsed_ns;
        }
        if (estimated_sysclk_hz < stats->min_sysclk_hz) {
            stats->min_sysclk_hz = estimated_sysclk_hz;
        }
        if (estimated_sysclk_hz > stats->max_sysclk_hz) {
            stats->max_sysclk_hz = estimated_sysclk_hz;
        }
    }

    stats->valid_samples++;

    double delta = (double) elapsed_ns - stats->mean_ns;
    stats->mean_ns += delta / (double) stats->valid_samples;
    double delta2 = (double) elapsed_ns - stats->mean_ns;
    stats->m2_ns += delta * delta2;
}

static double stats_stddev_ns(const pio_sysclk_stability_stats_t *stats)
{
    if (stats->valid_samples == 0u) {
        return 0.0;
    }
    return sqrt(stats->m2_ns / (double) stats->valid_samples);
}

static double period_ns_to_ppm(double period_ns)
{
    return ((period_ns - (double) VALIDATION_NSEC_PER_SEC) * (double) VALIDATION_USEC_PER_SEC) /
           (double) VALIDATION_NSEC_PER_SEC;
}

static double stats_mean_ppm(const pio_sysclk_stability_stats_t *stats)
{
    return period_ns_to_ppm(stats->mean_ns);
}

static double stats_min_ppm(const pio_sysclk_stability_stats_t *stats)
{
    return period_ns_to_ppm((double) stats->min_ns);
}

static double stats_max_ppm(const pio_sysclk_stability_stats_t *stats)
{
    return period_ns_to_ppm((double) stats->max_ns);
}

static uint32_t stats_mean_sysclk_hz(const pio_sysclk_stability_t *capture,
                                     const pio_sysclk_stability_stats_t *stats)
{
    if (!stats->has_sample) {
        return 0u;
    }
    return (uint32_t) (((uint64_t) capture->configured_sys_clk_hz * (uint64_t) stats->mean_ns) /
                       VALIDATION_NSEC_PER_SEC);
}

static void print_stats_summary(const char *label,
                                const pio_sysclk_stability_t *capture,
                                const pio_sysclk_stability_stats_t *stats)
{
    printf("%s:\n", label);
    printf("  Valid samples: %llu\n", (unsigned long long) stats->valid_samples);
    printf("  Timeouts: %llu\n", (unsigned long long) stats->timeout_count);
    if (!stats->has_sample) {
        return;
    }

    printf("  Average period: %.3f ns\n", stats->mean_ns);
    printf("  Stddev: %.3f ns\n", stats_stddev_ns(stats));
    printf("  Period min/max: %llu ns / %llu ns\n",
           (unsigned long long) stats->min_ns,
           (unsigned long long) stats->max_ns);
    printf("  Period jitter: %llu ns\n",
           (unsigned long long) (stats->max_ns - stats->min_ns));
    printf("  PPM avg/min/max: %.3f / %.3f / %.3f\n",
           stats_mean_ppm(stats),
           stats_min_ppm(stats),
           stats_max_ppm(stats));
    printf("  Estimated clk_sys avg/min/max: %lu Hz / %llu Hz / %llu Hz\n",
           (unsigned long) stats_mean_sysclk_hz(capture, stats),
           (unsigned long long) stats->min_sysclk_hz,
           (unsigned long long) stats->max_sysclk_hz);
}

static uint32_t next_run_sample_count(uint32_t current_samples,
                                      uint32_t max_samples)
{
    if (current_samples >= max_samples) {
        return max_samples;
    }

    uint64_t next_samples = (uint64_t) current_samples * VALIDATION_SYSCLK_STABILITY_RUN_SCALE_FACTOR;
    if (next_samples > max_samples) {
        next_samples = max_samples;
    }
    return (uint32_t) next_samples;
}

static uint32_t run_repetition_count(uint32_t run_samples,
                                     uint32_t max_samples)
{
    uint64_t denominator = VALIDATION_SYSCLK_STABILITY_REPETITION_DIVISOR * run_samples;
    uint64_t repetitions = (max_samples + denominator - 1ull) / denominator;
    if (repetitions == 0ull) {
        repetitions = 1ull;
    }
    return (uint32_t) repetitions;
}

static void print_schedule(uint32_t min_samples,
                           uint32_t max_samples)
{
    uint32_t run_samples = min_samples;

    printf("Planned run schedule:\n");
    while (true) {
        printf("  %u samples x %u run(s)\n",
               run_samples,
               run_repetition_count(run_samples, max_samples));
        if (run_samples >= max_samples) {
            break;
        }
        run_samples = next_run_sample_count(run_samples, max_samples);
    }
}

static void print_periodic_update(const pio_sysclk_stability_t *capture,
                                  const pio_sysclk_stability_stats_t *overall_stats,
                                  uint32_t stage_run_samples,
                                  uint32_t stage_run_index,
                                  uint32_t stage_run_count,
                                  uint64_t elapsed_s)
{
    printf("Progress update (%llu s):\n", (unsigned long long) elapsed_s);
    printf("  Current run: %u/%u at %u samples\n",
           stage_run_index,
           stage_run_count,
           stage_run_samples);
    print_stats_summary("  So far", capture, overall_stats);
}

void pio_sysclk_stability_validation_run(const pio_sysclk_stability_validation_config_t *config)
{
    pio_sysclk_stability_t capture;
    pio_sysclk_stability_init(&capture,
                              config->pio,
                              config->sm,
                              config->pps_pin,
                              config->sm_clk_hz,
                              config->timeout_ns);

    printf("Sysclk stability validation active: GP%u rising-to-next-rising\n",
           config->pps_pin);
    printf("State-machine clock: %u Hz, timeout: %lu ns, configured clk_sys: %lu Hz\n",
           config->sm_clk_hz,
           (unsigned long) config->timeout_ns,
           (unsigned long) capture.configured_sys_clk_hz);
    uint32_t min_samples = config->min_samples_per_run == 0u
                               ? VALIDATION_SYSCLK_STABILITY_MIN_SAMPLES_FALLBACK
                               : config->min_samples_per_run;
    uint32_t max_samples = config->max_samples_per_run < min_samples ? min_samples : config->max_samples_per_run;
    uint32_t update_period_s = config->update_period_s == 0u
                                   ? VALIDATION_SYSCLK_STABILITY_DEFAULT_UPDATE_PERIOD_S
                                   : config->update_period_s;
    printf("Run sizes: %lu to %lu samples, periodic updates every %lu s.\n",
           (unsigned long) min_samples,
           (unsigned long) max_samples,
           (unsigned long) update_period_s);
    printf("Press s for a progress update or q to stop.\n");
    print_schedule(min_samples, max_samples);

    pio_sysclk_stability_stats_t overall_stats;
    stats_reset(&overall_stats);

    uint64_t start_time_us = time_us_64();
    uint64_t next_update_us = start_time_us + ((uint64_t) update_period_s * VALIDATION_USEC_PER_SEC);
    bool aborted = false;

    for (uint32_t run_samples = min_samples; ; run_samples = next_run_sample_count(run_samples, max_samples)) {
        uint32_t stage_run_count = run_repetition_count(run_samples, max_samples);
        for (uint32_t run_index = 0u; run_index < stage_run_count; ++run_index) {
            pio_sysclk_stability_stats_t run_stats;
            stats_reset(&run_stats);

            while (run_stats.valid_samples < run_samples) {
                uint32_t elapsed_ticks = 0;
                bool timed_out = false;

                if (pio_sysclk_stability_poll(&capture, &elapsed_ticks, &timed_out)) {
                    if (timed_out) {
                        stats_add_timeout(&run_stats);
                        stats_add_timeout(&overall_stats);
                    } else {
                        uint64_t elapsed_ns = pio_sysclk_stability_ticks_to_ns(&capture, elapsed_ticks);
                        uint32_t estimated_sysclk_hz = pio_sysclk_stability_estimate_sysclk_hz(&capture,
                                                                                                elapsed_ticks);
                        stats_add_sample(&run_stats, elapsed_ns, estimated_sysclk_hz);
                        stats_add_sample(&overall_stats, elapsed_ns, estimated_sysclk_hz);
                    }
                }

                uint64_t now_us = time_us_64();
                if (now_us >= next_update_us) {
                    print_periodic_update(&capture,
                                          &overall_stats,
                                          run_samples,
                                          run_index + 1u,
                                          stage_run_count,
                                          (now_us - start_time_us) / VALIDATION_USEC_PER_SEC);
                    next_update_us = now_us + ((uint64_t) update_period_s * VALIDATION_USEC_PER_SEC);
                }

                int command = getchar_timeout_us(0);
                if (command == 's' || command == 'S') {
                    print_periodic_update(&capture,
                                          &overall_stats,
                                          run_samples,
                                          run_index + 1u,
                                          stage_run_count,
                                          (now_us - start_time_us) / VALIDATION_USEC_PER_SEC);
                }
                if (command == 'q' || command == 'Q') {
                    aborted = true;
                    break;
                }

                tight_loop_contents();
            }

            print_stats_summary("Run summary", &capture, &run_stats);
            if (aborted) {
                break;
            }
        }

        if (aborted || run_samples >= max_samples) {
            break;
        }
    }

    if (aborted) {
        printf("Sysclk stability validation aborted by user.\n");
    }
    print_stats_summary("Final overall summary", &capture, &overall_stats);
    printf("Leaving sysclk stability validation\n");
}