#include "driver/ad9850_driver/ad9850_driver.h"

#include "hardware/gpio.h"
#include "hardware/spi.h"

static uint8_t ad9850_reverse_bits(uint8_t value)
{
    value = (uint8_t) (((value & 0xF0u) >> 4) | ((value & 0x0Fu) << 4));
    value = (uint8_t) (((value & 0xCCu) >> 2) | ((value & 0x33u) << 2));
    value = (uint8_t) (((value & 0xAAu) >> 1) | ((value & 0x55u) << 1));
    return value;
}

static void ad9850_encode_wire_frame(const ad9850_frame_t *frame,
                                     uint8_t wire_bytes[5])
{
    for (uint i = 0u; i < 5u; ++i) {
        wire_bytes[i] = ad9850_reverse_bits(frame->bytes[i]);
    }
}

static void ad9850_manual_wclk_pulse(uint sck_pin)
{
    gpio_put(sck_pin, 0);
    sleep_us(1);
    gpio_put(sck_pin, 1);
    sleep_us(1);
    gpio_put(sck_pin, 0);
    sleep_us(1);
}

static bool ad9850_valid_phase(uint8_t phase)
{
    return phase <= 31u;
}

bool ad9850_driver_init(ad9850_driver_t *driver,
                        const ad9850_driver_config_t *config)
{
    if (driver == NULL || config == NULL || config->spi == NULL) {
        return false;
    }
    if (config->spi_baud_hz == 0u || config->dds_sysclk_hz == 0u) {
        return false;
    }

    (void) spi_init(config->spi, config->spi_baud_hz);
    spi_set_format(config->spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(config->sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->mosi_pin, GPIO_FUNC_SPI);

    if (config->use_fqud_pin) {
        gpio_init(config->fqud_pin);
        gpio_set_dir(config->fqud_pin, GPIO_OUT);
        gpio_put(config->fqud_pin, 0);
    }

    if (config->use_reset_pin) {
        gpio_init(config->reset_pin);
        gpio_set_dir(config->reset_pin, GPIO_OUT);
        gpio_put(config->reset_pin, 0);
    }

    driver->initialized = true;
    driver->serial_enabled = false;
    driver->spi = config->spi;
    driver->dds_sysclk_hz = config->dds_sysclk_hz;
    driver->sck_pin = config->sck_pin;
    driver->mosi_pin = config->mosi_pin;
    driver->use_fqud_pin = config->use_fqud_pin;
    driver->fqud_pin = config->fqud_pin;
    driver->fqud_pulse_us = config->fqud_pulse_us == 0u ? 1u : config->fqud_pulse_us;
    driver->use_reset_pin = config->use_reset_pin;
    driver->reset_pin = config->reset_pin;
    driver->tx_active = false;
    driver->tx_pending_pulse_fqud = false;
    driver->tx_last_success = false;
    driver->tx_result_ready = false;
    driver->tx_next_index = 0u;

    return true;
}

void ad9850_driver_deinit(ad9850_driver_t *driver)
{
    if (driver == NULL || !driver->initialized) {
        return;
    }

    spi_deinit(driver->spi);
    driver->initialized = false;
    driver->serial_enabled = false;
    driver->tx_active = false;
    driver->tx_pending_pulse_fqud = false;
    driver->tx_last_success = false;
    driver->tx_result_ready = false;
    driver->tx_next_index = 0u;
}

bool ad9850_driver_make_frame(uint32_t ftw,
                              uint8_t phase,
                              bool power_down,
                              ad9850_frame_t *frame_out)
{
    if (frame_out == NULL || !ad9850_valid_phase(phase)) {
        return false;
    }

    uint8_t control = (uint8_t) ((phase & 0x1Fu) << 3);
    if (power_down) {
        control |= 0x04u;
    }

    frame_out->bytes[0] = (uint8_t) (ftw & 0xFFu);
    frame_out->bytes[1] = (uint8_t) ((ftw >> 8) & 0xFFu);
    frame_out->bytes[2] = (uint8_t) ((ftw >> 16) & 0xFFu);
    frame_out->bytes[3] = (uint8_t) ((ftw >> 24) & 0xFFu);
    frame_out->bytes[4] = control;

    return true;
}

bool ad9850_driver_frequency_hz_to_ftw(const ad9850_driver_t *driver,
                                       uint32_t frequency_hz,
                                       uint32_t *ftw_out)
{
    if (driver == NULL || !driver->initialized || ftw_out == NULL) {
        return false;
    }
    if (driver->dds_sysclk_hz == 0u) {
        return false;
    }

    uint64_t numerator = ((uint64_t) frequency_hz) << 32;
    *ftw_out = (uint32_t) (numerator / (uint64_t) driver->dds_sysclk_hz);
    return true;
}

bool ad9850_driver_write_frame_blocking(const ad9850_driver_t *driver,
                                        const ad9850_frame_t *frame)
{
    if (driver == NULL || !driver->initialized || !driver->serial_enabled || frame == NULL) {
        return false;
    }

    uint8_t wire_bytes[5];
    ad9850_encode_wire_frame(frame, wire_bytes);

    int rc = spi_write_blocking(driver->spi, wire_bytes, 5);
    return rc == 5;
}

bool ad9850_driver_pulse_fqud(const ad9850_driver_t *driver)
{
    if (driver == NULL || !driver->initialized || !driver->use_fqud_pin) {
        return false;
    }

    gpio_put(driver->fqud_pin, 1);
    busy_wait_us_32(driver->fqud_pulse_us);
    gpio_put(driver->fqud_pin, 0);
    return true;
}

bool ad9850_driver_serial_enable(ad9850_driver_t *driver)
{
    if (driver == NULL || !driver->initialized) {
        return false;
    }
    if (!driver->use_fqud_pin) {
        return false;
    }

    if (driver->use_reset_pin) {
        if (!ad9850_driver_reset(driver)) {
            return false;
        }
    }

    // Latch the strapped startup word to enter serial mode.
    gpio_set_function(driver->sck_pin, GPIO_FUNC_SIO);
    gpio_set_dir(driver->sck_pin, GPIO_OUT);

    ad9850_manual_wclk_pulse(driver->sck_pin);
    if (!ad9850_driver_pulse_fqud(driver)) {
        gpio_set_function(driver->sck_pin, GPIO_FUNC_SPI);
        return false;
    }

    gpio_set_function(driver->sck_pin, GPIO_FUNC_SPI);

    driver->serial_enabled = true;
    return true;
}

bool ad9850_driver_apply_frame_blocking(const ad9850_driver_t *driver,
                                        const ad9850_frame_t *frame,
                                        bool pulse_fqud)
{
    if (!ad9850_driver_write_frame_blocking(driver, frame)) {
        return false;
    }

    if (!pulse_fqud) {
        return true;
    }

    return ad9850_driver_pulse_fqud(driver);
}

bool ad9850_driver_reset(ad9850_driver_t *driver)
{
    if (driver == NULL || !driver->initialized || !driver->use_reset_pin) {
        return false;
    }

    gpio_put(driver->reset_pin, 1);
    sleep_us(2);
    gpio_put(driver->reset_pin, 0);
    sleep_us(2);

    driver->serial_enabled = false;
    driver->tx_active = false;
    driver->tx_pending_pulse_fqud = false;
    driver->tx_result_ready = false;
    driver->tx_next_index = 0u;
    return true;
}

bool __not_in_flash_func(ad9850_driver_start_apply_nonblocking)(ad9850_driver_t *driver,
                                           const ad9850_frame_t *frame,
                                           bool pulse_fqud)
{
    if (driver == NULL || !driver->initialized || !driver->serial_enabled || frame == NULL) {
        return false;
    }

    if (driver->tx_active) {
        return false;
    }

    ad9850_encode_wire_frame(frame, driver->tx_frame_shadow.bytes);
    driver->tx_next_index = 0u;
    driver->tx_pending_pulse_fqud = pulse_fqud;
    driver->tx_active = true;
    driver->tx_last_success = false;
    driver->tx_result_ready = false;

    ad9850_driver_service_nonblocking(driver);
    return true;
}

void __not_in_flash_func(ad9850_driver_service_nonblocking)(ad9850_driver_t *driver)
{
    if (driver == NULL || !driver->initialized || !driver->tx_active) {
        return;
    }

    while (driver->tx_next_index < 5u && spi_is_writable(driver->spi)) {
        spi_get_hw(driver->spi)->dr = driver->tx_frame_shadow.bytes[driver->tx_next_index];
        driver->tx_next_index++;
    }

    if (driver->tx_next_index < 5u) {
        return;
    }

    if (spi_is_busy(driver->spi)) {
        return;
    }

    bool success = true;
    if (driver->tx_pending_pulse_fqud) {
        success = ad9850_driver_pulse_fqud(driver);
    }

    driver->tx_active = false;
    driver->tx_pending_pulse_fqud = false;
    driver->tx_last_success = success;
    driver->tx_result_ready = true;
}

bool __not_in_flash_func(ad9850_driver_is_nonblocking_busy)(const ad9850_driver_t *driver)
{
    if (driver == NULL || !driver->initialized) {
        return false;
    }

    return driver->tx_active;
}

bool __not_in_flash_func(ad9850_driver_take_nonblocking_result)(ad9850_driver_t *driver,
                                           bool *success_out)
{
    if (driver == NULL || !driver->initialized || success_out == NULL) {
        return false;
    }

    if (driver->tx_active || !driver->tx_result_ready) {
        return false;
    }

    *success_out = driver->tx_last_success;
    driver->tx_result_ready = false;
    driver->tx_last_success = false;
    return true;
}
