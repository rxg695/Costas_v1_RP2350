#include "src/scheduler/scheduler.h"

#include <string.h>

#include "driver/pio_timer_output_compare/pio_timer_output_compare.h"
#include "hardware/irq.h"
#include "pio_alarm_timer.pio.h"
#include "pio_timer_output_compare.pio.h"

#define SCHEDULER_PIO_COUNT 3u

static bool output_compare_program_loaded[SCHEDULER_PIO_COUNT] = {false};
static uint output_compare_program_offset[SCHEDULER_PIO_COUNT] = {0u};
static bool alarm_timer_program_loaded[SCHEDULER_PIO_COUNT] = {false};
static uint alarm_timer_program_offset[SCHEDULER_PIO_COUNT] = {0u};
static scheduler_t *tx_irq_registry[SCHEDULER_PIO_COUNT] = {0};
static bool tx_irq_handler_installed[SCHEDULER_PIO_COUNT] = {false};

static int scheduler_pio_slot(PIO pio);
static void feed_output_compare_fifo(scheduler_t *scheduler);
static void feed_alarm_fifo(scheduler_t *scheduler);

static void scheduler_init_output_compare_sm(const scheduler_t *scheduler)
{
    if (scheduler == NULL) {
        return;
    }

    int slot = scheduler_pio_slot(scheduler->cfg.output_compare_pio);
    if (slot < 0) {
        return;
    }

    pio_timer_output_compare_init(scheduler->cfg.output_compare_pio,
                                  scheduler->cfg.output_compare_sm,
                                  output_compare_program_offset[slot],
                                  scheduler->cfg.trigger_pin,
                                  scheduler->cfg.output_pin,
                                  (float) scheduler->cfg.sm_clk_hz,
                                  PIO_TIMER_OUTPUT_COMPARE_MODE_CONTINUOUS);
}

static int scheduler_pio_slot(PIO pio)
{
    if (pio == pio0) {
        return 0;
    }
    if (pio == pio1) {
        return 1;
    }
    if (pio == pio2) {
        return 2;
    }
    return -1;
}

static enum pio_interrupt_source scheduler_tx_not_full_source_for_sm(uint sm)
{
    switch (sm) {
    case 0:
        return pis_sm0_tx_fifo_not_full;
    case 1:
        return pis_sm1_tx_fifo_not_full;
    case 2:
        return pis_sm2_tx_fifo_not_full;
    default:
        return pis_sm3_tx_fifo_not_full;
    }
}

static void scheduler_set_tx_irq_enabled(scheduler_t *scheduler,
                                         PIO pio,
                                         uint sm,
                                         bool enabled)
{
    if (scheduler == NULL) {
        return;
    }

    pio_set_irq1_source_enabled(pio,
                                scheduler_tx_not_full_source_for_sm(sm),
                                enabled);
}

static bool sw_fifo_push(scheduler_sw_fifo_t *fifo,
                         uint32_t word)
{
    if (fifo == NULL || fifo->count >= SCHEDULER_SW_FIFO_WORDS) {
        return false;
    }

    fifo->words[fifo->head] = word;
    fifo->head = (uint16_t) ((fifo->head + 1u) % SCHEDULER_SW_FIFO_WORDS);
    fifo->count++;
    return true;
}

static bool sw_fifo_pop(scheduler_sw_fifo_t *fifo,
                        uint32_t *word_out)
{
    if (fifo == NULL || word_out == NULL || fifo->count == 0u) {
        return false;
    }

    *word_out = fifo->words[fifo->tail];
    fifo->tail = (uint16_t) ((fifo->tail + 1u) % SCHEDULER_SW_FIFO_WORDS);
    fifo->count--;
    return true;
}

static bool sw_fifo_peek(const scheduler_sw_fifo_t *fifo,
                         uint32_t *word_out)
{
    if (fifo == NULL || word_out == NULL || fifo->count == 0u) {
        return false;
    }

    *word_out = fifo->words[fifo->tail];
    return true;
}

