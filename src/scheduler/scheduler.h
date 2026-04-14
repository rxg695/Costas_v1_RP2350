#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/ad9850_driver/ad9850_driver.h"
#include "driver/pio_alarm_timer/pio_alarm_timer.h"
#include "hardware/pio.h"
#include "hardware/spi.h"

/** Maximum number of symbols that can be prepared in one scheduler run. */
#define SCHEDULER_MAX_SYMBOLS 256u
/** Software FIFO depth used for staging output and alarm words. */
#define SCHEDULER_SW_FIFO_WORDS 1024u

/**
 * @brief Lifecycle state of one scheduler instance.
 */
typedef enum {
    SCHEDULER_STATE_INIT = 0,
    SCHEDULER_STATE_IDLE,
    SCHEDULER_STATE_PREPARE_PRELOAD,
    SCHEDULER_STATE_ARM,
    SCHEDULER_STATE_END_OK,
    SCHEDULER_STATE_END_FAULT,
} scheduler_state_t;

/**
 * @brief Last-error code reported by the scheduler.
 */
typedef enum {
    SCHEDULER_ERROR_NONE = 0,
    SCHEDULER_ERROR_INVALID_ARG,
    SCHEDULER_ERROR_NOT_READY,
    SCHEDULER_ERROR_SEQUENCE_OVERFLOW,
    SCHEDULER_ERROR_ALARM_ENQUEUE,
    SCHEDULER_ERROR_OUTPUT_ENQUEUE,
    SCHEDULER_ERROR_AD9850,
    SCHEDULER_ERROR_TIMER_FAULT,
    SCHEDULER_ERROR_UNEXPECTED_ALARM_TICK,
} scheduler_error_t;

/**
 * @brief Simple software FIFO used to stage 32-bit words.
 */
typedef struct {
    uint32_t words[SCHEDULER_SW_FIFO_WORDS];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
} scheduler_sw_fifo_t;

/**
 * @brief Static scheduler configuration.
 */
typedef struct {
    PIO output_compare_pio;
    PIO alarm_timer_pio;
    uint output_compare_sm;
    uint alarm_timer_sm;

    uint trigger_pin;
    uint output_pin;
    uint pps_pin;
    uint32_t sm_clk_hz;

    uint32_t output_pulse_ticks;

    spi_inst_t *ad9850_spi;
    uint32_t ad9850_spi_baud_hz;
    uint ad9850_sck_pin;
    uint ad9850_mosi_pin;
    bool ad9850_use_fqud_pin;
    uint ad9850_fqud_pin;
    uint32_t ad9850_fqud_pulse_us;
    bool ad9850_use_reset_pin;
    uint ad9850_reset_pin;
    uint32_t ad9850_sysclk_hz;
} scheduler_config_t;

/**
 * @brief Sequence description passed to @ref scheduler_prepare.
 */
typedef struct {
    uint32_t symbol_count;
    uint32_t dt0;
    const uint32_t *dts;
    int32_t load_offset;

    const ad9850_frame_t *ftw_frames;
    const uint32_t *freq_hz;
} scheduler_prepare_request_t;

/**
 * @brief Optional fault callback raised when the scheduler enters END_FAULT.
 */
typedef void (*scheduler_fault_callback_t)(scheduler_error_t error,
                                           void *user_data);

/**
 * @brief Full runtime state of one scheduler instance.
 */
typedef struct {
    scheduler_state_t state;
    scheduler_error_t last_error;

    bool initialized;
    bool prepared;

    scheduler_config_t cfg;

    pio_alarm_timer_t alarm_timer;
    ad9850_driver_t ad9850;

    scheduler_sw_fifo_t output_fifo;
    scheduler_sw_fifo_t alarm_fifo;

    uint32_t symbol_count;
    uint32_t alarm_count;
    uint32_t output_compare_sequence[SCHEDULER_MAX_SYMBOLS];
    uint32_t alarm_timer_sequence[SCHEDULER_MAX_SYMBOLS + 1u];
    ad9850_frame_t prepared_frames[SCHEDULER_MAX_SYMBOLS];

    uint32_t next_alarm_index;
    uint32_t next_write_symbol;

    uint32_t rearm_ack_count;
    uint32_t alarm_fired_count;
    uint32_t output_feed_count;
    uint32_t alarm_feed_count;
    bool output_stop_queued;

    scheduler_fault_callback_t fault_callback;
    void *fault_user_data;
} scheduler_t;

/**
 * @brief Initializes the scheduler and its embedded driver instances.
 */
bool scheduler_init(scheduler_t *scheduler,
                    const scheduler_config_t *config);

/**
 * @brief Builds the prepared timing sequences and DDS frames for one run.
 */
bool scheduler_prepare(scheduler_t *scheduler,
                       const scheduler_prepare_request_t *request);

/**
 * @brief Arms one prepared scheduler run.
 */
bool scheduler_arm(scheduler_t *scheduler);

/**
 * @brief Resets the scheduler after an END_OK or END_FAULT terminal state.
 */
bool scheduler_reset(scheduler_t *scheduler);

/**
 * @brief Registers an optional fault callback.
 */
void scheduler_set_fault_callback(scheduler_t *scheduler,
                                  scheduler_fault_callback_t callback,
                                  void *user_data);

/**
 * @brief TX FIFO feeder entry point for the shared PIO IRQ handler.
 */
void scheduler_on_tx_fifo_not_full_irq(scheduler_t *scheduler,
                                       uint sm);

/**
 * @brief Alarm-timer callback used by the scheduler.
 */
void scheduler_on_alarm_result(const pio_alarm_timer_result_t *result,
                               void *user_data);

/**
 * @brief Returns the current scheduler state.
 */
scheduler_state_t scheduler_get_state(const scheduler_t *scheduler);

/**
 * @brief Returns the last scheduler error.
 */
scheduler_error_t scheduler_get_last_error(const scheduler_t *scheduler);

#endif
