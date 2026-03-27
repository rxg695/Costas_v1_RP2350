#include "driver/pio_alarm_timer/pio_alarm_timer.h"

#include "hardware/clocks.h"
#include "hardware/irq.h"
#include "pio_alarm_timer.pio.h"

#define PIO_ALARM_TIMER_PIO_COUNT 3u

// One registry slot per PIO block and state machine.
static pio_alarm_timer_t *rx_irq_registry[PIO_ALARM_TIMER_PIO_COUNT][4] = {0};
static bool rx_irq_handler_installed[PIO_ALARM_TIMER_PIO_COUNT] = {false};

static int pio_alarm_timer_pio_index(PIO pio)
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

static enum pio_interrupt_source pio_alarm_timer_rx_source_for_sm(uint sm)
{
    switch (sm) {
    case 0:
        return pis_sm0_rx_fifo_not_empty;
    case 1:
        return pis_sm1_rx_fifo_not_empty;
    case 2:
        return pis_sm2_rx_fifo_not_empty;
    default:
        return pis_sm3_rx_fifo_not_empty;
    }
}

static void __not_in_flash_func(pio_alarm_timer_decode_result_raw)(uint32_t raw_result,
                                              pio_alarm_timer_result_t *decoded_out)
{
    if (raw_result == PIO_ALARM_TIMER_RESULT_REARM_ACK) {
        decoded_out->kind = PIO_ALARM_TIMER_RESULT_KIND_REARM_ACK;
        decoded_out->tick = 0u;
        return;
    }

    if (raw_result == PIO_ALARM_TIMER_RESULT_LATE) {
        decoded_out->kind = PIO_ALARM_TIMER_RESULT_KIND_LATE;
        decoded_out->tick = 0u;
        return;
    }

    decoded_out->kind = PIO_ALARM_TIMER_RESULT_KIND_FIRED;
    decoded_out->tick = raw_result;
}

static void __not_in_flash_func(pio_alarm_timer_irq0_dispatch)(PIO pio)
{
    int pio_index = pio_alarm_timer_pio_index(pio);
    if (pio_index < 0) {
        return;
    }

    for (uint sm = 0; sm < 4; ++sm) {
        pio_alarm_timer_t *timer = rx_irq_registry[pio_index][sm];
        if (timer == NULL || timer->rx_callback == NULL) {
            continue;
        }

        while (!pio_sm_is_rx_fifo_empty(pio, sm)) {
            uint32_t raw = pio_sm_get(pio, sm);
            pio_alarm_timer_result_t result;
            pio_alarm_timer_decode_result_raw(raw, &result);
            timer->rx_callback(&result, timer->rx_user_data);
        }
    }
}

static void __not_in_flash_func(pio0_irq0_handler)(void)
{
    pio_alarm_timer_irq0_dispatch(pio0);
}

static void __not_in_flash_func(pio1_irq0_handler)(void)
{
    pio_alarm_timer_irq0_dispatch(pio1);
}

static void __not_in_flash_func(pio2_irq0_handler)(void)
{
    pio_alarm_timer_irq0_dispatch(pio2);
}

static void pio_alarm_timer_ensure_irq_handler_installed(PIO pio)
{
    int pio_index = pio_alarm_timer_pio_index(pio);
    if (pio_index < 0 || rx_irq_handler_installed[pio_index]) {
        return;
    }

    if (pio_index == 0) {
        irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq0_handler);
        irq_set_priority(PIO0_IRQ_0, 0);
        irq_set_enabled(PIO0_IRQ_0, true);
    } else if (pio_index == 1) {
        irq_set_exclusive_handler(PIO1_IRQ_0, pio1_irq0_handler);
        irq_set_priority(PIO1_IRQ_0, 0);
        irq_set_enabled(PIO1_IRQ_0, true);
    } else if (pio_index == 2) {
        irq_set_exclusive_handler(PIO2_IRQ_0, pio2_irq0_handler);
        irq_set_priority(PIO2_IRQ_0, 0);
        irq_set_enabled(PIO2_IRQ_0, true);
    } else {
        return;
    }

    rx_irq_handler_installed[pio_index] = true;
}

static bool pio_alarm_timer_try_put(PIO pio,
                                    uint sm,
                                    uint32_t value)
{
    if (pio_sm_is_tx_fifo_full(pio, sm)) {
        return false;
    }

    pio_sm_put(pio, sm, value);
    return true;
}