static bool sw_fifo_peek_at(const scheduler_sw_fifo_t *fifo,
                            uint16_t offset,
                            uint32_t *word_out)
{
    if (fifo == NULL || word_out == NULL || fifo->count <= offset) {
        return false;
    }

    uint16_t idx = (uint16_t) ((fifo->tail + offset) % SCHEDULER_SW_FIFO_WORDS);
    *word_out = fifo->words[idx];
    return true;
}

static void sw_fifo_clear(scheduler_sw_fifo_t *fifo)
{
    if (fifo == NULL) {
        return;
    }

    fifo->head = 0u;
    fifo->tail = 0u;
    fifo->count = 0u;
}

static void __not_in_flash_func(scheduler_service_ad9850_nonblocking)(scheduler_t *scheduler)
{
    if (scheduler == NULL || scheduler->state == SCHEDULER_STATE_END_FAULT) {
        return;
    }

    ad9850_driver_service_nonblocking(&scheduler->ad9850);
    bool success = false;
    if (ad9850_driver_take_nonblocking_result(&scheduler->ad9850, &success) && !success) {
        scheduler->last_error = SCHEDULER_ERROR_AD9850;
        scheduler->state = SCHEDULER_STATE_END_FAULT;
    }
}

static void __not_in_flash_func(scheduler_tx_irq_dispatch)(PIO pio)
{
    int slot = scheduler_pio_slot(pio);
    if (slot < 0) {
        return;
    }

    scheduler_t *scheduler = tx_irq_registry[slot];
    if (scheduler == NULL) {
        return;
    }

    if (scheduler->cfg.output_compare_pio == pio) {
        scheduler_on_tx_fifo_not_full_irq(scheduler, scheduler->cfg.output_compare_sm);
    }

    if (scheduler->cfg.alarm_timer_pio == pio) {
        scheduler_on_tx_fifo_not_full_irq(scheduler, scheduler->cfg.alarm_timer_sm);
    }
}

static void __not_in_flash_func(scheduler_pio0_irq1_handler)(void)
{
    scheduler_tx_irq_dispatch(pio0);
}

static void __not_in_flash_func(scheduler_pio1_irq1_handler)(void)
{
    scheduler_tx_irq_dispatch(pio1);
}

static void __not_in_flash_func(scheduler_pio2_irq1_handler)(void)
{
    scheduler_tx_irq_dispatch(pio2);
}

static void scheduler_ensure_tx_irq_installed(PIO pio)
{
    int slot = scheduler_pio_slot(pio);
    if (slot < 0 || tx_irq_handler_installed[slot]) {
        return;
    }

    if (slot == 0) {
        irq_set_exclusive_handler(PIO0_IRQ_1, scheduler_pio0_irq1_handler);
        irq_set_priority(PIO0_IRQ_1, 0);
        irq_set_enabled(PIO0_IRQ_1, true);
    } else if (slot == 1) {
        irq_set_exclusive_handler(PIO1_IRQ_1, scheduler_pio1_irq1_handler);
        irq_set_priority(PIO1_IRQ_1, 0);
        irq_set_enabled(PIO1_IRQ_1, true);
    } else if (slot == 2) {
        irq_set_exclusive_handler(PIO2_IRQ_1, scheduler_pio2_irq1_handler);
        irq_set_priority(PIO2_IRQ_1, 0);
        irq_set_enabled(PIO2_IRQ_1, true);
    } else {
        return;
    }

    tx_irq_handler_installed[slot] = true;
}

