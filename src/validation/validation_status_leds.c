#include "src/validation/validation_status_leds.h"

#include <stdbool.h>

#include "hardware/gpio.h"
#include "pico/time.h"

#include "src/validation/validation_config.h"

typedef struct {
    validation_status_led_config_t config;
    bool initialized;
    uint32_t heartbeat_phase;
    uint32_t heartbeat_ticks;
    uint32_t heartbeat_on_ticks;
    struct repeating_timer heartbeat_timer;
} validation_status_led_runtime_t;

static validation_status_led_runtime_t led_runtime;

static bool validation_status_led_pin_enabled(uint32_t pin)
{
    return pin != VALIDATION_STATUS_LED_DISABLED_PIN;
}

static void validation_status_led_write(uint32_t pin,
                                        bool enabled)
{
    if (!led_runtime.initialized || !validation_status_led_pin_enabled(pin)) {
        return;
    }

    gpio_put(pin, enabled ? 1u : 0u);
}

static bool validation_status_led_heartbeat_callback(struct repeating_timer *timer)
{
    (void) timer;

    if (!validation_status_led_pin_enabled(led_runtime.config.running_pin) ||
        led_runtime.heartbeat_ticks == 0u) {
        return true;
    }

    bool enabled = led_runtime.heartbeat_phase < led_runtime.heartbeat_on_ticks;
    validation_status_led_write(led_runtime.config.running_pin, enabled);

    led_runtime.heartbeat_phase++;
    if (led_runtime.heartbeat_phase >= led_runtime.heartbeat_ticks) {
        led_runtime.heartbeat_phase = 0u;
    }

    return true;
}

void validation_status_leds_init(const validation_status_led_config_t *config)
{
    if (config == NULL || led_runtime.initialized) {
        return;
    }

    led_runtime.config = *config;
    led_runtime.heartbeat_phase = 0u;
    led_runtime.heartbeat_ticks =
        VALIDATION_STATUS_LED_RUNNING_PERIOD_MS / VALIDATION_STATUS_LED_RUNNING_PULSE_MS;
    if (led_runtime.heartbeat_ticks == 0u) {
        led_runtime.heartbeat_ticks = 1u;
    }
    led_runtime.heartbeat_on_ticks = 1u;

    uint32_t pins[] = {
        led_runtime.config.running_pin,
        led_runtime.config.initialized_pin,
        led_runtime.config.prepared_pin,
        led_runtime.config.armed_pin,
        led_runtime.config.error_pin,
    };
    for (uint i = 0u; i < (sizeof(pins) / sizeof(pins[0])); ++i) {
        if (!validation_status_led_pin_enabled(pins[i])) {
            continue;
        }

        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_OUT);
        gpio_put(pins[i], 0u);
    }

    if (validation_status_led_pin_enabled(led_runtime.config.running_pin)) {
        add_repeating_timer_ms((int32_t) VALIDATION_STATUS_LED_RUNNING_PULSE_MS,
                               validation_status_led_heartbeat_callback,
                               NULL,
                               &led_runtime.heartbeat_timer);
    }

    led_runtime.initialized = true;
}

void validation_status_leds_update_scheduler(const scheduler_t *scheduler)
{
    if (!led_runtime.initialized) {
        return;
    }

    bool initialized = false;
    bool prepared = false;
    bool armed = false;
    bool error = false;

    if (scheduler != NULL) {
        initialized = scheduler->initialized;
        prepared = scheduler->prepared ||
                   scheduler->state == SCHEDULER_STATE_PREPARE_PRELOAD ||
                   scheduler->state == SCHEDULER_STATE_ARM;
        armed = scheduler->state == SCHEDULER_STATE_ARM;
        error = scheduler->last_error != SCHEDULER_ERROR_NONE ||
                scheduler->state == SCHEDULER_STATE_END_FAULT;
    }

    validation_status_led_write(led_runtime.config.initialized_pin, initialized);
    validation_status_led_write(led_runtime.config.prepared_pin, prepared);
    validation_status_led_write(led_runtime.config.armed_pin, armed);
    validation_status_led_write(led_runtime.config.error_pin, error);
}

void validation_status_leds_clear_scheduler(void)
{
    if (!led_runtime.initialized) {
        return;
    }

    validation_status_led_write(led_runtime.config.initialized_pin, false);
    validation_status_led_write(led_runtime.config.prepared_pin, false);
    validation_status_led_write(led_runtime.config.armed_pin, false);
    validation_status_led_write(led_runtime.config.error_pin, false);
}