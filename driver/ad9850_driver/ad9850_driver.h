#ifndef AD9850_DRIVER_H
#define AD9850_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"
#include "pico/stdlib.h"

/**
 * @brief AD9850 serial frame in transmit order.
 *
 * The logical frame order is the four FTW bytes least-significant first,
 * followed by the control byte. The driver bit-reverses each byte at transmit
 * time because the RP-series SPI block only supports MSB-first shifting while
 * the AD9850 serial input expects LSB-first bit order.
 */
typedef struct {
    uint8_t bytes[5];
} ad9850_frame_t;

/**
 * @brief Static configuration for one AD9850 driver instance.
 */
typedef struct {
    /** SPI peripheral instance, typically @c spi0 or @c spi1. */
    spi_inst_t *spi;
    /** SPI bit rate used for 40-bit frame transfers. */
    uint32_t spi_baud_hz;

    /** SPI clock pin used for normal transfers and manual W_CLK pulsing. */
    uint sck_pin;
    /** SPI MOSI pin used to shift frame data into the DDS. */
    uint mosi_pin;

    /** Enables use of the FQ_UD latch pin. */
    bool use_fqud_pin;
    /** GPIO used for FQ_UD when enabled. */
    uint fqud_pin;
    /** High time of the FQ_UD pulse in microseconds. */
    uint32_t fqud_pulse_us;

    /** Enables use of the hardware reset pin. */
    bool use_reset_pin;
    /** GPIO used for RESET when enabled. */
    uint reset_pin;

    /** DDS system clock in hertz, used by frequency-to-FTW conversion. */
    uint32_t dds_sysclk_hz;
} ad9850_driver_config_t;

/**
 * @brief Runtime state for one initialized AD9850 driver.
 */
typedef struct {
    /** True after successful initialization. */
    bool initialized;
    /** True after the serial-enable sequence has completed successfully. */
    bool serial_enabled;
    spi_inst_t *spi;
    uint32_t dds_sysclk_hz;

    /** SCK pin reused for the manual W_CLK pulse during serial-enable. */
    uint sck_pin;
    /** MOSI pin reused to drive startup serial bits during serial-enable. */
    uint mosi_pin;

    bool use_fqud_pin;
    uint fqud_pin;
    uint32_t fqud_pulse_us;

    bool use_reset_pin;
    uint reset_pin;

    /** Non-blocking transfer state. */
    bool tx_active;
    bool tx_pending_pulse_fqud;
    bool tx_last_success;
    bool tx_result_ready;
    uint8_t tx_next_index;
    ad9850_frame_t tx_frame_shadow;
} ad9850_driver_t;

/**
 * @brief Initializes SPI and optional control pins.
 *
 * The write path remains locked until @ref ad9850_driver_serial_enable is run.
 *
 * @return true on success, false on invalid configuration.
 */
bool ad9850_driver_init(ad9850_driver_t *driver,
                        const ad9850_driver_config_t *config);

/**
 * @brief Deinitializes the SPI peripheral used by this driver instance.
 */
void ad9850_driver_deinit(ad9850_driver_t *driver);

/**
 * @brief Builds a frame from FTW and control fields.
 *
 * @param ftw 32-bit frequency tuning word.
 * @param phase 5-bit phase value in the range [0, 31].
 * @param power_down When true, sets the AD9850 power-down bit.
 * @param frame_out Output frame buffer.
 * @return true on success, false if arguments are invalid.
 */
bool ad9850_driver_make_frame(uint32_t ftw,
                              uint8_t phase,
                              bool power_down,
                              ad9850_frame_t *frame_out);

/**
 * @brief Converts a frequency in hertz to an FTW.
 *
 * Uses the configured DDS system clock and integer floor division.
 */
bool ad9850_driver_frequency_hz_to_ftw(const ad9850_driver_t *driver,
                                       uint32_t frequency_hz,
                                       uint32_t *ftw_out);

/**
 * @brief Writes one frame over SPI.
 *
 * Fails until the serial-enable sequence has completed.
 */
bool ad9850_driver_write_frame_blocking(const ad9850_driver_t *driver,
                                        const ad9850_frame_t *frame);

/**
 * @brief Pulses FQ_UD when that pin is enabled.
 */
bool ad9850_driver_pulse_fqud(const ad9850_driver_t *driver);

/**
 * @brief Runs the AD9850 serial-enable sequence.
 *
 * This performs RESET, manually latches the strapped parallel startup word to
 * enter serial mode, then unlocks the write path.
 */
bool ad9850_driver_serial_enable(ad9850_driver_t *driver);

/**
 * @brief Writes a frame and optionally pulses FQ_UD.
 */
bool ad9850_driver_apply_frame_blocking(const ad9850_driver_t *driver,
                                        const ad9850_frame_t *frame,
                                        bool pulse_fqud);

/**
 * @brief Starts a non-blocking write or write-and-latch operation.
 *
 * The caller must continue the transfer with
 * @ref ad9850_driver_service_nonblocking.
 */
bool ad9850_driver_start_apply_nonblocking(ad9850_driver_t *driver,
                                           const ad9850_frame_t *frame,
                                           bool pulse_fqud);

/**
 * @brief Advances the active non-blocking transfer.
 *
 * Safe to call from an IRQ-driven code path.
 */
void ad9850_driver_service_nonblocking(ad9850_driver_t *driver);

/**
 * @brief Reports whether a non-blocking transfer is active.
 */
bool ad9850_driver_is_nonblocking_busy(const ad9850_driver_t *driver);

/**
 * @brief Retrieves the completion result of the most recent non-blocking transfer.
 *
 * @return true when a new completion result was returned, false otherwise.
 */
bool ad9850_driver_take_nonblocking_result(ad9850_driver_t *driver,
                                           bool *success_out);

/**
 * @brief Pulses RESET when available and clears the serial-enabled state.
 */
bool ad9850_driver_reset(ad9850_driver_t *driver);

#endif