static bool scheduler_build_sequences(scheduler_t *scheduler,
                                      const scheduler_prepare_request_t *request)
{
    if (scheduler == NULL || request == NULL || request->dts == NULL) {
        return false;
    }

    if (request->symbol_count == 0u || request->symbol_count > SCHEDULER_MAX_SYMBOLS) {
        scheduler->last_error = SCHEDULER_ERROR_INVALID_ARG;
        return false;
    }

    if (request->ftw_frames == NULL && request->freq_hz == NULL) {
        scheduler->last_error = SCHEDULER_ERROR_INVALID_ARG;
        return false;
    }

    sw_fifo_clear(&scheduler->output_fifo);
    sw_fifo_clear(&scheduler->alarm_fifo);
    scheduler->output_stop_queued = false;

    // Output compare uses a 5x tick scale relative to the alarm sequence.
    const int64_t output_scale = 5ll;

    int64_t output_abs = (int64_t) request->dt0 * output_scale;
    if (output_abs <= 0 || output_abs > 0xFFFFFFFFll) {
        scheduler->last_error = SCHEDULER_ERROR_SEQUENCE_OVERFLOW;
        return false;
    }
    scheduler->output_compare_sequence[0] = (uint32_t) output_abs;

    int64_t alarm_abs = (int64_t) request->dt0 - (int64_t) request->load_offset;
    if (alarm_abs <= 0 || alarm_abs > 0xFFFFFFFFll) {
        scheduler->last_error = SCHEDULER_ERROR_SEQUENCE_OVERFLOW;
        return false;
    }
    scheduler->alarm_timer_sequence[0] = (uint32_t) alarm_abs;

    if (!sw_fifo_push(&scheduler->output_fifo, (uint32_t) output_abs) ||
        !sw_fifo_push(&scheduler->output_fifo, scheduler->cfg.output_pulse_ticks)) {
        scheduler->last_error = SCHEDULER_ERROR_OUTPUT_ENQUEUE;
        return false;
    }

    if (!sw_fifo_push(&scheduler->alarm_fifo, (uint32_t) alarm_abs)) {
        scheduler->last_error = SCHEDULER_ERROR_ALARM_ENQUEUE;
        return false;
    }

    for (uint32_t i = 1u; i < request->symbol_count; ++i) {
        int64_t dt = (int64_t) request->dts[i - 1u];
        if (dt <= 0 || dt > 0xFFFFFFFFll) {
            scheduler->last_error = SCHEDULER_ERROR_SEQUENCE_OVERFLOW;
            return false;
        }

        int64_t dt_scaled = dt * output_scale;
        if (dt_scaled <= 0 || dt_scaled > 0xFFFFFFFFll) {
            scheduler->last_error = SCHEDULER_ERROR_SEQUENCE_OVERFLOW;
            return false;
        }

        output_abs += dt_scaled;
        if (output_abs <= 0 || output_abs > 0xFFFFFFFFll) {
            scheduler->last_error = SCHEDULER_ERROR_SEQUENCE_OVERFLOW;
            return false;
        }
        scheduler->output_compare_sequence[i] = (uint32_t) output_abs;

        // Continuous output compare starts counting the next delay only after
        // the previous pulse finishes, so convert requested pulse-start spacing
        // into a compare delay by removing the programmed pulse width.
        int64_t output_compare_delay = dt_scaled - (int64_t) scheduler->cfg.output_pulse_ticks;
        if (output_compare_delay <= 0 || output_compare_delay > 0xFFFFFFFFll) {
            scheduler->last_error = SCHEDULER_ERROR_SEQUENCE_OVERFLOW;
            return false;
        }

        if (!sw_fifo_push(&scheduler->output_fifo, (uint32_t) output_compare_delay) ||
            !sw_fifo_push(&scheduler->output_fifo, scheduler->cfg.output_pulse_ticks)) {
            scheduler->last_error = SCHEDULER_ERROR_OUTPUT_ENQUEUE;
            return false;
        }

        alarm_abs += dt;
        if (alarm_abs <= 0 || alarm_abs > 0xFFFFFFFFll) {
            scheduler->last_error = SCHEDULER_ERROR_SEQUENCE_OVERFLOW;
            return false;
        }
        scheduler->alarm_timer_sequence[i] = (uint32_t) alarm_abs;

        if (!sw_fifo_push(&scheduler->alarm_fifo, (uint32_t) alarm_abs)) {
            scheduler->last_error = SCHEDULER_ERROR_ALARM_ENQUEUE;
            return false;
        }
    }

    int64_t final_alarm_abs = alarm_abs + (int64_t) request->load_offset;
    if (final_alarm_abs <= 0 || final_alarm_abs > 0xFFFFFFFFll) {
        scheduler->last_error = SCHEDULER_ERROR_SEQUENCE_OVERFLOW;
        return false;
    }

    scheduler->alarm_timer_sequence[request->symbol_count] = (uint32_t) final_alarm_abs;
    if (!sw_fifo_push(&scheduler->alarm_fifo, (uint32_t) final_alarm_abs)) {
        scheduler->last_error = SCHEDULER_ERROR_ALARM_ENQUEUE;
        return false;
    }

    for (uint32_t i = 0u; i < request->symbol_count; ++i) {
        if (request->ftw_frames != NULL) {
            scheduler->prepared_frames[i] = request->ftw_frames[i];
        } else {
            uint32_t ftw = 0u;
            if (request->freq_hz[i] != 0u) {
                if (!ad9850_driver_frequency_hz_to_ftw(&scheduler->ad9850, request->freq_hz[i], &ftw)) {
                    scheduler->last_error = SCHEDULER_ERROR_AD9850;
                    return false;
                }
            }

            if (!ad9850_driver_make_frame(ftw, 0u, false, &scheduler->prepared_frames[i])) {
                scheduler->last_error = SCHEDULER_ERROR_AD9850;
                return false;
            }
        }
    }

    scheduler->symbol_count = request->symbol_count;
    scheduler->next_alarm_index = 0u;
    scheduler->next_write_symbol = 0u;
    return true;
}

