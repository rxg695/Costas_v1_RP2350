#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include "driver/pio_timer_input_capture/pio_timer_input_capture.h"
#include "src/validation/pio_timer_input_capture_validation.h"

void pio_timer_input_capture_validation_run(const pio_timer_input_capture_validation_config_t *config)
{
    pio_timer_input_capture_t capture;
    pio_timer_input_capture_init(&capture,
                     config->pio,
                     config->sm,
                     config->start_pin,
                     config->stop_pin,
                     config->sm_clk_hz,
                     config->timeout_ns);

    uint32_t loop_ns = (uint32_t) (2000000000ull / config->sm_clk_hz);
    printf("PIO timer input capture active: GP%u rising -> GP%u rising\n",
        config->start_pin,
        config->stop_pin);
    printf("SM clock: %u Hz, loop resolution: %u ns, timeout: %lu ns\n",
        config->sm_clk_hz,
           loop_ns,
        (unsigned long) config->timeout_ns);
    printf("Block size: %lu samples, press 'q' to stop this validation run\n",
        (unsigned long) config->sample_count);

    // Block-local counters/statistics for each configured window.
    uint32_t sample_index = 0;
    uint32_t block_timeout_count = 0;

    // Run-wide totals maintained across all blocks.
    uint64_t total_timeout_count = 0;
    uint64_t total_valid_samples = 0;

    // Block-local extrema in raw ticks and derived ns.
    uint32_t min_ticks = 0;
    uint32_t max_ticks = 0;
    uint64_t min_ns = 0;
    uint64_t max_ns = 0;

    // Welford online moments for numerically stable mean/stddev.
    double mean_ns = 0.0;
    double m2_ns = 0.0;

    // Run-wide extrema (persist across all blocks).
    uint32_t run_min_ticks = 0;
    uint32_t run_max_ticks = 0;
    uint64_t run_min_ns = 0;
    uint64_t run_max_ns = 0;
    bool run_has_sample = false;

    while (true) {
        uint32_t elapsed_ticks = 0;
        bool timed_out = false;

        if (pio_timer_input_capture_poll(&capture, &elapsed_ticks, &timed_out)) {
            if (timed_out) {
                block_timeout_count++;
                total_timeout_count++;
            } else {
                uint64_t elapsed_ns = pio_timer_input_capture_ticks_to_ns(&capture, elapsed_ticks);

                // Update block extrema.
                if (sample_index == 0) {
                    min_ticks = elapsed_ticks;
                    max_ticks = elapsed_ticks;
                    min_ns = elapsed_ns;
                    max_ns = elapsed_ns;
                } else {
                    if (elapsed_ticks < min_ticks) {
                        min_ticks = elapsed_ticks;
                    }
                    if (elapsed_ticks > max_ticks) {
                        max_ticks = elapsed_ticks;
                    }
                    if (elapsed_ns < min_ns) {
                        min_ns = elapsed_ns;
                    }
                    if (elapsed_ns > max_ns) {
                        max_ns = elapsed_ns;
                    }
                }

                sample_index++;
                total_valid_samples++;

                // Update run-wide extrema.
                if (!run_has_sample) {
                    run_min_ticks = elapsed_ticks;
                    run_max_ticks = elapsed_ticks;
                    run_min_ns = elapsed_ns;
                    run_max_ns = elapsed_ns;
                    run_has_sample = true;
                } else {
                    if (elapsed_ticks < run_min_ticks) {
                        run_min_ticks = elapsed_ticks;
                    }
                    if (elapsed_ticks > run_max_ticks) {
                        run_max_ticks = elapsed_ticks;
                    }
                    if (elapsed_ns < run_min_ns) {
                        run_min_ns = elapsed_ns;
                    }
                    if (elapsed_ns > run_max_ns) {
                        run_max_ns = elapsed_ns;
                    }
                }

                // Welford online update.
                double delta = (double) elapsed_ns - mean_ns;
                mean_ns += delta / (double) sample_index;
                double delta2 = (double) elapsed_ns - mean_ns;
                m2_ns += delta * delta2;

                // Emit summary each complete block of valid samples.
                if (sample_index == config->sample_count) {
                    double variance_ns2 = m2_ns / (double) config->sample_count;
                    double stddev_ns = sqrt(variance_ns2);
                    uint32_t jitter_ticks = max_ticks - min_ticks;
                    uint64_t jitter_ns = max_ns - min_ns;
                    uint32_t run_jitter_ticks = run_max_ticks - run_min_ticks;
                    uint64_t run_jitter_ns = run_max_ns - run_min_ns;

                    printf("block samples=%u avg=%.3f ns stddev=%.3f ns min=%u ticks/%llu ns max=%u ticks/%llu ns jitter=%u ticks/%llu ns timeouts=%u\n",
                           config->sample_count,
                           mean_ns,
                           stddev_ns,
                           min_ticks,
                           (unsigned long long) min_ns,
                           max_ticks,
                           (unsigned long long) max_ns,
                           jitter_ticks,
                           (unsigned long long) jitter_ns,
                           block_timeout_count);
                    printf("run   samples=%llu min=%u ticks/%llu ns max=%u ticks/%llu ns jitter=%u ticks/%llu ns total_timeouts=%llu\n",
                           (unsigned long long) total_valid_samples,
                           run_min_ticks,
                           (unsigned long long) run_min_ns,
                           run_max_ticks,
                           (unsigned long long) run_max_ns,
                           run_jitter_ticks,
                           (unsigned long long) run_jitter_ns,
                           (unsigned long long) total_timeout_count);

                    // Reset block-local accumulators for next window.
                    sample_index = 0;
                    block_timeout_count = 0;
                    min_ticks = 0;
                    max_ticks = 0;
                    min_ns = 0;
                    max_ns = 0;
                    mean_ns = 0.0;
                    m2_ns = 0.0;
                }
            }
        }

        int command = getchar_timeout_us(0);
        if (command == 'q' || command == 'Q') {
            printf("Stopping input capture validation and returning to menu\n");
            break;
        }

        tight_loop_contents();
    }
}
