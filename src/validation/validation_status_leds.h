#ifndef VALIDATION_STATUS_LEDS_H
#define VALIDATION_STATUS_LEDS_H

#include <stdint.h>

#include "src/scheduler/scheduler.h"

typedef struct {
    uint32_t running_pin;
    uint32_t initialized_pin;
    uint32_t prepared_pin;
    uint32_t armed_pin;
    uint32_t error_pin;
} validation_status_led_config_t;

void validation_status_leds_init(const validation_status_led_config_t *config);

void validation_status_leds_update_scheduler(const scheduler_t *scheduler);

void validation_status_leds_clear_scheduler(void);

#endif