static void scheduler_raise_fault(scheduler_t *scheduler,
                                  scheduler_error_t error)
{
    if (scheduler == NULL) {
        return;
    }

    scheduler->last_error = error;
    scheduler->state = SCHEDULER_STATE_END_FAULT;

    scheduler_set_tx_irq_enabled(scheduler,
                                 scheduler->cfg.output_compare_pio,
                                 scheduler->cfg.output_compare_sm,
                                 false);
    scheduler_set_tx_irq_enabled(scheduler,
                                 scheduler->cfg.alarm_timer_pio,
                                 scheduler->cfg.alarm_timer_sm,
                                 false);

    ad9850_frame_t power_down_frame;
    if (ad9850_driver_make_frame(0u, 0u, true, &power_down_frame)) {
        (void) ad9850_driver_start_apply_nonblocking(&scheduler->ad9850,
                                                     &power_down_frame,
                                                     scheduler->cfg.ad9850_use_fqud_pin);
    }

    if (scheduler->fault_callback != NULL) {
        scheduler->fault_callback(error, scheduler->fault_user_data);
    }
}

static void feed_output_compare_fifo(scheduler_t *scheduler)
{
    if (scheduler == NULL) {
        return;
    }

    while (scheduler->output_fifo.count >= 2u) {
        uint32_t compare_ticks;
        uint32_t pulse_ticks;
        if (!sw_fifo_peek_at(&scheduler->output_fifo, 0u, &compare_ticks) ||
            !sw_fifo_peek_at(&scheduler->output_fifo, 1u, &pulse_ticks)) {
            continue;
        }

        if (!pio_timer_output_compare_try_queue_event(scheduler->cfg.output_compare_pio,
                                                      scheduler->cfg.output_compare_sm,
                                                      compare_ticks,
                                                      pulse_ticks)) {
            break;
        }

        uint32_t discard;
        (void) sw_fifo_pop(&scheduler->output_fifo, &discard);
        (void) sw_fifo_pop(&scheduler->output_fifo, &discard);
        scheduler->output_feed_count++;
    }

    if (scheduler->output_fifo.count == 0u && !scheduler->output_stop_queued) {
                if (pio_timer_output_compare_try_queue_stop(scheduler->cfg.output_compare_pio,
                                                                                                        scheduler->cfg.output_compare_sm)) {
            scheduler->output_stop_queued = true;
            scheduler_set_tx_irq_enabled(scheduler,
                                         scheduler->cfg.output_compare_pio,
                                         scheduler->cfg.output_compare_sm,
                                         false);
        }
    }
}