void pio_alarm_timer_init(pio_alarm_timer_t *timer,
                          PIO pio,
                          uint sm,
                          uint offset,
                          uint pps_pin,
                          float sm_clk_hz)
{
    pio_sm_config config = pio_alarm_timer_program_get_default_config(offset);

    pio_gpio_init(pio, pps_pin);
    gpio_set_dir(pps_pin, GPIO_IN);
    sm_config_set_in_pins(&config, pps_pin);

    float clkdiv = (float) clock_get_hz(clk_sys) / sm_clk_hz;
    sm_config_set_clkdiv(&config, clkdiv);

    pio_sm_init(pio, sm, offset, &config);
    pio_sm_set_enabled(pio, sm, true);

    timer->pio = pio;
    timer->sm = sm;
    timer->initialized = true;
    timer->has_last_alarm = false;
    timer->last_alarm_tick = 0u;
    timer->rx_irq_enabled = false;
    timer->rx_callback = NULL;
    timer->rx_user_data = NULL;
}

bool pio_alarm_timer_queue_rearm(pio_alarm_timer_t *timer)
{
    if (timer == NULL || !timer->initialized) {
        return false;
    }

    bool queued = pio_alarm_timer_try_put(timer->pio, timer->sm, PIO_ALARM_TIMER_CMD_REARM);
    if (queued) {
        timer->has_last_alarm = false;
        timer->last_alarm_tick = 0u;
    }

    return queued;
}

pio_alarm_timer_enqueue_status_t pio_alarm_timer_queue_alarm(pio_alarm_timer_t *timer,
                                                             uint32_t alarm_tick)
{
    if (timer == NULL || !timer->initialized) {
        return PIO_ALARM_TIMER_ENQUEUE_ERR_NOT_INIT;
    }

    if (alarm_tick == 0u) {
        return PIO_ALARM_TIMER_ENQUEUE_ERR_ZERO_TICK;
    }

    if (timer->has_last_alarm && alarm_tick < timer->last_alarm_tick) {
        // Re-arm before reporting the monotonicity error so host and PIO state stay aligned.
        (void) pio_alarm_timer_queue_rearm(timer);
        return PIO_ALARM_TIMER_ENQUEUE_ERR_NON_MONOTONIC;
    }

    if (!pio_alarm_timer_try_put(timer->pio, timer->sm, alarm_tick)) {
        return PIO_ALARM_TIMER_ENQUEUE_ERR_TX_FULL;
    }

    timer->has_last_alarm = true;
    timer->last_alarm_tick = alarm_tick;
    return PIO_ALARM_TIMER_ENQUEUE_OK;
}

bool pio_alarm_timer_try_read_result(pio_alarm_timer_t *timer,
                                     uint32_t *result_out)
{
    if (timer == NULL || result_out == NULL || !timer->initialized) {
        return false;
    }

    if (pio_sm_is_rx_fifo_empty(timer->pio, timer->sm)) {
        return false;
    }

    *result_out = pio_sm_get(timer->pio, timer->sm);
    return true;
}

void pio_alarm_timer_decode_result(uint32_t raw_result,
                                   pio_alarm_timer_result_t *decoded_out)
{
    if (decoded_out == NULL) {
        return;
    }

    pio_alarm_timer_decode_result_raw(raw_result, decoded_out);
}

bool pio_alarm_timer_try_read_decoded_result(pio_alarm_timer_t *timer,
                                             pio_alarm_timer_result_t *decoded_out)
{
    uint32_t raw = 0u;
    if (!pio_alarm_timer_try_read_result(timer, &raw)) {
        return false;
    }

    pio_alarm_timer_decode_result(raw, decoded_out);
    return true;
}

void pio_alarm_timer_set_rx_irq_callback(pio_alarm_timer_t *timer,
                                         pio_alarm_timer_rx_callback_t callback,
                                         void *user_data)
{
    if (timer == NULL || !timer->initialized || callback == NULL) {
        return;
    }

    pio_alarm_timer_ensure_irq_handler_installed(timer->pio);

    timer->rx_callback = callback;
    timer->rx_user_data = user_data;
    timer->rx_irq_enabled = true;

    int pio_index = pio_alarm_timer_pio_index(timer->pio);
    if (pio_index < 0) {
        return;
    }
    rx_irq_registry[pio_index][timer->sm] = timer;

    pio_set_irq0_source_enabled(timer->pio,
                                pio_alarm_timer_rx_source_for_sm(timer->sm),
                                true);
}

void pio_alarm_timer_clear_rx_irq_callback(pio_alarm_timer_t *timer)
{
    if (timer == NULL || !timer->initialized || !timer->rx_irq_enabled) {
        return;
    }

    pio_set_irq0_source_enabled(timer->pio,
                                pio_alarm_timer_rx_source_for_sm(timer->sm),
                                false);

    int pio_index = pio_alarm_timer_pio_index(timer->pio);
    if (pio_index >= 0) {
        rx_irq_registry[pio_index][timer->sm] = NULL;
    }

    timer->rx_irq_enabled = false;
    timer->rx_callback = NULL;
    timer->rx_user_data = NULL;
}
