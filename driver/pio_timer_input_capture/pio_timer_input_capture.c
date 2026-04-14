#include "driver/pio_timer_input_capture/pio_timer_input_capture.h"

#include "hardware/clocks.h"
#include "pio_timer_input_capture.pio.h"

#define PIO_TIMER_INPUT_CAPTURE_PIO_COUNT 3u

static int pio_timer_input_capture_pio_index(PIO pio)
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

void pio_timer_input_capture_init(pio_timer_input_capture_t *capture,
                                  PIO pio,
                                  uint sm,
                                  uint start_pin,
                                  uint stop_pin,
                                  uint32_t sm_clk_hz,
                                  uint32_t timeout_ns)
{
    static bool program_loaded[PIO_TIMER_INPUT_CAPTURE_PIO_COUNT] = {false};
    static uint program_offset[PIO_TIMER_INPUT_CAPTURE_PIO_COUNT] = {0u};

    int pio_index = pio_timer_input_capture_pio_index(pio);
    if (capture == NULL || pio_index < 0) {
        return;
    }

    capture->pio = pio;
    capture->sm = sm;
    capture->start_pin = start_pin;
    capture->stop_pin = stop_pin;
    capture->sm_clk_hz = sm_clk_hz;
    capture->timeout_ns = timeout_ns;

    // The PIO loop consumes two state-machine cycles per decrement.
    uint64_t timeout_loops_64 = ((uint64_t) timeout_ns * sm_clk_hz) / 2000000000ull;
    capture->timeout_loops = (uint32_t) timeout_loops_64;

    if (!program_loaded[pio_index]) {
        program_offset[pio_index] = pio_add_program(pio, &pio_timer_input_capture_program);
        program_loaded[pio_index] = true;
    }
    capture->offset = program_offset[pio_index];

    gpio_init(start_pin);
    gpio_init(stop_pin);
    gpio_set_dir(start_pin, GPIO_IN);
    gpio_set_dir(stop_pin, GPIO_IN);
    pio_sm_set_consecutive_pindirs(pio, sm, start_pin, 1, false);
    pio_sm_set_consecutive_pindirs(pio, sm, stop_pin, 1, false);

    pio_sm_config config = pio_timer_input_capture_program_get_default_config(capture->offset);
    float clkdiv = (float) clock_get_hz(clk_sys) / (float) sm_clk_hz;
    sm_config_set_clkdiv(&config, clkdiv);

    sm_config_set_in_pins(&config, start_pin);

    sm_config_set_jmp_pin(&config, stop_pin);

    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_init(pio, sm, capture->offset, &config);
    pio_sm_put_blocking(pio, sm, capture->timeout_loops);
    pio_sm_set_enabled(pio, sm, true);
}

bool pio_timer_input_capture_poll(pio_timer_input_capture_t *capture,
                                  uint32_t *elapsed_ticks,
                                  bool *timed_out)
{
    if (pio_sm_is_rx_fifo_empty(capture->pio, capture->sm)) {
        return false;
    }

    uint32_t result = pio_sm_get(capture->pio, capture->sm);
    if (result == PIO_TIMER_INPUT_CAPTURE_TIMEOUT_SENTINEL) {
        *timed_out = true;
        *elapsed_ticks = 0;
    } else {
        *timed_out = false;
        *elapsed_ticks = capture->timeout_loops - result;
    }

    return true;
}

uint64_t pio_timer_input_capture_ticks_to_ns(const pio_timer_input_capture_t *capture,
                                             uint32_t ticks)
{
    return ((uint64_t) ticks * 2000000000ull) / capture->sm_clk_hz;
}