static void feed_alarm_fifo(scheduler_t *scheduler)
{
    if (scheduler == NULL || scheduler->state != SCHEDULER_STATE_ARM) {
        return;
    }

    while (scheduler->alarm_fifo.count > 0u) {
        uint32_t alarm_tick;
        if (!sw_fifo_peek(&scheduler->alarm_fifo, &alarm_tick)) {
            break;
        }

        pio_alarm_timer_enqueue_status_t status =
            pio_alarm_timer_queue_alarm(&scheduler->alarm_timer, alarm_tick);
        if (status == PIO_ALARM_TIMER_ENQUEUE_OK) {
            (void) sw_fifo_pop(&scheduler->alarm_fifo, &alarm_tick);
            scheduler->alarm_feed_count++;
            continue;
        }

        if (status == PIO_ALARM_TIMER_ENQUEUE_ERR_TX_FULL) {
            break;
        }

        if (status == PIO_ALARM_TIMER_ENQUEUE_ERR_NON_MONOTONIC) {
            scheduler_raise_fault(scheduler, SCHEDULER_ERROR_UNEXPECTED_ALARM_TICK);
            return;
        }

        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_ALARM_ENQUEUE);
        return;
    }

    if (scheduler->alarm_fifo.count == 0u) {
        scheduler_set_tx_irq_enabled(scheduler,
                                     scheduler->cfg.alarm_timer_pio,
                                     scheduler->cfg.alarm_timer_sm,
                                     false);
    }
}

bool scheduler_init(scheduler_t *scheduler,
                    const scheduler_config_t *config)
{
    if (scheduler == NULL || config == NULL ||
        config->output_compare_pio == NULL || config->alarm_timer_pio == NULL) {
        return false;
    }

    if (config->output_compare_sm >= 4u ||
        config->alarm_timer_sm >= 4u ||
        (config->output_compare_pio == config->alarm_timer_pio &&
         config->output_compare_sm == config->alarm_timer_sm) ||
        config->sm_clk_hz == 0u ||
        config->ad9850_spi == NULL ||
        config->ad9850_spi_baud_hz == 0u ||
        config->ad9850_sysclk_hz == 0u) {
        return false;
    }

    memset(scheduler, 0, sizeof(*scheduler));
    scheduler->cfg = *config;
    scheduler->state = SCHEDULER_STATE_INIT;
    scheduler->last_error = SCHEDULER_ERROR_NONE;

    int output_slot = scheduler_pio_slot(config->output_compare_pio);
    int alarm_slot = scheduler_pio_slot(config->alarm_timer_pio);
    if (output_slot < 0 || alarm_slot < 0) {
        return false;
    }

    if (!output_compare_program_loaded[output_slot]) {
        output_compare_program_offset[output_slot] =
            pio_add_program(config->output_compare_pio, &pio_timer_output_compare_program);
        output_compare_program_loaded[output_slot] = true;
    }

    if (!alarm_timer_program_loaded[alarm_slot]) {
        alarm_timer_program_offset[alarm_slot] =
            pio_add_program(config->alarm_timer_pio, &pio_alarm_timer_program);
        alarm_timer_program_loaded[alarm_slot] = true;
    }

    scheduler_init_output_compare_sm(scheduler);

    pio_alarm_timer_init(&scheduler->alarm_timer,
                         config->alarm_timer_pio,
                         config->alarm_timer_sm,
                         alarm_timer_program_offset[alarm_slot],
                         config->pps_pin,
                         (float) config->sm_clk_hz);

    pio_alarm_timer_set_rx_irq_callback(&scheduler->alarm_timer,
                                        scheduler_on_alarm_result,
                                        scheduler);

    ad9850_driver_config_t ad9850_cfg = {
        .spi = config->ad9850_spi,
        .spi_baud_hz = config->ad9850_spi_baud_hz,
        .sck_pin = config->ad9850_sck_pin,
        .mosi_pin = config->ad9850_mosi_pin,
        .use_fqud_pin = config->ad9850_use_fqud_pin,
        .fqud_pin = config->ad9850_fqud_pin,
        .fqud_pulse_us = config->ad9850_fqud_pulse_us,
        .use_reset_pin = config->ad9850_use_reset_pin,
        .reset_pin = config->ad9850_reset_pin,
        .dds_sysclk_hz = config->ad9850_sysclk_hz,
    };

    if (!ad9850_driver_init(&scheduler->ad9850, &ad9850_cfg) ||
        !ad9850_driver_serial_enable(&scheduler->ad9850)) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_AD9850);
        return false;
    }

    ad9850_frame_t idle_frame;
    if (!ad9850_driver_make_frame(0u, 0u, false, &idle_frame) ||
        !ad9850_driver_apply_frame_blocking(&scheduler->ad9850, &idle_frame, config->ad9850_use_fqud_pin)) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_AD9850);
        return false;
    }

    // Re-apply the PIO pin function if AD9850 setup temporarily switched it to SIO.
    scheduler_init_output_compare_sm(scheduler);

    scheduler_ensure_tx_irq_installed(config->output_compare_pio);
    scheduler_ensure_tx_irq_installed(config->alarm_timer_pio);
    tx_irq_registry[scheduler_pio_slot(config->output_compare_pio)] = scheduler;
    tx_irq_registry[scheduler_pio_slot(config->alarm_timer_pio)] = scheduler;
    scheduler_set_tx_irq_enabled(scheduler,
                                 config->output_compare_pio,
                                 config->output_compare_sm,
                                 false);
    scheduler_set_tx_irq_enabled(scheduler,
                                 config->alarm_timer_pio,
                                 config->alarm_timer_sm,
                                 false);

    scheduler->initialized = true;
    scheduler->state = SCHEDULER_STATE_IDLE;
    scheduler->last_error = SCHEDULER_ERROR_NONE;
    return true;
}

