#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include "driver/pio_timer_input_capture/pio_timer_input_capture.h"
#include "src/validation/validation_config.h"
#include "src/validation/pio_timer_input_capture_validation.h"

static uint64_t ticks_to_ns(uint32_t timing_sm_clk_hz,
                            uint32_t ticks)
{
    return ((uint64_t) ticks * VALIDATION_INPUT_CAPTURE_TICK_NUMERATOR_NS) / timing_sm_clk_hz;
}

static void print_input_capture_status(uint64_t total_valid_samples,
                        uint64_t total_timeout_count,
                        bool run_has_sample,
                        uint32_t run_min_ticks,
                        uint32_t run_max_ticks,
                        uint64_t run_min_ns,
                        uint64_t run_max_ns)
{
    printf("Run summary:\n");
    printf("  Valid samples: %llu\n", (unsigned long long) total_valid_samples);
    printf("  Timeouts: %llu\n", (unsigned long long) total_timeout_count);
    if (run_has_sample) {
     printf("  Minimum: %u ticks / %llu ns\n",
         run_min_ticks,
         (unsigned long long) run_min_ns);
     printf("  Maximum: %u ticks / %llu ns\n",
         run_max_ticks,
         (unsigned long long) run_max_ns);
     printf("  Jitter: %u ticks / %llu ns\n",
         run_max_ticks - run_min_ticks,
         (unsigned long long) (run_max_ns - run_min_ns));
    }
}

void pio_timer_input_capture_validation_run(const pio_timer_input_capture_validation_config_t *config)
{
    pio_timer_input_capture_t capture;
    uint32_t timing_sm_clk_hz = config->timing_sm_clk_hz == 0u ? config->sm_clk_hz : config->timing_sm_clk_hz;
    pio_timer_input_capture_init(&capture,
                     config->pio,
                     config->sm,
                     config->start_pin,
                     config->stop_pin,
                     config->sm_clk_hz,
                     config->timeout_ns);

    uint32_t loop_ns = (uint32_t) (VALIDATION_INPUT_CAPTURE_TICK_NUMERATOR_NS / timing_sm_clk_hz);
    printf("Input capture validation active: GP%u rising -> GP%u rising\n",
        config->start_pin,
        config->stop_pin);
    printf("State-machine clock: %u Hz, timing clock: %u Hz, loop resolution: %u ns, timeout: %lu ns\n",
        config->sm_clk_hz,
        timing_sm_clk_hz,
           loop_ns,
        (unsigned long) config->timeout_ns);
    printf("Block size: %lu valid samples. Press s for a run summary or q to stop.\n",
        (unsigned long) config->sample_count);

    uint32_t sample_index = 0;
    uint32_t block_timeout_count = 0;

    uint64_t total_timeout_count = 0;
    uint64_t total_valid_samples = 0;

    uint32_t min_ticks = 0;
    uint32_t max_ticks = 0;
    uint64_t min_ns = 0;
    uint64_t max_ns = 0;

    // Welford accumulation keeps the running variance stable over long runs.
    double mean_ns = 0.0;
    double m2_ns = 0.0;

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
                uint64_t elapsed_ns = ticks_to_ns(timing_sm_clk_hz, elapsed_ticks);

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

                double delta = (double) elapsed_ns - mean_ns;
                mean_ns += delta / (double) sample_index;
                double delta2 = (double) elapsed_ns - mean_ns;
                m2_ns += delta * delta2;

                if (sample_index == config->sample_count) {
                    double variance_ns2 = m2_ns / (double) config->sample_count;
                    double stddev_ns = sqrt(variance_ns2);
                    uint32_t jitter_ticks = max_ticks - min_ticks;
                    uint64_t jitter_ns = max_ns - min_ns;
                    uint32_t run_jitter_ticks = run_max_ticks - run_min_ticks;
                    uint64_t run_jitter_ns = run_max_ns - run_min_ns;

                      printf("Block summary:\n");
                      printf("  Samples: %u\n", config->sample_count);
                      printf("  Average: %.3f ns\n", mean_ns);
                      printf("  Stddev: %.3f ns\n", stddev_ns);
                      printf("  Minimum: %u ticks / %llu ns\n",
                          min_ticks,
                          (unsigned long long) min_ns);
                      printf("  Maximum: %u ticks / %llu ns\n",
                          max_ticks,
                          (unsigned long long) max_ns);
                      printf("  Jitter: %u ticks / %llu ns\n",
                          jitter_ticks,
                          (unsigned long long) jitter_ns);
                      printf("  Block timeouts: %u\n", block_timeout_count);
                      printf("Run summary:\n");
                      printf("  Valid samples: %llu\n",
                          (unsigned long long) total_valid_samples);
                      printf("  Minimum: %u ticks / %llu ns\n",
                          run_min_ticks,
                          (unsigned long long) run_min_ns);
                      printf("  Maximum: %u ticks / %llu ns\n",
                          run_max_ticks,
                          (unsigned long long) run_max_ns);
                      printf("  Jitter: %u ticks / %llu ns\n",
                          run_jitter_ticks,
                          (unsigned long long) run_jitter_ns);
                      printf("  Total timeouts: %llu\n",
                          (unsigned long long) total_timeout_count);

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
        if (command == 's' || command == 'S') {
            print_input_capture_status(total_valid_samples,
                                       total_timeout_count,
                                       run_has_sample,
                                       run_min_ticks,
                                       run_max_ticks,
                                       run_min_ns,
                                       run_max_ns);
        }
        if (command == 'q' || command == 'Q') {
            printf("Leaving input capture validation\n");
            break;
        }

        tight_loop_contents();
    }
}
