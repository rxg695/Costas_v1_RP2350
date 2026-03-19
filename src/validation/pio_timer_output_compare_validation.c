#include <stdio.h>

#include "driver/pio_timer_output_compare/pio_timer_output_compare.h"
#include "pio_timer_output_compare.pio.h"
#include "src/validation/pio_timer_output_compare_validation.h"

static PIO resolve_pio(uint pio_index) {
    return pio_index == 1 ? pio1 : pio0;
}

void pio_timer_output_compare_validation_run(const pio_timer_output_compare_validation_config_t *config)
{
    static bool program_loaded[2] = {false, false};
    static uint program_offset[2] = {0, 0};

    PIO pio = resolve_pio(config->pio_index);
    uint slot = config->pio_index == 1 ? 1u : 0u;

    if (!program_loaded[slot]) {
        program_offset[slot] = pio_add_program(pio, &pio_timer_output_compare_program);
        program_loaded[slot] = true;
    }

    pio_timer_output_compare_init(pio,
                                  config->sm,
                                  program_offset[slot],
                                  config->trigger_pin,
                                  config->output_pin,
                                  (float) config->sm_clk_hz);

    uint32_t compare_ticks = pio_timer_output_compare_ns_to_ticks(config->sm_clk_hz, config->compare_ns);
    uint32_t pulse_ticks = pio_timer_output_compare_ns_to_ticks(config->sm_clk_hz, config->pulse_ns);
    if (pulse_ticks == 0) {
        pulse_ticks = 1;
    }

    printf("\nOutput compare validation active\n");
    printf("PIO%u SM%u trigger=GP%u output=GP%u sm_clk=%lu Hz\n",
           config->pio_index,
           config->sm,
           config->trigger_pin,
           config->output_pin,
           (unsigned long) config->sm_clk_hz);
    printf("compare=%lu ns (%lu ticks), pulse=%lu ns (%lu ticks)\n",
           (unsigned long) config->compare_ns,
           (unsigned long) compare_ticks,
           (unsigned long) config->pulse_ns,
           (unsigned long) pulse_ticks);
    printf("Press 'a' to arm one event, 'q' to return to menu\n");

    uint32_t armed_count = 0;
    while (true) {
        int ch = getchar_timeout_us(10000);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        if (ch == 'a' || ch == 'A') {
            pio_timer_output_compare_arm(pio, config->sm, compare_ticks, pulse_ticks);
            armed_count++;
            printf("armed=%lu\n", (unsigned long) armed_count);
        } else if (ch == 'q' || ch == 'Q') {
            printf("Leaving output compare validation\n");
            break;
        }
    }
}
