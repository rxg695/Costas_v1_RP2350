#include <stdio.h>
#include <stdlib.h>

#include "driver/pio_sysclk_stability/pio_sysclk_stability_monitor.h"
#include "hardware/clocks.h"
#include "pico/stdio_usb.h"
#include "pico/stdlib.h"

#include "src/validation/ad9850_validation.h"
#include "src/validation/pio_alarm_timer_validation.h"
#include "src/validation/pio_timer_input_capture_validation.h"
#include "src/validation/pio_timer_output_compare_validation.h"
#include "src/validation/scheduler_validation.h"
#include "src/validation/validation_config.h"
#include "src/validation/validation_status_leds.h"

typedef struct {
    uint pio_index;
    uint sm;
    uint start_pin;
    uint stop_pin;
    uint32_t sm_clk_hz;
    uint32_t timeout_ns;
    uint32_t sample_count;
} input_capture_menu_cfg_t;

typedef struct {
    uint pio_index;
    uint sm;
    uint pps_pin;
    uint32_t sm_clk_hz;
    uint32_t timeout_ns;
    uint32_t update_interval_valid_samples;
} sysclk_monitor_menu_cfg_t;

typedef enum {
    VALIDATION_CLOCK_SOURCE_SYSTEM = 0,
    VALIDATION_CLOCK_SOURCE_EFFECTIVE = 1,
} validation_clock_source_t;

typedef struct {
    uint32_t configured_sys_clk_hz;
    uint32_t timing_sys_clk_hz;
    bool effective_selected;
    bool effective_available;
} validation_clock_context_t;

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
    char line[VALIDATION_CONSOLE_PROMPT_BUFFER_SIZE];
    printf("%s [%lu]: ", label, (unsigned long) default_value);
    fflush(stdout);
    read_line(line, sizeof(line));
    if (line[0] == '\0') {
        return default_value;
    }

    char *end_ptr = NULL;
    unsigned long value = strtoul(line, &end_ptr, 10);
    if (end_ptr == line || *end_ptr != '\0') {
        printf("Input was not a valid integer. Keeping %lu.\n", (unsigned long) default_value);
        return default_value;
    }

    return (uint32_t) value;
}

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

static const char *clock_source_name(validation_clock_source_t source)
{
    return source == VALIDATION_CLOCK_SOURCE_EFFECTIVE ? "effective" : "system";
}

static validation_clock_context_t current_clock_context(validation_clock_source_t source)
{
    validation_clock_context_t context = {
        .configured_sys_clk_hz = clock_get_hz(clk_sys),
        .timing_sys_clk_hz = clock_get_hz(clk_sys),
        .effective_selected = source == VALIDATION_CLOCK_SOURCE_EFFECTIVE,
        .effective_available = false,
    };

    if (source == VALIDATION_CLOCK_SOURCE_EFFECTIVE) {
        pio_sysclk_stability_monitor_snapshot_t snapshot;
        if (pio_sysclk_stability_monitor_get_snapshot(&snapshot) &&
            snapshot.configured_sys_clk_hz != 0u &&
            snapshot.updates_published != 0u &&
            snapshot.effective_sys_clk_hz != 0u) {
            context.configured_sys_clk_hz = snapshot.configured_sys_clk_hz;
            context.timing_sys_clk_hz = snapshot.effective_sys_clk_hz;
            context.effective_available = true;
        }
    }

    return context;
}

static uint32_t resolve_timing_sm_clk_hz(uint32_t configured_sm_clk_hz,
                                         const validation_clock_context_t *clock_context)
{
    if (clock_context == NULL || configured_sm_clk_hz == 0u ||
        clock_context->configured_sys_clk_hz == 0u || clock_context->timing_sys_clk_hz == 0u) {
        return configured_sm_clk_hz;
    }

    return (uint32_t) ((((uint64_t) configured_sm_clk_hz * clock_context->timing_sys_clk_hz) +
                        (clock_context->configured_sys_clk_hz / 2u)) /
                       clock_context->configured_sys_clk_hz);
}

