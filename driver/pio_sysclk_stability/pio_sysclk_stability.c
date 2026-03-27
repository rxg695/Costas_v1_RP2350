#include "driver/pio_sysclk_stability/pio_sysclk_stability.h"

#include "hardware/clocks.h"
#include "pio_sysclk_stability.pio.h"

#define PIO_SYSCLK_STABILITY_PIO_COUNT 3u
#define PIO_SYSCLK_STABILITY_CYCLES_PER_TICK 2u

static int pio_sysclk_stability_pio_index(PIO pio)
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

void pio_sysclk_stability_init(pio_sysclk_stability_t *capture,
                               PIO pio,
                               uint sm,
                               uint pps_pin,
                               uint32_t sm_clk_hz,
                               uint32_t timeout_ns)
{
    static bool program_loaded[PIO_SYSCLK_STABILITY_PIO_COUNT] = {false};
    static uint program_offset[PIO_SYSCLK_STABILITY_PIO_COUNT] = {0u};

    int pio_index = pio_sysclk_stability_pio_index(pio);
    if (capture == NULL || pio_index < 0) {
        return;
    }

    capture->pio = pio;
    capture->sm = sm;
    capture->pps_pin = pps_pin;
    capture->configured_sys_clk_hz = clock_get_hz(clk_sys);
    capture->sm_clk_hz = sm_clk_hz;
    capture->timeout_ns = timeout_ns;

    uint64_t timeout_loops_64 = ((uint64_t) timeout_ns * sm_clk_hz) /
                                (1000000000ull * PIO_SYSCLK_STABILITY_CYCLES_PER_TICK);
    capture->timeout_loops = (uint32_t) timeout_loops_64;

    if (!program_loaded[pio_index]) {
        program_offset[pio_index] = pio_add_program(pio, &pio_sysclk_stability_program);
        program_loaded[pio_index] = true;
    }
    capture->offset = program_offset[pio_index];

    pio_gpio_init(pio, pps_pin);
    gpio_set_dir(pps_pin, GPIO_IN);
    pio_sm_set_consecutive_pindirs(pio, sm, pps_pin, 1, false);

    pio_sm_config config = pio_sysclk_stability_program_get_default_config(capture->offset);
    float clkdiv = (float) capture->configured_sys_clk_hz / (float) sm_clk_hz;
    sm_config_set_clkdiv(&config, clkdiv);
    sm_config_set_in_pins(&config, pps_pin);
    sm_config_set_jmp_pin(&config, pps_pin);

    pio_sm_set_enabled(pio, sm, false);
    pio_sm_clear_fifos(pio, sm);
    pio_sm_init(pio, sm, capture->offset, &config);
    pio_sm_put_blocking(pio, sm, capture->timeout_loops);
    pio_sm_set_enabled(pio, sm, true);
}

bool pio_sysclk_stability_poll(pio_sysclk_stability_t *capture,
                               uint32_t *elapsed_ticks,
                               bool *timed_out)
{
    if (pio_sm_is_rx_fifo_empty(capture->pio, capture->sm)) {
        return false;
    }

    uint32_t result = pio_sm_get(capture->pio, capture->sm);
    if (result == PIO_SYSCLK_STABILITY_TIMEOUT_SENTINEL) {
        *timed_out = true;
        *elapsed_ticks = 0;
    } else {
        *timed_out = false;
        *elapsed_ticks = capture->timeout_loops - result;
    }

    return true;
}

uint64_t pio_sysclk_stability_ticks_to_ns(const pio_sysclk_stability_t *capture,
                                          uint32_t ticks)
{
    return ((uint64_t) ticks * (1000000000ull * PIO_SYSCLK_STABILITY_CYCLES_PER_TICK)) /
           capture->sm_clk_hz;
}

int32_t pio_sysclk_stability_ticks_to_ppm(const pio_sysclk_stability_t *capture,
                                          uint32_t ticks)
{
    uint64_t measured_ns = pio_sysclk_stability_ticks_to_ns(capture, ticks);
    int64_t error_ns = (int64_t) measured_ns - 1000000000ll;
    return (int32_t) ((error_ns * 1000000ll) / 1000000000ll);
}

uint32_t pio_sysclk_stability_estimate_sysclk_hz(const pio_sysclk_stability_t *capture,
                                                 uint32_t ticks)
{
    uint64_t measured_ns = pio_sysclk_stability_ticks_to_ns(capture, ticks);
    return (uint32_t) (((uint64_t) capture->configured_sys_clk_hz * measured_ns) / 1000000000ull);
}