bool scheduler_prepare(scheduler_t *scheduler,
                       const scheduler_prepare_request_t *request)
{
    if (scheduler == NULL || !scheduler->initialized || request == NULL || request->dts == NULL) {
        return false;
    }

    if (scheduler->state != SCHEDULER_STATE_IDLE) {
        scheduler->last_error = SCHEDULER_ERROR_NOT_READY;
        return false;
    }

    scheduler->state = SCHEDULER_STATE_PREPARE_PRELOAD;
    scheduler->prepared = false;

    if (!scheduler_build_sequences(scheduler, request)) {
        scheduler_raise_fault(scheduler,
                              scheduler->last_error == SCHEDULER_ERROR_NONE ? SCHEDULER_ERROR_SEQUENCE_OVERFLOW
                                                                            : scheduler->last_error);
        return false;
    }

    scheduler_service_ad9850_nonblocking(scheduler);
    if (scheduler->state == SCHEDULER_STATE_END_FAULT) {
        return false;
    }

    scheduler->prepared = true;
    scheduler->state = SCHEDULER_STATE_IDLE;
    scheduler->last_error = SCHEDULER_ERROR_NONE;
    return true;
}

bool scheduler_arm(scheduler_t *scheduler)
{
    if (scheduler == NULL || !scheduler->initialized) {
        return false;
    }

    if (scheduler->state != SCHEDULER_STATE_IDLE || !scheduler->prepared) {
        scheduler->last_error = SCHEDULER_ERROR_NOT_READY;
        return false;
    }

    // Start each run from a clean output-compare state.
    scheduler_init_output_compare_sm(scheduler);

    // Start each run from a clean alarm-timer state as well.
    pio_sm_set_enabled(scheduler->cfg.alarm_timer_pio, scheduler->cfg.alarm_timer_sm, false);
    pio_sm_clear_fifos(scheduler->cfg.alarm_timer_pio, scheduler->cfg.alarm_timer_sm);
    pio_sm_restart(scheduler->cfg.alarm_timer_pio, scheduler->cfg.alarm_timer_sm);
    pio_sm_clkdiv_restart(scheduler->cfg.alarm_timer_pio, scheduler->cfg.alarm_timer_sm);
    pio_sm_set_enabled(scheduler->cfg.alarm_timer_pio, scheduler->cfg.alarm_timer_sm, true);

    if (!pio_alarm_timer_queue_rearm(&scheduler->alarm_timer)) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_ALARM_ENQUEUE);
        return false;
    }

    scheduler->next_alarm_index = 0u;
    scheduler->next_write_symbol = 0u;
    scheduler->state = SCHEDULER_STATE_ARM;
    scheduler->last_error = SCHEDULER_ERROR_NONE;

    scheduler_set_tx_irq_enabled(scheduler,
                                 scheduler->cfg.output_compare_pio,
                                 scheduler->cfg.output_compare_sm,
                                 true);
    scheduler_set_tx_irq_enabled(scheduler,
                                 scheduler->cfg.alarm_timer_pio,
                                 scheduler->cfg.alarm_timer_sm,
                                 true);
    feed_output_compare_fifo(scheduler);
    feed_alarm_fifo(scheduler);

    return true;
}

