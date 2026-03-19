#include <stdio.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/stdio_usb.h"

#include "src/validation/pio_alarm_timer_validation.h"
#include "src/validation/pio_event_scheduler_validation.h"
#include "src/validation/pio_timer_input_capture_validation.h"
#include "src/validation/pio_timer_output_compare_validation.h"

typedef struct {
    uint pio_index;
    uint sm;
    uint start_pin;
    uint stop_pin;
    uint32_t sm_clk_hz;
    uint32_t timeout_ns;
    uint32_t sample_count;
} input_capture_menu_cfg_t;

static bool read_line(char *buffer, size_t buffer_size) {
    size_t index = 0;
    while (true) {
        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            putchar('\n');
            buffer[index] = '\0';
            return true;
        }

        if ((ch == 8 || ch == 127) && index > 0) {
            index--;
            printf("\b \b");
            continue;
        }

        if (ch >= 32 && ch <= 126 && index < (buffer_size - 1)) {
            buffer[index++] = (char) ch;
            putchar(ch);
        }
    }
}

static uint32_t prompt_u32(const char *label, uint32_t default_value) {
    char line[64];
    printf("%s [%lu]: ", label, (unsigned long) default_value);
    read_line(line, sizeof(line));
    if (line[0] == '\0') {
        return default_value;
    }

    char *end_ptr = NULL;
    unsigned long value = strtoul(line, &end_ptr, 10);
    if (end_ptr == line || *end_ptr != '\0') {
        printf("Invalid input, using default %lu\n", (unsigned long) default_value);
        return default_value;
    }

    return (uint32_t) value;
}

static PIO resolve_pio(uint pio_index) {
    return pio_index == 1 ? pio1 : pio0;
}