static double sys_clk_to_ppm(uint32_t measured_sys_clk_hz,
                             uint32_t configured_sys_clk_hz)
{
    if (configured_sys_clk_hz == 0u) {
        return 0.0;
    }

    return (((double) measured_sys_clk_hz - (double) configured_sys_clk_hz) * 1000000.0) /
           (double) configured_sys_clk_hz;
}

static void print_clock_context(validation_clock_source_t source)
{
    validation_clock_context_t clock_context = current_clock_context(source);

    printf("Clock source: %s", clock_source_name(source));
    if (clock_context.effective_selected && !clock_context.effective_available) {
        printf(" (monitor data unavailable, using system clock)");
    }
    printf("\n");
    printf("  Configured clk_sys: %lu Hz\n", (unsigned long) clock_context.configured_sys_clk_hz);
    printf("  Timing clk_sys: %lu Hz\n", (unsigned long) clock_context.timing_sys_clk_hz);
    printf("  Timing offset: %.3f ppm\n",
           sys_clk_to_ppm(clock_context.timing_sys_clk_hz, clock_context.configured_sys_clk_hz));
}

static void print_monitor_snapshot(const pio_sysclk_stability_monitor_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        printf("Clock monitor has not been initialized yet.\n");
        return;
    }

    printf("Clock monitor:\n");
    printf("  Running: %s\n", snapshot->running ? "yes" : "no");
    printf("  PPS ready: %s\n", snapshot->pps_ready ? "yes" : "no");
    printf("  Configured clk_sys: %lu Hz\n", (unsigned long) snapshot->configured_sys_clk_hz);
    printf("  Published effective clk_sys: %lu Hz\n", (unsigned long) snapshot->effective_sys_clk_hz);
    printf("  Published offset: %.3f ppm\n",
           sys_clk_to_ppm(snapshot->effective_sys_clk_hz, snapshot->configured_sys_clk_hz));
    printf("  Batch size: %lu valid samples\n", (unsigned long) snapshot->update_interval_valid_samples);
    printf("  Last published batch: %lu samples\n", (unsigned long) snapshot->last_batch_valid_samples);
    printf("  Updates published: %lu\n", (unsigned long) snapshot->updates_published);
    printf("  Valid samples: %lu\n", (unsigned long) snapshot->total_valid_samples);
    printf("  Timeouts: %lu\n", (unsigned long) snapshot->total_timeouts);
    if (!snapshot->pps_ready && snapshot->updates_published > 0u) {
        printf("  Status: holding last published clock while waiting for PPS to restart\n");
    }
    if (snapshot->total_valid_samples > 0u) {
        printf("  Average clk_sys: %lu Hz\n", (unsigned long) snapshot->average_sys_clk_hz);
        printf("  Min/max clk_sys: %lu / %lu Hz\n",
               (unsigned long) snapshot->min_sys_clk_hz,
               (unsigned long) snapshot->max_sys_clk_hz);
        printf("  Average period: %llu ns\n", (unsigned long long) snapshot->average_period_ns);
        printf("  Min/max period: %llu / %llu ns\n",
               (unsigned long long) snapshot->min_period_ns,
               (unsigned long long) snapshot->max_period_ns);
    }
}