bool scheduler_reset(scheduler_t *scheduler)
{
    if (scheduler == NULL || !scheduler->initialized) {
        return false;
    }

    if (scheduler->state != SCHEDULER_STATE_END_FAULT && scheduler->state != SCHEDULER_STATE_END_OK) {
        scheduler->last_error = SCHEDULER_ERROR_NOT_READY;
        return false;
    }

    scheduler_set_tx_irq_enabled(scheduler,
                                 scheduler->cfg.output_compare_pio,
                                 scheduler->cfg.output_compare_sm,
                                 false);
    scheduler_set_tx_irq_enabled(scheduler,
                                 scheduler->cfg.alarm_timer_pio,
                                 scheduler->cfg.alarm_timer_sm,
                                 false);

    pio_sm_set_enabled(scheduler->cfg.output_compare_pio,
                       scheduler->cfg.output_compare_sm,
                       false);

    if (scheduler->cfg.ad9850_use_fqud_pin) {
        gpio_set_function(scheduler->cfg.ad9850_fqud_pin, GPIO_FUNC_SIO);
        gpio_set_dir(scheduler->cfg.ad9850_fqud_pin, GPIO_OUT);
        gpio_put(scheduler->cfg.ad9850_fqud_pin, 0);
    }

    if (!ad9850_driver_serial_enable(&scheduler->ad9850)) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_AD9850);
        return false;
    }

    ad9850_frame_t idle_frame;
    if (!ad9850_driver_make_frame(0u, 0u, false, &idle_frame) ||
        !ad9850_driver_apply_frame_blocking(&scheduler->ad9850,
                                            &idle_frame,
                                            true)) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_AD9850);
        return false;
    }

    scheduler->state = SCHEDULER_STATE_INIT;
    scheduler->prepared = false;
    scheduler->symbol_count = 0u;
    scheduler->next_alarm_index = 0u;
    scheduler->next_write_symbol = 0u;
    scheduler->rearm_ack_count = 0u;
    scheduler->alarm_fired_count = 0u;
    scheduler->output_feed_count = 0u;
    scheduler->alarm_feed_count = 0u;
    scheduler->output_stop_queued = false;
    sw_fifo_clear(&scheduler->output_fifo);
    sw_fifo_clear(&scheduler->alarm_fifo);

    scheduler_init_output_compare_sm(scheduler);

    scheduler->state = SCHEDULER_STATE_IDLE;
    scheduler->last_error = SCHEDULER_ERROR_NONE;
    return true;
}

void scheduler_set_fault_callback(scheduler_t *scheduler,
                                  scheduler_fault_callback_t callback,
                                  void *user_data)
{
    if (scheduler == NULL) {
        return;
    }

    scheduler->fault_callback = callback;
    scheduler->fault_user_data = user_data;
}

