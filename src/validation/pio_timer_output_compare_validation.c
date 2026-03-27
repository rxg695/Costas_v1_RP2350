#include <stdio.h>

#include "driver/pio_timer_output_compare/pio_timer_output_compare.h"
#include "pio_timer_output_compare.pio.h"
#include "src/validation/validation_config.h"
#include "src/validation/pio_timer_output_compare_validation.h"

static PIO resolve_pio(uint pio_index) {
    switch (pio_index) {
    case 0:
        return pio0;
    case 1:
        return pio1;
    case 2:
        return pio2;
    default:
        return pio0;
    }
}

static bool queue_pair_if_room(PIO pio,
                               uint sm,
                               uint32_t compare_ticks,
                               uint32_t pulse_ticks)
{
    const uint tx_fifo_capacity_words = VALIDATION_OUTPUT_COMPARE_TX_FIFO_CAPACITY_WORDS;
    uint tx_level = pio_sm_get_tx_fifo_level(pio, sm);
    if (tx_level > (tx_fifo_capacity_words - 2u)) {
        return false;
    }

    pio_sm_put(pio, sm, compare_ticks);
    pio_sm_put(pio, sm, pulse_ticks);
    return true;
}

    static void print_output_compare_status(bool continuous_mode,
                             uint32_t compare_ns,
                             uint32_t compare_ticks,
                             uint32_t pulse_ns,
                             uint32_t pulse_ticks,
                             uint32_t armed_count)
    {
        printf("Status:\n");
        printf("  Mode: %s\n", continuous_mode ? "continuous stream" : "one-shot");
        printf("  Delay: %lu ns (%lu ticks)\n",
            (unsigned long) compare_ns,
            (unsigned long) compare_ticks);
        printf("  Pulse width: %lu ns (%lu ticks)\n",
            (unsigned long) pulse_ns,
            (unsigned long) pulse_ticks);
        printf("  Events queued or armed this run: %lu\n",
            (unsigned long) armed_count);
    }

void pio_timer_output_compare_validation_run(const pio_timer_output_compare_validation_config_t *config)
{
    static bool program_loaded[VALIDATION_OUTPUT_COMPARE_VALIDATION_PIO_COUNT] = {false};
    static uint program_offset[VALIDATION_OUTPUT_COMPARE_VALIDATION_PIO_COUNT] = {0u};

    PIO pio = resolve_pio(config->pio_index);
    uint slot = config->pio_index;
    if (slot >= VALIDATION_OUTPUT_COMPARE_VALIDATION_PIO_COUNT) {
        slot = 0u;
    }

    if (!program_loaded[slot]) {
        program_offset[slot] = pio_add_program(pio, &pio_timer_output_compare_program);
        program_loaded[slot] = true;
    }

    pio_timer_output_compare_mode_t mode =
        config->continuous_mode ? PIO_TIMER_OUTPUT_COMPARE_MODE_CONTINUOUS
                                : PIO_TIMER_OUTPUT_COMPARE_MODE_ONE_SHOT;

    pio_timer_output_compare_init(pio,
                                  config->sm,
                                  program_offset[slot],
                                  config->trigger_pin,
                                  config->output_pin,
                                  (float) config->sm_clk_hz,
                                  mode);

    uint32_t timing_sm_clk_hz = config->timing_sm_clk_hz == 0u ? config->sm_clk_hz : config->timing_sm_clk_hz;
    uint32_t compare_ticks = pio_timer_output_compare_ns_to_ticks(timing_sm_clk_hz, config->compare_ns);
    uint32_t pulse_ticks = pio_timer_output_compare_ns_to_ticks(timing_sm_clk_hz, config->pulse_ns);
    if (pulse_ticks == 0) {
        pulse_ticks = 1;
    }

    printf("\nOutput compare validation active\n");
        printf("PIO%u SM%u trigger=GP%u output=GP%u state-machine clock=%lu Hz, timing clock=%lu Hz\n",
           config->pio_index,
           config->sm,
           config->trigger_pin,
           config->output_pin,
            (unsigned long) config->sm_clk_hz,
            (unsigned long) timing_sm_clk_hz);
    printf("mode=%s\n", config->continuous_mode ? "continuous stream" : "one-shot");
    printf("delay=%lu ns (%lu ticks), pulse=%lu ns (%lu ticks)\n",
           (unsigned long) config->compare_ns,
           (unsigned long) compare_ticks,
           (unsigned long) config->pulse_ns,
           (unsigned long) pulse_ticks);
    if (config->continuous_mode) {
        printf("Commands: a=queue one event, 4=queue four events, s=queue stop marker, i=show status, q=return\n");
    } else {
        printf("Commands: a=arm one event, i=show status, q=return\n");
    }

    uint32_t armed_count = 0;
    while (true) {
        int ch = getchar_timeout_us(10000);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        if (ch == 'a' || ch == 'A') {
            if (config->continuous_mode) {
                if (!queue_pair_if_room(pio, config->sm, compare_ticks, pulse_ticks)) {
                    printf("TX FIFO is full. Trigger the pending events, then try again.\n");
                    continue;
                }
            } else {
                pio_timer_output_compare_arm(pio, config->sm, compare_ticks, pulse_ticks);
            }
            armed_count++;
            printf("Queued or armed event count: %lu\n", (unsigned long) armed_count);
        } else if ((ch == '4') && config->continuous_mode) {
            uint32_t queued_now = 0;
            for (uint32_t index = 0; index < VALIDATION_OUTPUT_COMPARE_BURST_QUEUE_COUNT; ++index) {
                if (!queue_pair_if_room(pio, config->sm, compare_ticks, pulse_ticks)) {
                    break;
                }
                queued_now++;
                armed_count++;
            }
                 printf("Queued burst:\n");
                 printf("  Added now: %lu\n",
                   (unsigned long) queued_now,
                     (unsigned long) queued_now);
                 printf("  Total queued: %lu\n",
                     (unsigned long) armed_count);
            if (queued_now < VALIDATION_OUTPUT_COMPARE_BURST_QUEUE_COUNT) {
                printf("Only part of the burst fit in the TX FIFO. Trigger or drain events, then queue the rest.\n");
            }
        } else if ((ch == 's' || ch == 'S') && config->continuous_mode) {
            if (!queue_pair_if_room(pio,
                                    config->sm,
                                    PIO_TIMER_OUTPUT_COMPARE_STOP_COMPARE_TICKS,
                                    0u)) {
                printf("TX FIFO is full, so the stop marker could not be queued yet.\n");
                continue;
            }
            printf("Stop marker queued.\n");
        } else if (ch == 'i' || ch == 'I') {
            print_output_compare_status(config->continuous_mode,
                                        config->compare_ns,
                                        compare_ticks,
                                        config->pulse_ns,
                                        pulse_ticks,
                                        armed_count);
        } else if (ch == 'q' || ch == 'Q') {
            if (config->continuous_mode) {
                if (!queue_pair_if_room(pio,
                                        config->sm,
                                        PIO_TIMER_OUTPUT_COMPARE_STOP_COMPARE_TICKS,
                                        0u)) {
                    printf("TX FIFO is full, so the stop marker was not queued on exit.\n");
                }
            }
            printf("Leaving output compare validation\n");
            break;
        }
    }
}