int main()
{
    stdio_init_all();

    while (!stdio_usb_connected()) {
        sleep_ms(10);
    }
    sleep_ms(50);

    input_capture_menu_cfg_t input_cfg = {
        .pio_index = 0,
        .sm = 0,
        .start_pin = 6,
        .stop_pin = 8,
        .sm_clk_hz = 100000000u,
        .timeout_ns = 100000000u,
        .sample_count = 10000u,
    };

    pio_timer_output_compare_validation_config_t output_cfg = {
        .pio_index = 0,
        .sm = 1,
        .trigger_pin = 6,
        .output_pin = 9,
        .continuous_mode = false,
        .sm_clk_hz = 100000000u,
        .compare_ns = 1000u,
        .pulse_ns = 1000u,
    };

    pio_event_scheduler_validation_config_t scheduler_cfg = {
        .pio_index = 0,
        .sm = 2,
        .trigger_pin = 6,
        .output_pin = 10,
        .sm_clk_hz = 100000000u,
        .compare_ns = 1000u,
        .pulse_ns = 1000u,
        .event_count = 16u,
    };

    pio_alarm_timer_validation_config_t alarm_timer_cfg = {
        .pio_index = 0,
        .sm = 3,
        .pps_pin = 6,
        .sm_clk_hz = 100000000u,
        .first_alarm_tick = 1000u,
        .alarm_step_ticks = 1000u,
        .burst_count = 8u,
    };

    while (true) {
        printf("\n=== Validation Menu ===\n");
        printf("1) Input capture validation\n");
        printf("2) Output compare validation\n");
        printf("3) Scheduler validation\n");
        printf("4) PIO alarm timer validation\n");
        printf("q) Quit menu loop\n");
        printf("Select: ");

        char line[16];
        read_line(line, sizeof(line));

        if (line[0] == '1') {
            input_cfg.pio_index = prompt_u32("PIO index (0 or 1)", input_cfg.pio_index);
            input_cfg.sm = prompt_u32("SM index", input_cfg.sm);
            input_cfg.start_pin = prompt_u32("Start pin", input_cfg.start_pin);
            input_cfg.stop_pin = prompt_u32("Stop pin", input_cfg.stop_pin);
            input_cfg.sm_clk_hz = prompt_u32("SM clock (Hz)", input_cfg.sm_clk_hz);
            input_cfg.timeout_ns = prompt_u32("Timeout (ns)", input_cfg.timeout_ns);
            input_cfg.sample_count = prompt_u32("Samples per report", input_cfg.sample_count);

            pio_timer_input_capture_validation_config_t run_cfg = {
                .start_pin = input_cfg.start_pin,
                .stop_pin = input_cfg.stop_pin,
                .sm_clk_hz = input_cfg.sm_clk_hz,
                .timeout_ns = input_cfg.timeout_ns,
                .sample_count = input_cfg.sample_count,
                .pio = resolve_pio(input_cfg.pio_index),
                .sm = input_cfg.sm,
            };
            pio_timer_input_capture_validation_run(&run_cfg);
        } else if (line[0] == '2') {
            output_cfg.pio_index = prompt_u32("PIO index (0 or 1)", output_cfg.pio_index);
            output_cfg.sm = prompt_u32("SM index", output_cfg.sm);
            output_cfg.trigger_pin = prompt_u32("Trigger pin", output_cfg.trigger_pin);
            output_cfg.output_pin = prompt_u32("Output pin", output_cfg.output_pin);
            output_cfg.continuous_mode =
                prompt_u32("Continuous mode (0=one-shot, 1=continuous)",
                           output_cfg.continuous_mode ? 1u : 0u) != 0u;
            output_cfg.sm_clk_hz = prompt_u32("SM clock (Hz)", output_cfg.sm_clk_hz);
            output_cfg.compare_ns = prompt_u32("Compare delay (ns)", output_cfg.compare_ns);
            output_cfg.pulse_ns = prompt_u32("Pulse width (ns)", output_cfg.pulse_ns);

            pio_timer_output_compare_validation_run(&output_cfg);
        } else if (line[0] == '3') {
            scheduler_cfg.pio_index = prompt_u32("PIO index (0 or 1)", scheduler_cfg.pio_index);
            scheduler_cfg.sm = prompt_u32("SM index", scheduler_cfg.sm);
            scheduler_cfg.trigger_pin = prompt_u32("Trigger pin", scheduler_cfg.trigger_pin);
            scheduler_cfg.output_pin = prompt_u32("Output pin", scheduler_cfg.output_pin);
            scheduler_cfg.sm_clk_hz = prompt_u32("SM clock (Hz)", scheduler_cfg.sm_clk_hz);
            scheduler_cfg.compare_ns = prompt_u32("Compare delay (ns)", scheduler_cfg.compare_ns);
            scheduler_cfg.pulse_ns = prompt_u32("Pulse width (ns)", scheduler_cfg.pulse_ns);
            scheduler_cfg.event_count = prompt_u32("Event count", scheduler_cfg.event_count);

            pio_event_scheduler_validation_run(&scheduler_cfg);
        } else if (line[0] == '4') {
            alarm_timer_cfg.pio_index = prompt_u32("PIO index (0 or 1)", alarm_timer_cfg.pio_index);
            alarm_timer_cfg.sm = prompt_u32("SM index", alarm_timer_cfg.sm);
            alarm_timer_cfg.pps_pin = prompt_u32("PPS pin", alarm_timer_cfg.pps_pin);
            alarm_timer_cfg.sm_clk_hz = prompt_u32("SM clock (Hz)", alarm_timer_cfg.sm_clk_hz);
            alarm_timer_cfg.first_alarm_tick = prompt_u32("First alarm tick", alarm_timer_cfg.first_alarm_tick);
            alarm_timer_cfg.alarm_step_ticks = prompt_u32("Alarm step ticks", alarm_timer_cfg.alarm_step_ticks);
            alarm_timer_cfg.burst_count = prompt_u32("Burst count", alarm_timer_cfg.burst_count);

            pio_alarm_timer_validation_run(&alarm_timer_cfg);
        } else if (line[0] == 'q' || line[0] == 'Q') {
            printf("Exiting validation menu loop\n");
            while (true) {
                tight_loop_contents();
            }
        } else {
            printf("Unknown selection\n");
        }
    }
}
