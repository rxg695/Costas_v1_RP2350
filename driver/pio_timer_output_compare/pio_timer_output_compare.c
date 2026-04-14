#include "driver/pio_timer_output_compare/pio_timer_output_compare.h"

#include "hardware/clocks.h"
#include "pio_timer_output_compare.pio.h"

void pio_timer_output_compare_init(PIO pio,
                                   uint sm,
                                   uint offset,
                                   uint trigger_pin,
                                   uint output_pin,
                                   float sm_clk_hz,
                                   pio_timer_output_compare_mode_t mode)
{
    gpio_init(trigger_pin);
    gpio_set_dir(trigger_pin, GPIO_IN);

    pio_gpio_init(pio, output_pin);
    gpio_set_dir(output_pin, GPIO_OUT);
    pio_sm_set_consecutive_pindirs(pio, sm, output_pin, 1, true);

    pio_sm_config config = pio_timer_output_compare_program_get_default_config(offset);

    sm_config_set_set_pins(&config, output_pin, 1);

    // RX is unused, so join the FIFOs for extra TX depth.
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);

    sm_config_set_in_pins(&config, trigger_pin);

    float clkdiv = (float) clock_get_hz(clk_sys) / sm_clk_hz;
    sm_config_set_clkdiv(&config, clkdiv);

    pio_sm_init(pio, sm, offset, &config);
    pio_sm_set_pins_with_mask(pio, sm, 0u, 1u << output_pin);

    pio_sm_set_enabled(pio, sm, true);

    pio_sm_put_blocking(pio, sm, (uint32_t) mode);
}

void pio_timer_output_compare_arm(PIO pio,
                                  uint sm,
                                  uint32_t compare_ticks,
                                  uint32_t pulse_ticks)
{
    pio_timer_output_compare_queue_event(pio, sm, compare_ticks, pulse_ticks);
}

void pio_timer_output_compare_queue_event(PIO pio,
                                          uint sm,
                                          uint32_t compare_ticks,
                                          uint32_t pulse_ticks)
{
    pio_sm_put_blocking(pio, sm, compare_ticks);
    pio_sm_put_blocking(pio, sm, pulse_ticks);
}

bool pio_timer_output_compare_try_queue_event(PIO pio,
                                              uint sm,
                                              uint32_t compare_ticks,
                                              uint32_t pulse_ticks)
{
    uint tx_level = pio_sm_get_tx_fifo_level(pio, sm);
    if (tx_level > (PIO_TIMER_OUTPUT_COMPARE_TX_FIFO_WORDS - PIO_TIMER_OUTPUT_COMPARE_WORDS_PER_EVENT)) {
        return false;
    }

    pio_sm_put(pio, sm, compare_ticks);
    pio_sm_put(pio, sm, pulse_ticks);
    return true;
}

void pio_timer_output_compare_queue_stop(PIO pio,
                                         uint sm)
{
    pio_sm_put_blocking(pio, sm, PIO_TIMER_OUTPUT_COMPARE_STOP_COMPARE_TICKS);
    pio_sm_put_blocking(pio, sm, 0u);
}

bool pio_timer_output_compare_try_queue_stop(PIO pio,
                                             uint sm)
{
    uint tx_level = pio_sm_get_tx_fifo_level(pio, sm);
    if (tx_level > (PIO_TIMER_OUTPUT_COMPARE_TX_FIFO_WORDS - PIO_TIMER_OUTPUT_COMPARE_WORDS_PER_EVENT)) {
        return false;
    }

    pio_sm_put(pio, sm, PIO_TIMER_OUTPUT_COMPARE_STOP_COMPARE_TICKS);
    pio_sm_put(pio, sm, 0u);
    return true;
}

uint32_t pio_timer_output_compare_ns_to_ticks(uint32_t sm_clk_hz,
                                              uint64_t duration_ns)
{
    return (uint32_t) ((duration_ns * (uint64_t) sm_clk_hz) / 1000000000ull);
}
