#ifndef PIO_ALARM_TIMER_H
#define PIO_ALARM_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#define PIO_ALARM_TIMER_CMD_REARM 0u
#define PIO_ALARM_TIMER_RESULT_LATE 0u
#define PIO_ALARM_TIMER_RESULT_REARM_ACK 0xFFFFFFFFu

typedef enum {
    PIO_ALARM_TIMER_ENQUEUE_OK = 0,
    PIO_ALARM_TIMER_ENQUEUE_ERR_NOT_INIT,
    PIO_ALARM_TIMER_ENQUEUE_ERR_ZERO_TICK,
    PIO_ALARM_TIMER_ENQUEUE_ERR_NON_MONOTONIC,
    PIO_ALARM_TIMER_ENQUEUE_ERR_TX_FULL,
} pio_alarm_timer_enqueue_status_t;

typedef enum {
    PIO_ALARM_TIMER_RESULT_KIND_REARM_ACK = 0,
    PIO_ALARM_TIMER_RESULT_KIND_LATE,
    PIO_ALARM_TIMER_RESULT_KIND_FIRED,
} pio_alarm_timer_result_kind_t;

typedef struct {
    pio_alarm_timer_result_kind_t kind;
    uint32_t tick;
} pio_alarm_timer_result_t;

typedef void (*pio_alarm_timer_rx_callback_t)(const pio_alarm_timer_result_t *result,
                                              void *user_data);

typedef struct {
    PIO pio;
    uint sm;
    bool initialized;
    bool has_last_alarm;
    uint32_t last_alarm_tick;
    bool rx_irq_enabled;
    pio_alarm_timer_rx_callback_t rx_callback;
    void *rx_user_data;
} pio_alarm_timer_t;

// Initializes one PIO state machine for the alarm timer program.
// `offset` must reference `pio_alarm_timer_program` loaded in this PIO block.
// `pps_pin` is sampled by WAIT PIN 0 and gates reset/rearm release.
void pio_alarm_timer_init(pio_alarm_timer_t *timer,
                          PIO pio,
                          uint sm,
                          uint offset,
                          uint pps_pin,
                          float sm_clk_hz);

// Queues rearm/reset command (0) if TX FIFO has room.
// Returns false when timer is not initialized or TX FIFO is full.
bool pio_alarm_timer_queue_rearm(pio_alarm_timer_t *timer);

// Queues an alarm tick command with monotonic guard.
// Guard behavior:
// - tick==0: rejected (ERR_ZERO_TICK), no enqueue
// - tick < last_alarm_tick: queue rearm command and return ERR_NON_MONOTONIC
// - otherwise: enqueue tick and return OK
// On successful enqueue, `last_alarm_tick` is updated.
pio_alarm_timer_enqueue_status_t pio_alarm_timer_queue_alarm(pio_alarm_timer_t *timer,
                                                             uint32_t alarm_tick);

// Reads one result from RX FIFO if available.
// Returns true when one word is returned via `result_out`.
bool pio_alarm_timer_try_read_result(pio_alarm_timer_t *timer,
                                     uint32_t *result_out);

// Decodes one raw result word into typed result data.
void pio_alarm_timer_decode_result(uint32_t raw_result,
                                   pio_alarm_timer_result_t *decoded_out);

// Reads and decodes one result from RX FIFO if available.
bool pio_alarm_timer_try_read_decoded_result(pio_alarm_timer_t *timer,
                                             pio_alarm_timer_result_t *decoded_out);

// Enables RX-not-empty IRQ callback dispatch for this timer instance.
void pio_alarm_timer_set_rx_irq_callback(pio_alarm_timer_t *timer,
                                         pio_alarm_timer_rx_callback_t callback,
                                         void *user_data);

// Disables RX-not-empty IRQ callback dispatch for this timer instance.
void pio_alarm_timer_clear_rx_irq_callback(pio_alarm_timer_t *timer);

#endif
