#include <stdio.h>

#include "driver/pio_alarm_timer/pio_alarm_timer.h"
#include "pio_alarm_timer.pio.h"
#include "src/validation/validation_config.h"
#include "src/validation/pio_alarm_timer_validation.h"

typedef struct {
    volatile uint32_t rearm_ack_count;
    volatile uint32_t late_count;
    volatile uint32_t fired_count;

    volatile uint32_t last_fired_tick;

    uint32_t rearm_ack_logged;
    uint32_t late_logged;
    uint32_t fired_logged;
} alarm_validation_runtime_t;

static PIO resolve_pio(uint pio_index)
{
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

static void on_alarm_result(const pio_alarm_timer_result_t *result,
                            void *user_data)
{
    alarm_validation_runtime_t *runtime = (alarm_validation_runtime_t *) user_data;

    if (result->kind == PIO_ALARM_TIMER_RESULT_KIND_REARM_ACK) {
        runtime->rearm_ack_count++;
    } else if (result->kind == PIO_ALARM_TIMER_RESULT_KIND_LATE) {
        runtime->late_count++;
    } else {
        runtime->fired_count++;
        runtime->last_fired_tick = result->tick;
    }
}

static const char *enqueue_status_str(pio_alarm_timer_enqueue_status_t status)
{
    switch (status) {
    case PIO_ALARM_TIMER_ENQUEUE_OK:
        return "ok";
    case PIO_ALARM_TIMER_ENQUEUE_ERR_NOT_INIT:
        return "not_init";
    case PIO_ALARM_TIMER_ENQUEUE_ERR_ZERO_TICK:
        return "zero_tick";
    case PIO_ALARM_TIMER_ENQUEUE_ERR_NON_MONOTONIC:
        return "non_monotonic_rearmed";
    case PIO_ALARM_TIMER_ENQUEUE_ERR_TX_FULL:
        return "tx_full";
    default:
        return "unknown";
    }
}

static uint32_t ticks_to_us(uint32_t ticks,
                            uint32_t sm_clk_hz)
{
    uint64_t numerator = (uint64_t) ticks * (uint64_t) VALIDATION_ALARM_TIMER_CYCLES_PER_TICK * VALIDATION_USEC_PER_SEC;
    return (uint32_t) (numerator / (uint64_t) sm_clk_hz);
}

static void print_runtime_summary(const alarm_validation_runtime_t *runtime,
                      uint32_t sm_clk_hz)
{
    printf("Event counters:\n");
    printf("  Rearm acknowledgements: %lu\n",
        (unsigned long) runtime->rearm_ack_count);
    printf("  Late alarms: %lu\n",
        (unsigned long) runtime->late_count);
    printf("  Fired alarms: %lu\n",
        (unsigned long) runtime->fired_count);
    if (runtime->fired_count > 0u) {
     printf("  Last fired tick: %lu (~%lu us)\n",
         (unsigned long) runtime->last_fired_tick,
         (unsigned long) ticks_to_us(runtime->last_fired_tick, sm_clk_hz));
    }
}

void pio_alarm_timer_validation_run(const pio_alarm_timer_validation_config_t *config)
{
    static bool program_loaded[VALIDATION_ALARM_TIMER_VALIDATION_PIO_COUNT] = {false};
    static uint program_offset[VALIDATION_ALARM_TIMER_VALIDATION_PIO_COUNT] = {0u};
    uint32_t timing_sm_clk_hz = config->timing_sm_clk_hz == 0u ? config->sm_clk_hz : config->timing_sm_clk_hz;

    PIO pio = resolve_pio(config->pio_index);
    uint slot = config->pio_index;
    if (slot >= VALIDATION_ALARM_TIMER_VALIDATION_PIO_COUNT) {
        slot = 0u;
    }

    if (!program_loaded[slot]) {
        program_offset[slot] = pio_add_program(pio, &pio_alarm_timer_program);
        program_loaded[slot] = true;
    }

    pio_alarm_timer_t timer;
    pio_alarm_timer_init(&timer,
                         pio,
                         config->sm,
                         program_offset[slot],
                         config->pps_pin,
                         (float) config->sm_clk_hz);

    alarm_validation_runtime_t runtime = {
        .rearm_ack_count = 0u,
        .late_count = 0u,
        .fired_count = 0u,
        .last_fired_tick = 0u,
        .rearm_ack_logged = 0u,
        .late_logged = 0u,
        .fired_logged = 0u,
    };

    pio_alarm_timer_set_rx_irq_callback(&timer, on_alarm_result, &runtime);

    bool initial_rearm_ok = pio_alarm_timer_queue_rearm(&timer);

    uint32_t next_alarm_tick = config->first_alarm_tick;
    if (next_alarm_tick == 0u) {
        next_alarm_tick = VALIDATION_ALARM_TIMER_DEFAULT_FIRST_TICK;
    }

    uint32_t alarm_step_ticks = config->alarm_step_ticks;
    if (alarm_step_ticks == 0u) {
        alarm_step_ticks = VALIDATION_ALARM_TIMER_DEFAULT_STEP_TICKS;
    }

    uint32_t burst_count = config->burst_count;
    if (burst_count == 0u) {
        burst_count = VALIDATION_ALARM_TIMER_DEFAULT_BURST_COUNT;
    }

        printf("\nAlarm timer validation active\n");
          printf("PIO%u SM%u PPS=GP%u state-machine clock=%lu Hz, timing clock=%lu Hz\n",
           config->pio_index,
           config->sm,
            config->pps_pin,
              (unsigned long) config->sm_clk_hz,
              (unsigned long) timing_sm_clk_hz);
        printf("Schedule:\n");
        printf("  First tick: %lu\n",
           (unsigned long) next_alarm_tick);
        printf("  Tick step: %lu\n",
            (unsigned long) alarm_step_ticks);
        printf("  Burst length: %lu\n",
            (unsigned long) burst_count);
        printf("Timing:\n");
        printf("  One timer tick currently equals %u state-machine cycles\n",
            (unsigned) VALIDATION_ALARM_TIMER_CYCLES_PER_TICK);
        printf("  First tick ~= %lu us\n",
            (unsigned long) ticks_to_us(next_alarm_tick, timing_sm_clk_hz));
        printf("  Tick step ~= %lu us\n",
            (unsigned long) ticks_to_us(alarm_step_ticks, timing_sm_clk_hz));
        printf("Startup rearm: %s; waiting for PPS on GP%u\n",
            initial_rearm_ok ? "queued" : "failed",
            config->pps_pin);
        printf("Commands: r=rearm, a=queue next alarm, b=queue burst, d=test descending guard, s=show counters, q=return\n");

    while (true) {
        pio_alarm_timer_result_t polled_result;
        while (pio_alarm_timer_try_read_decoded_result(&timer, &polled_result)) {
            on_alarm_result(&polled_result, &runtime);
        }

        while (runtime.rearm_ack_logged < runtime.rearm_ack_count) {
            runtime.rearm_ack_logged++;
            printf("Rearm acknowledgement #%lu\n", (unsigned long) runtime.rearm_ack_logged);
        }
        while (runtime.late_logged < runtime.late_count) {
            runtime.late_logged++;
            printf("Late alarm #%lu\n", (unsigned long) runtime.late_logged);
        }
        while (runtime.fired_logged < runtime.fired_count) {
            runtime.fired_logged++;
            printf("Alarm fired #%lu\n",
                   (unsigned long) runtime.fired_logged,
                   (unsigned long) runtime.fired_logged);
            printf("  Tick: %lu\n",
                   (unsigned long) runtime.last_fired_tick);
            printf("  Approximate time: %lu us\n",
                     (unsigned long) ticks_to_us(runtime.last_fired_tick, timing_sm_clk_hz));
        }

        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        if (ch == 'r' || ch == 'R') {
            bool ok = pio_alarm_timer_queue_rearm(&timer);
            printf("Queue rearm: %s\n", ok ? "ok" : "failed");
            next_alarm_tick = config->first_alarm_tick == 0u
                                 ? VALIDATION_ALARM_TIMER_DEFAULT_FIRST_TICK
                                 : config->first_alarm_tick;
        } else if (ch == 'a' || ch == 'A') {
            pio_alarm_timer_enqueue_status_t status =
                pio_alarm_timer_queue_alarm(&timer, next_alarm_tick);
            printf("Queue next alarm:\n");
            printf("  Tick: %lu\n",
                   (unsigned long) next_alarm_tick,
                   (unsigned long) next_alarm_tick);
            printf("  Approximate time: %lu us\n",
                     (unsigned long) ticks_to_us(next_alarm_tick, timing_sm_clk_hz),
                     (unsigned long) ticks_to_us(next_alarm_tick, timing_sm_clk_hz));
            printf("  Status: %s\n", enqueue_status_str(status));
            if (status == PIO_ALARM_TIMER_ENQUEUE_OK) {
                next_alarm_tick += alarm_step_ticks;
            }
        } else if (ch == 'b' || ch == 'B') {
            uint32_t queued = 0u;
            for (uint32_t i = 0; i < burst_count; ++i) {
                pio_alarm_timer_enqueue_status_t status =
                    pio_alarm_timer_queue_alarm(&timer, next_alarm_tick);
                if (status != PIO_ALARM_TIMER_ENQUEUE_OK) {
                    printf("Queue burst stopped early:\n");
                    printf("  Burst index: %lu\n",
                           (unsigned long) i,
                           (unsigned long) i);
                    printf("  Tick: %lu\n",
                           (unsigned long) next_alarm_tick,
                           (unsigned long) next_alarm_tick);
                    printf("  Status: %s\n", enqueue_status_str(status));
                    break;
                }
                queued++;
                next_alarm_tick += alarm_step_ticks;
            }
            printf("Queue burst: queued %lu alarm(s)\n", (unsigned long) queued);
        } else if (ch == 'd' || ch == 'D') {
            uint32_t descending_tick = next_alarm_tick > alarm_step_ticks
                                           ? (next_alarm_tick - alarm_step_ticks)
                                           : VALIDATION_SYSCLK_STABILITY_MIN_SAMPLES_FALLBACK;
            pio_alarm_timer_enqueue_status_t status =
                pio_alarm_timer_queue_alarm(&timer, descending_tick);
            printf("Queue descending tick test:\n");
            printf("  Tick: %lu\n",
                   (unsigned long) descending_tick,
                   (unsigned long) descending_tick);
            printf("  Approximate time: %lu us\n",
                     (unsigned long) ticks_to_us(descending_tick, timing_sm_clk_hz));
            printf("  Status: %s\n", enqueue_status_str(status));
            next_alarm_tick = config->first_alarm_tick == 0u
                                 ? VALIDATION_ALARM_TIMER_DEFAULT_FIRST_TICK
                                 : config->first_alarm_tick;
        } else if (ch == 's' || ch == 'S') {
                 print_runtime_summary(&runtime, timing_sm_clk_hz);
        } else if (ch == 'q' || ch == 'Q') {
            printf("Leaving PIO alarm timer validation\n");
            break;
        }
    }

    pio_alarm_timer_clear_rx_irq_callback(&timer);
}