static void run_clock_monitor_menu(sysclk_monitor_menu_cfg_t *monitor_cfg)
{
    if (monitor_cfg == NULL) {
        return;
    }

    while (true) {
        pio_sysclk_stability_monitor_snapshot_t snapshot;
        bool have_snapshot = pio_sysclk_stability_monitor_get_snapshot(&snapshot);

        printf("\n=== Clock Monitor ===\n");
        printf("Config: PIO%u SM%u PPS=GP%u sm_clk=%lu Hz timeout=%lu ns batch=%lu\n",
               monitor_cfg->pio_index,
               monitor_cfg->sm,
               monitor_cfg->pps_pin,
               (unsigned long) monitor_cfg->sm_clk_hz,
               (unsigned long) monitor_cfg->timeout_ns,
               (unsigned long) monitor_cfg->update_interval_valid_samples);
        if (have_snapshot) {
            print_monitor_snapshot(&snapshot);
        } else {
            printf("Clock monitor has not published any data yet.\n");
        }
        printf("Commands: c=configure, r=start, t=stop, s=show stats, q=return\n");
        printf("Choose: ");

        char line[16];
        read_line(line, sizeof(line));

        if (line[0] == 'c' || line[0] == 'C') {
            monitor_cfg->pio_index = prompt_u32("PIO index (0, 1, or 2)", monitor_cfg->pio_index);
            monitor_cfg->sm = prompt_u32("SM index", monitor_cfg->sm);
            monitor_cfg->pps_pin = prompt_u32("PPS pin (rising edge)", monitor_cfg->pps_pin);
            monitor_cfg->sm_clk_hz = prompt_u32("State-machine clock (Hz)", monitor_cfg->sm_clk_hz);
            monitor_cfg->timeout_ns = prompt_u32("Timeout window (ns)", monitor_cfg->timeout_ns);
            monitor_cfg->update_interval_valid_samples =
                prompt_u32("Valid samples per published update", monitor_cfg->update_interval_valid_samples);
        } else if (line[0] == 'r' || line[0] == 'R') {
            pio_sysclk_stability_monitor_config_t run_cfg = {
                .pio = resolve_pio(monitor_cfg->pio_index),
                .sm = monitor_cfg->sm,
                .pps_pin = monitor_cfg->pps_pin,
                .sm_clk_hz = monitor_cfg->sm_clk_hz,
                .timeout_ns = monitor_cfg->timeout_ns,
                .update_interval_valid_samples = monitor_cfg->update_interval_valid_samples,
            };
            bool ok = pio_sysclk_stability_monitor_start(&run_cfg);
            printf("Clock monitor start: %s\n", ok ? "ok" : "failed or already running");
        } else if (line[0] == 't' || line[0] == 'T') {
            bool ok = pio_sysclk_stability_monitor_stop();
            printf("Clock monitor stop: %s\n", ok ? "ok" : "not running");
        } else if (line[0] == 's' || line[0] == 'S') {
            if (have_snapshot) {
                print_monitor_snapshot(&snapshot);
            } else {
                printf("Clock monitor has not published any data yet.\n");
            }
        } else if (line[0] == 'q' || line[0] == 'Q') {
            return;
        }
    }
}

static void run_clock_source_menu(validation_clock_source_t *clock_source)
{
    if (clock_source == NULL) {
        return;
    }

    printf("\nClock source selection\n");
    print_clock_context(*clock_source);
    printf("Select timing clock source: 0=system, 1=effective\n");
    {
        uint32_t value = prompt_u32("Clock source", (uint32_t) *clock_source);
        *clock_source = value == 0u ? VALIDATION_CLOCK_SOURCE_SYSTEM : VALIDATION_CLOCK_SOURCE_EFFECTIVE;
    }
    print_clock_context(*clock_source);
}