void __not_in_flash_func(scheduler_on_tx_fifo_not_full_irq)(scheduler_t *scheduler,
                                       uint sm)
{
    if (scheduler == NULL) {
        return;
    }

    if (sm == scheduler->cfg.output_compare_sm) {
        feed_output_compare_fifo(scheduler);
    }

    if (sm == scheduler->cfg.alarm_timer_sm) {
        feed_alarm_fifo(scheduler);
    }

    scheduler_service_ad9850_nonblocking(scheduler);
}

void __not_in_flash_func(scheduler_on_alarm_result)(const pio_alarm_timer_result_t *result,
                               void *user_data)
{
    scheduler_t *scheduler = (scheduler_t *) user_data;
    if (scheduler == NULL || result == NULL) {
        return;
    }

    if (result->kind == PIO_ALARM_TIMER_RESULT_KIND_LATE) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_TIMER_FAULT);
        return;
    }

    if (result->kind == PIO_ALARM_TIMER_RESULT_KIND_REARM_ACK) {
        scheduler->rearm_ack_count++;
        return;
    }

    if (scheduler->state != SCHEDULER_STATE_ARM) {
        return;
    }

    if (scheduler->next_alarm_index >= (scheduler->symbol_count + 1u)) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_UNEXPECTED_ALARM_TICK);
        return;
    }

    uint32_t expected_tick = scheduler->alarm_timer_sequence[scheduler->next_alarm_index];
    if (result->tick != expected_tick) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_UNEXPECTED_ALARM_TICK);
        return;
    }

    scheduler->next_alarm_index++;
    scheduler->alarm_fired_count++;

    if (scheduler->next_alarm_index == (scheduler->symbol_count + 1u)) {
        scheduler_set_tx_irq_enabled(scheduler,
                                     scheduler->cfg.output_compare_pio,
                                     scheduler->cfg.output_compare_sm,
                                     false);
        scheduler_set_tx_irq_enabled(scheduler,
                                     scheduler->cfg.alarm_timer_pio,
                                     scheduler->cfg.alarm_timer_sm,
                                     false);
        scheduler->state = SCHEDULER_STATE_END_OK;
        scheduler->last_error = SCHEDULER_ERROR_NONE;
        scheduler->prepared = false;
        scheduler_service_ad9850_nonblocking(scheduler);
        return;
    }

    if (scheduler->next_write_symbol >= scheduler->symbol_count) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_UNEXPECTED_ALARM_TICK);
        return;
    }

    if (ad9850_driver_is_nonblocking_busy(&scheduler->ad9850)) {
        scheduler_service_ad9850_nonblocking(scheduler);
        if (!ad9850_driver_is_nonblocking_busy(&scheduler->ad9850)) {
            bool success = false;
            if (ad9850_driver_take_nonblocking_result(&scheduler->ad9850, &success) && !success) {
                scheduler_raise_fault(scheduler, SCHEDULER_ERROR_AD9850);
                return;
            }
        }
    }

    if (ad9850_driver_is_nonblocking_busy(&scheduler->ad9850)) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_AD9850);
        return;
    }

    if (!ad9850_driver_start_apply_nonblocking(&scheduler->ad9850,
                                               &scheduler->prepared_frames[scheduler->next_write_symbol],
                                               false)) {
        scheduler_raise_fault(scheduler, SCHEDULER_ERROR_AD9850);
        return;
    }

    scheduler->next_write_symbol++;
}

scheduler_state_t scheduler_get_state(const scheduler_t *scheduler)
{
    if (scheduler == NULL) {
        return SCHEDULER_STATE_END_FAULT;
    }

    return scheduler->state;
}

scheduler_error_t scheduler_get_last_error(const scheduler_t *scheduler)
{
    if (scheduler == NULL) {
        return SCHEDULER_ERROR_INVALID_ARG;
    }

    return scheduler->last_error;
}