int main()
{
    stdio_init_all();

    const validation_status_led_config_t led_config = {
        .running_pin = VALIDATION_STATUS_LED_RUNNING_PIN,
        .initialized_pin = VALIDATION_STATUS_LED_INITIALIZED_PIN,
        .prepared_pin = VALIDATION_STATUS_LED_PREPARED_PIN,
        .armed_pin = VALIDATION_STATUS_LED_ARMED_PIN,
        .error_pin = VALIDATION_STATUS_LED_ERROR_PIN,
    };
    validation_status_leds_init(&led_config);

    while (!stdio_usb_connected()) {
        sleep_ms(VALIDATION_CONSOLE_USB_WAIT_MS);
    }
    sleep_ms(VALIDATION_CONSOLE_USB_SETTLE_MS);

    input_capture_menu_cfg_t input_cfg = {
        .pio_index = VALIDATION_INPUT_CAPTURE_DEFAULT_PIO_INDEX,
        .sm = VALIDATION_INPUT_CAPTURE_DEFAULT_SM,
        .start_pin = VALIDATION_INPUT_CAPTURE_DEFAULT_START_PIN,
        .stop_pin = VALIDATION_INPUT_CAPTURE_DEFAULT_STOP_PIN,
        .sm_clk_hz = VALIDATION_INPUT_CAPTURE_DEFAULT_SM_CLK_HZ,
        .timeout_ns = VALIDATION_INPUT_CAPTURE_DEFAULT_TIMEOUT_NS,
        .sample_count = VALIDATION_INPUT_CAPTURE_DEFAULT_SAMPLE_COUNT,
    };

    sysclk_monitor_menu_cfg_t monitor_cfg = {
        .pio_index = VALIDATION_SYSCLK_MONITOR_DEFAULT_PIO_INDEX,
        .sm = VALIDATION_SYSCLK_MONITOR_DEFAULT_SM,
        .pps_pin = VALIDATION_SYSCLK_MONITOR_DEFAULT_PPS_PIN,
        .sm_clk_hz = VALIDATION_SYSCLK_MONITOR_DEFAULT_SM_CLK_HZ,
        .timeout_ns = VALIDATION_SYSCLK_MONITOR_DEFAULT_TIMEOUT_NS,
        .update_interval_valid_samples = VALIDATION_SYSCLK_MONITOR_DEFAULT_BATCH_SIZE,
    };

    pio_timer_output_compare_validation_config_t output_cfg = {
        .pio_index = VALIDATION_OUTPUT_COMPARE_DEFAULT_PIO_INDEX,
        .sm = VALIDATION_OUTPUT_COMPARE_DEFAULT_SM,
        .trigger_pin = VALIDATION_OUTPUT_COMPARE_DEFAULT_TRIGGER_PIN,
        .output_pin = VALIDATION_OUTPUT_COMPARE_DEFAULT_OUTPUT_PIN,
        .continuous_mode = false,
        .sm_clk_hz = VALIDATION_OUTPUT_COMPARE_DEFAULT_SM_CLK_HZ,
        .timing_sm_clk_hz = VALIDATION_OUTPUT_COMPARE_DEFAULT_SM_CLK_HZ,
        .compare_ns = VALIDATION_OUTPUT_COMPARE_DEFAULT_COMPARE_NS,
        .pulse_ns = VALIDATION_OUTPUT_COMPARE_DEFAULT_PULSE_NS,
    };

    pio_alarm_timer_validation_config_t alarm_timer_cfg = {
        .pio_index = VALIDATION_ALARM_TIMER_DEFAULT_PIO_INDEX,
        .sm = VALIDATION_ALARM_TIMER_DEFAULT_SM,
        .pps_pin = VALIDATION_ALARM_TIMER_DEFAULT_PPS_PIN,
        .sm_clk_hz = VALIDATION_ALARM_TIMER_DEFAULT_SM_CLK_HZ,
        .timing_sm_clk_hz = VALIDATION_ALARM_TIMER_DEFAULT_SM_CLK_HZ,
        .first_alarm_tick = VALIDATION_ALARM_TIMER_DEFAULT_FIRST_TICK,
        .alarm_step_ticks = VALIDATION_ALARM_TIMER_DEFAULT_STEP_TICKS,
        .burst_count = VALIDATION_ALARM_TIMER_DEFAULT_BURST_COUNT,
    };

    ad9850_validation_config_t ad9850_cfg = {
        .spi_index = VALIDATION_AD9850_DEFAULT_SPI_INDEX,
        .spi_baud_hz = VALIDATION_AD9850_DEFAULT_SPI_BAUD_HZ,
        .sck_pin = VALIDATION_AD9850_DEFAULT_SCK_PIN,
        .mosi_pin = VALIDATION_AD9850_DEFAULT_MOSI_PIN,
        .use_fqud_pin = true,
        .fqud_pin = VALIDATION_AD9850_DEFAULT_FQUD_PIN,
        .fqud_pulse_us = VALIDATION_AD9850_DEFAULT_FQUD_PULSE_US,
        .use_reset_pin = true,
        .reset_pin = VALIDATION_AD9850_DEFAULT_RESET_PIN,
        .dds_sysclk_hz = VALIDATION_AD9850_DEFAULT_DDS_SYSCLK_HZ,
        .frequency_hz = VALIDATION_AD9850_DEFAULT_FREQUENCY_HZ,
        .phase = VALIDATION_AD9850_DEFAULT_PHASE,
        .power_down = false,
    };

    scheduler_validation_config_t scheduler_cfg = {
        .output_compare_pio_index = VALIDATION_SCHEDULER_DEFAULT_OUTPUT_COMPARE_PIO_INDEX,
        .alarm_timer_pio_index = VALIDATION_SCHEDULER_DEFAULT_ALARM_TIMER_PIO_INDEX,
        .output_compare_sm = VALIDATION_SCHEDULER_DEFAULT_OUTPUT_COMPARE_SM,
        .alarm_timer_sm = VALIDATION_SCHEDULER_DEFAULT_ALARM_TIMER_SM,
        .trigger_pin = VALIDATION_SCHEDULER_DEFAULT_TRIGGER_PIN,
        .output_pin = VALIDATION_SCHEDULER_DEFAULT_OUTPUT_PIN,
        .pps_pin = VALIDATION_SCHEDULER_DEFAULT_PPS_PIN,
        .sm_clk_hz = VALIDATION_SCHEDULER_DEFAULT_SM_CLK_HZ,
        .use_effective_clock = false,
        .timing_sys_clk_hz = 0u,
        .configured_sys_clk_hz = 0u,
        .output_pulse_us = VALIDATION_SCHEDULER_DEFAULT_OUTPUT_PULSE_US,
        .ad9850_spi_index = VALIDATION_AD9850_DEFAULT_SPI_INDEX,
        .ad9850_spi_baud_hz = VALIDATION_AD9850_DEFAULT_SPI_BAUD_HZ,
        .ad9850_sck_pin = VALIDATION_AD9850_DEFAULT_SCK_PIN,
        .ad9850_mosi_pin = VALIDATION_AD9850_DEFAULT_MOSI_PIN,
        .ad9850_use_fqud_pin = true,
        .ad9850_fqud_pin = VALIDATION_AD9850_DEFAULT_FQUD_PIN,
        .ad9850_fqud_pulse_us = VALIDATION_AD9850_DEFAULT_FQUD_PULSE_US,
        .ad9850_use_reset_pin = true,
        .ad9850_reset_pin = VALIDATION_AD9850_DEFAULT_RESET_PIN,
        .ad9850_sysclk_hz = VALIDATION_AD9850_DEFAULT_DDS_SYSCLK_HZ,
        .dt0_us = VALIDATION_SCHEDULER_DEFAULT_DT0_US,
        .symbol_count = VALIDATION_SCHEDULER_DEFAULT_SYMBOL_COUNT,
        .dts_us = VALIDATION_SCHEDULER_DEFAULT_DTS_US,
        .load_offset_us = VALIDATION_SCHEDULER_DEFAULT_LOAD_OFFSET_US,
        .carrier_hz = VALIDATION_SCHEDULER_DEFAULT_CARRIER_HZ,
        .baseband_hz = VALIDATION_SCHEDULER_DEFAULT_BASEBAND_HZ_LIST,
    };

    validation_clock_source_t clock_source = VALIDATION_CLOCK_SOURCE_SYSTEM;

    while (true) {
        validation_clock_context_t clock_context = current_clock_context(clock_source);

        printf("\n=== RP2350 Validation Console ===\n");
        printf("Clock source: %s", clock_source_name(clock_source));
        if (clock_context.effective_selected && !clock_context.effective_available) {
            printf(" (monitor data unavailable, using system clock)");
        }
        printf("\n");
        printf("Configured clk_sys=%lu Hz, timing clk_sys=%lu Hz\n",
               (unsigned long) clock_context.configured_sys_clk_hz,
               (unsigned long) clock_context.timing_sys_clk_hz);
        printf("1) Input capture       Measure delay from a start edge to a stop edge\n");
        printf("2) Clock monitor       Run or inspect the background PPS clock monitor\n");
        printf("3) Clock source        Choose system or effective timing conversions\n");
        printf("4) Output compare      Generate delayed pulses from a trigger\n");
        printf("5) Alarm timer         Queue PPS-gated alarm events\n");
        printf("6) AD9850              Exercise SPI writes and latch/reset control\n");
        printf("7) Scheduler           Run the output/alarm/DDS orchestration path\n");
        printf("q) Quit                Leave the console in an idle loop\n");
        printf("Choose a module: ");

        {
            char line[VALIDATION_CONSOLE_LINE_BUFFER_SIZE];
            read_line(line, sizeof(line));

            if (line[0] == '1') {
                printf("\nInput capture setup. Press Enter to keep the value shown in brackets.\n");
                input_cfg.pio_index = prompt_u32("PIO index (0, 1, or 2)", input_cfg.pio_index);
                input_cfg.sm = prompt_u32("SM index", input_cfg.sm);
                input_cfg.start_pin = prompt_u32("Start pin (rising edge)", input_cfg.start_pin);
                input_cfg.stop_pin = prompt_u32("Stop pin (rising edge)", input_cfg.stop_pin);
                input_cfg.sm_clk_hz = prompt_u32("State-machine clock (Hz)", input_cfg.sm_clk_hz);
                input_cfg.timeout_ns = prompt_u32("Timeout window (ns)", input_cfg.timeout_ns);
                input_cfg.sample_count = prompt_u32("Valid samples per report", input_cfg.sample_count);

                pio_timer_input_capture_validation_config_t run_cfg = {
                    .start_pin = input_cfg.start_pin,
                    .stop_pin = input_cfg.stop_pin,
                    .sm_clk_hz = input_cfg.sm_clk_hz,
                    .timing_sm_clk_hz = resolve_timing_sm_clk_hz(input_cfg.sm_clk_hz, &clock_context),
                    .timeout_ns = input_cfg.timeout_ns,
                    .sample_count = input_cfg.sample_count,
                    .pio = resolve_pio(input_cfg.pio_index),
                    .sm = input_cfg.sm,
                };
                pio_timer_input_capture_validation_run(&run_cfg);
            } else if (line[0] == '2') {
                run_clock_monitor_menu(&monitor_cfg);
            } else if (line[0] == '3') {
                run_clock_source_menu(&clock_source);
            } else if (line[0] == '4') {
                printf("\nOutput compare setup. Press Enter to keep the value shown in brackets.\n");
                output_cfg.pio_index = prompt_u32("PIO index (0, 1, or 2)", output_cfg.pio_index);
                output_cfg.sm = prompt_u32("SM index", output_cfg.sm);
                output_cfg.trigger_pin = prompt_u32("Trigger pin", output_cfg.trigger_pin);
                output_cfg.output_pin = prompt_u32("Pulse output pin", output_cfg.output_pin);
                output_cfg.continuous_mode =
                    prompt_u32("Playback mode (0=one-shot, 1=continuous)",
                               output_cfg.continuous_mode ? 1u : 0u) != 0u;
                output_cfg.sm_clk_hz = prompt_u32("State-machine clock (Hz)", output_cfg.sm_clk_hz);
                output_cfg.compare_ns = prompt_u32("Compare delay (ns)", output_cfg.compare_ns);
                output_cfg.pulse_ns = prompt_u32("Pulse width (ns)", output_cfg.pulse_ns);

                {
                    pio_timer_output_compare_validation_config_t run_cfg = output_cfg;
                    run_cfg.timing_sm_clk_hz = resolve_timing_sm_clk_hz(output_cfg.sm_clk_hz, &clock_context);
                    pio_timer_output_compare_validation_run(&run_cfg);
                }
            } else if (line[0] == '5') {
                printf("\nAlarm timer setup. Press Enter to keep the value shown in brackets.\n");
                alarm_timer_cfg.pio_index = prompt_u32("PIO index (0, 1, or 2)", alarm_timer_cfg.pio_index);
                alarm_timer_cfg.sm = prompt_u32("SM index", alarm_timer_cfg.sm);
                alarm_timer_cfg.pps_pin = prompt_u32("PPS input pin", alarm_timer_cfg.pps_pin);
                alarm_timer_cfg.sm_clk_hz = prompt_u32("State-machine clock (Hz)", alarm_timer_cfg.sm_clk_hz);
                alarm_timer_cfg.first_alarm_tick = prompt_u32("First alarm tick", alarm_timer_cfg.first_alarm_tick);
                alarm_timer_cfg.alarm_step_ticks = prompt_u32("Tick step between alarms", alarm_timer_cfg.alarm_step_ticks);
                alarm_timer_cfg.burst_count = prompt_u32("Burst length", alarm_timer_cfg.burst_count);

                {
                    pio_alarm_timer_validation_config_t run_cfg = alarm_timer_cfg;
                    run_cfg.timing_sm_clk_hz = resolve_timing_sm_clk_hz(alarm_timer_cfg.sm_clk_hz, &clock_context);
                    pio_alarm_timer_validation_run(&run_cfg);
                }
            } else if (line[0] == '6') {
                printf("\nAD9850 setup. Press Enter to keep the value shown in brackets.\n");
                ad9850_cfg.spi_index = prompt_u32("SPI index (0 or 1)", ad9850_cfg.spi_index);
                ad9850_cfg.spi_baud_hz = prompt_u32("SPI baud rate (Hz)", ad9850_cfg.spi_baud_hz);
                ad9850_cfg.sck_pin = prompt_u32("SCK pin", ad9850_cfg.sck_pin);
                ad9850_cfg.mosi_pin = prompt_u32("MOSI pin", ad9850_cfg.mosi_pin);
                ad9850_cfg.use_fqud_pin =
                    prompt_u32("Drive FQ_UD pin (0/1)", ad9850_cfg.use_fqud_pin ? 1u : 0u) != 0u;
                ad9850_cfg.fqud_pin = prompt_u32("FQ_UD pin", ad9850_cfg.fqud_pin);
                ad9850_cfg.fqud_pulse_us = prompt_u32("FQ_UD pulse length (us)", ad9850_cfg.fqud_pulse_us);
                ad9850_cfg.use_reset_pin =
                    prompt_u32("Drive RESET pin (0/1)", ad9850_cfg.use_reset_pin ? 1u : 0u) != 0u;
                ad9850_cfg.reset_pin = prompt_u32("Reset pin", ad9850_cfg.reset_pin);
                ad9850_cfg.dds_sysclk_hz = prompt_u32("DDS system clock (Hz)", ad9850_cfg.dds_sysclk_hz);
                ad9850_cfg.frequency_hz = prompt_u32("Initial frequency (Hz)", ad9850_cfg.frequency_hz);
                ad9850_cfg.phase = prompt_u32("Initial phase (0..31)", ad9850_cfg.phase);
                ad9850_cfg.power_down =
                    prompt_u32("Start in power-down (0/1)", ad9850_cfg.power_down ? 1u : 0u) != 0u;

                ad9850_validation_run(&ad9850_cfg);
            } else if (line[0] == '7') {
                printf("\nScheduler setup. Module-specific prompts will follow.\n");
                {
                    scheduler_validation_config_t run_cfg = scheduler_cfg;
                    run_cfg.use_effective_clock = clock_context.effective_selected;
                    run_cfg.timing_sys_clk_hz = clock_context.timing_sys_clk_hz;
                    run_cfg.configured_sys_clk_hz = clock_context.configured_sys_clk_hz;
                    scheduler_validation_run(&run_cfg);
                }
            } else if (line[0] == 'q' || line[0] == 'Q') {
                printf("Validation console is now idle. Reset the board to start again.\n");
                while (true) {
                    tight_loop_contents();
                }
            } else {
                printf("Unknown selection. Choose 1-7 or q.\n");
            }
        }
    }
}
