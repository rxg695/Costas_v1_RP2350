#include <stdio.h>

#include "driver/ad9850_driver/ad9850_driver.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "src/validation/validation_config.h"
#include "src/validation/ad9850_validation.h"

static uint32_t ftw_from_freq(uint32_t frequency_hz,
                              uint32_t dds_sysclk_hz)
{
    uint64_t numerator = ((uint64_t) frequency_hz) << VALIDATION_AD9850_FTW_WIDTH_BITS;
    return (uint32_t) (numerator / (uint64_t) dds_sysclk_hz);
}

static uint8_t reverse_bits(uint8_t value)
{
    value = (uint8_t) (((value & VALIDATION_AD9850_REVERSE_MASK_A) >> VALIDATION_AD9850_REVERSE_SHIFT_NIBBLE) |
                       ((value & VALIDATION_AD9850_REVERSE_MASK_B) << VALIDATION_AD9850_REVERSE_SHIFT_NIBBLE));
    value = (uint8_t) (((value & VALIDATION_AD9850_REVERSE_MASK_C) >> VALIDATION_AD9850_REVERSE_SHIFT_PAIR) |
                       ((value & VALIDATION_AD9850_REVERSE_MASK_D) << VALIDATION_AD9850_REVERSE_SHIFT_PAIR));
    value = (uint8_t) (((value & VALIDATION_AD9850_REVERSE_MASK_E) >> VALIDATION_AD9850_REVERSE_SHIFT_BIT) |
                       ((value & VALIDATION_AD9850_REVERSE_MASK_F) << VALIDATION_AD9850_REVERSE_SHIFT_BIT));
    return value;
}

static void print_bytes_decimal_line(const char *label,
                                     const uint8_t *bytes,
                                     size_t count)
{
    printf("  %s:", label);
    for (size_t index = 0; index < count; ++index) {
        printf(" b%u=%3u", (unsigned) index, (unsigned) bytes[index]);
    }
    printf("\n");
}

static void print_bytes_hex_line(const char *label,
                                 const uint8_t *bytes,
                                 size_t count)
{
    printf("  %s:", label);
    for (size_t index = 0; index < count; ++index) {
        printf(" b%u=0x%02X", (unsigned) index, (unsigned) bytes[index]);
    }
    printf("\n");
}

static void print_byte_binary(uint8_t value)
{
    for (int bit = VALIDATION_AD9850_BINARY_MSB_INDEX; bit >= 0; --bit) {
        putchar((value & (1u << bit)) ? '1' : '0');
    }
}

static void print_bytes_binary_line(const char *label,
                                    const uint8_t *bytes,
                                    size_t count)
{
    printf("  %s:", label);
    for (size_t index = 0; index < count; ++index) {
        printf(" b%u=", (unsigned) index);
        print_byte_binary(bytes[index]);
    }
    printf("\n");
}

static void print_ftw_views(uint32_t ftw)
{
    uint8_t ftw_bytes[VALIDATION_AD9850_FTW_BYTE_COUNT] = {
        (uint8_t) (ftw & VALIDATION_AD9850_BYTE_MASK),
        (uint8_t) ((ftw >> VALIDATION_AD9850_SHIFT_8) & VALIDATION_AD9850_BYTE_MASK),
        (uint8_t) ((ftw >> VALIDATION_AD9850_SHIFT_16) & VALIDATION_AD9850_BYTE_MASK),
        (uint8_t) ((ftw >> VALIDATION_AD9850_SHIFT_24) & VALIDATION_AD9850_BYTE_MASK),
    };
    uint8_t wire_ftw_bytes[VALIDATION_AD9850_FTW_BYTE_COUNT];

    for (size_t index = 0; index < VALIDATION_AD9850_FTW_BYTE_COUNT; ++index) {
        wire_ftw_bytes[index] = reverse_bits(ftw_bytes[index]);
    }

    printf("  FTW bytes (logical LSB->MSB):\n");
    print_bytes_decimal_line("dec", ftw_bytes, VALIDATION_AD9850_FTW_BYTE_COUNT);
    print_bytes_hex_line("hex", ftw_bytes, VALIDATION_AD9850_FTW_BYTE_COUNT);
    print_bytes_binary_line("bin", ftw_bytes, VALIDATION_AD9850_FTW_BYTE_COUNT);

    printf("  FTW bytes (on-wire after per-byte bit reversal):\n");
    print_bytes_decimal_line("dec", wire_ftw_bytes, VALIDATION_AD9850_FTW_BYTE_COUNT);
    print_bytes_hex_line("hex", wire_ftw_bytes, VALIDATION_AD9850_FTW_BYTE_COUNT);
    print_bytes_binary_line("bin", wire_ftw_bytes, VALIDATION_AD9850_FTW_BYTE_COUNT);
}

static void print_frame_views(const ad9850_frame_t *frame)
{
    if (frame == NULL) {
        return;
    }

    uint8_t wire_bytes[VALIDATION_AD9850_FRAME_BYTE_COUNT];

    for (size_t index = 0; index < VALIDATION_AD9850_FRAME_BYTE_COUNT; ++index) {
        wire_bytes[index] = reverse_bits(frame->bytes[index]);
    }

    printf("  Frame bytes (logical FTW-first):\n");
    print_bytes_decimal_line("dec", frame->bytes, VALIDATION_AD9850_FRAME_BYTE_COUNT);
    print_bytes_hex_line("hex", frame->bytes, VALIDATION_AD9850_FRAME_BYTE_COUNT);
    print_bytes_binary_line("bin", frame->bytes, VALIDATION_AD9850_FRAME_BYTE_COUNT);

    printf("  Frame bytes (on-wire SPI bytes):\n");
    print_bytes_decimal_line("dec", wire_bytes, VALIDATION_AD9850_FRAME_BYTE_COUNT);
    print_bytes_hex_line("hex", wire_bytes, VALIDATION_AD9850_FRAME_BYTE_COUNT);
    print_bytes_binary_line("bin", wire_bytes, VALIDATION_AD9850_FRAME_BYTE_COUNT);
}

static void print_status(uint32_t frequency_hz,
                         uint32_t step_hz,
                         uint8_t phase,
                         bool power_down,
                         uint32_t ftw,
                const ad9850_frame_t *frame,
                bool serial_enabled,
                bool nb_busy)
{
    printf("Status:\n");
    printf("  Frequency: %lu Hz   Step: %lu Hz   Phase: %u\n",
        (unsigned long) frequency_hz,
        (unsigned long) step_hz,
        (unsigned) phase);
    printf("  Power-down: %s   Serial enabled: %s   Non-blocking busy: %s\n",
        power_down ? "yes" : "no",
        serial_enabled ? "yes" : "no",
        nb_busy ? "yes" : "no");
    printf("  FTW: 0x%08lx\n", (unsigned long) ftw);
    print_ftw_views(ftw);

    if (frame != NULL) {
        print_frame_views(frame);
    }
}

void ad9850_validation_run(const ad9850_validation_config_t *config)
{
    if (config == NULL) {
        printf("ad9850_validation: null config\n");
        return;
    }

    uint32_t dds_sysclk_hz = config->dds_sysclk_hz == 0u ? VALIDATION_AD9850_DEFAULT_DDS_SYSCLK_HZ : config->dds_sysclk_hz;
    uint32_t spi_baud_hz = config->spi_baud_hz == 0u ? VALIDATION_AD9850_FALLBACK_SPI_BAUD_HZ : config->spi_baud_hz;
    uint8_t phase = (uint8_t) (config->phase & VALIDATION_AD9850_PHASE_MASK);
    uint32_t frequency_hz = config->frequency_hz == 0u ? VALIDATION_AD9850_DEFAULT_FREQUENCY_HZ : config->frequency_hz;
    uint32_t step_hz = VALIDATION_AD9850_DEFAULT_STEP_HZ;
    bool power_down = config->power_down;

    spi_inst_t *spi = config->spi_index == VALIDATION_AD9850_SPI_INDEX_1 ? spi1 : spi0;

    ad9850_driver_t driver;
    ad9850_driver_config_t driver_cfg = {
        .spi = spi,
        .spi_baud_hz = spi_baud_hz,
        .sck_pin = config->sck_pin,
        .mosi_pin = config->mosi_pin,
        .use_fqud_pin = config->use_fqud_pin,
        .fqud_pin = config->fqud_pin,
        .fqud_pulse_us = config->fqud_pulse_us,
        .use_reset_pin = config->use_reset_pin,
        .reset_pin = config->reset_pin,
        .dds_sysclk_hz = dds_sysclk_hz,
    };

    if (!ad9850_driver_init(&driver, &driver_cfg)) {
        printf("AD9850 initialization failed.\n");
        return;
    }

    uint32_t ftw = ftw_from_freq(frequency_hz, dds_sysclk_hz);
    ad9850_frame_t frame;
    bool frame_ok = ad9850_driver_make_frame(ftw, phase, power_down, &frame);
    uint32_t nb_start_ok_count = 0u;
    uint32_t nb_start_busy_count = 0u;
    uint32_t nb_start_fail_count = 0u;
    uint32_t nb_complete_ok_count = 0u;
    uint32_t nb_complete_fail_count = 0u;

    printf("\nAD9850 validation active\n");
        printf("SPI=%s baud=%lu Hz SCK=GP%u MOSI=GP%u FQ_UD=%s(GP%u, %lu us) RESET=%s(GP%u) sysclk=%lu Hz\n",
           spi == spi0 ? "spi0" : "spi1",
           (unsigned long) spi_baud_hz,
           config->sck_pin,
           config->mosi_pin,
           config->use_fqud_pin ? "on" : "off",
           config->fqud_pin,
            (unsigned long) (config->fqud_pulse_us == 0u ? VALIDATION_AD9850_DEFAULT_FQUD_PULSE_US : config->fqud_pulse_us),
           config->use_reset_pin ? "on" : "off",
           config->reset_pin,
           (unsigned long) dds_sysclk_hz);
    printf("Commands: e=serial enable, a=write+latch, w=write only, p=pulse FQ_UD, r=reset\n");
    printf("          n=start non-blocking write, m=start non-blocking write+latch, v=service once\n");
    printf("          b=show busy flag, x=show non-blocking counters, +=freq up, -=freq down\n");
    printf("          d=toggle power-down, s=show status, q=return\n");

    if (!frame_ok) {
        printf("Initial frame build failed.\n");
    } else {
        print_status(frequency_hz,
                     step_hz,
                     phase,
                     power_down,
                     ftw,
                     &frame,
                     driver.serial_enabled,
                     ad9850_driver_is_nonblocking_busy(&driver));
    }

    while (true) {
        ad9850_driver_service_nonblocking(&driver);
        bool nb_success = false;
        if (ad9850_driver_take_nonblocking_result(&driver, &nb_success)) {
            if (nb_success) {
                nb_complete_ok_count++;
            } else {
                nb_complete_fail_count++;
            }
            printf("Non-blocking transfer completed: %s\n", nb_success ? "ok" : "failed");
        }

        int ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }

        if (ch == 'a' || ch == 'A') {
            bool ok = frame_ok && ad9850_driver_apply_frame_blocking(&driver, &frame, config->use_fqud_pin);
            printf("Write + latch: %s\n", ok ? "ok" : "failed");
        } else if (ch == 'w' || ch == 'W') {
            bool ok = frame_ok && ad9850_driver_write_frame_blocking(&driver, &frame);
            printf("Write only: %s\n", ok ? "ok" : "failed");
        } else if (ch == 'p' || ch == 'P') {
            bool ok = ad9850_driver_pulse_fqud(&driver);
            printf("Pulse FQ_UD: %s\n", ok ? "ok" : "failed_or_disabled");
        } else if (ch == 'r' || ch == 'R') {
            bool ok = ad9850_driver_reset(&driver);
            printf("Reset: %s\n", ok ? "ok" : "failed_or_disabled");
        } else if (ch == 'e' || ch == 'E') {
            bool ok = ad9850_driver_serial_enable(&driver);
            printf("Serial enable: %s\n", ok ? "ok" : "failed");
        } else if (ch == 'n' || ch == 'N') {
            bool ok = frame_ok && ad9850_driver_start_apply_nonblocking(&driver, &frame, false);
            if (ok) {
                nb_start_ok_count++;
            } else if (ad9850_driver_is_nonblocking_busy(&driver)) {
                nb_start_busy_count++;
            } else {
                nb_start_fail_count++;
            }
            printf("Start non-blocking write: %s\n", ok ? "started" : (ad9850_driver_is_nonblocking_busy(&driver) ? "busy" : "failed"));
        } else if (ch == 'm' || ch == 'M') {
            bool ok = frame_ok && ad9850_driver_start_apply_nonblocking(&driver, &frame, config->use_fqud_pin);
            if (ok) {
                nb_start_ok_count++;
            } else if (ad9850_driver_is_nonblocking_busy(&driver)) {
                nb_start_busy_count++;
            } else {
                nb_start_fail_count++;
            }
            printf("Start non-blocking write + latch: %s\n", ok ? "started" : (ad9850_driver_is_nonblocking_busy(&driver) ? "busy" : "failed"));
        } else if (ch == 'v' || ch == 'V') {
            ad9850_driver_service_nonblocking(&driver);
            printf("Advanced the non-blocking transfer once.\n");
        } else if (ch == 'b' || ch == 'B') {
            printf("Non-blocking transfer busy: %s\n",
                   ad9850_driver_is_nonblocking_busy(&driver) ? "yes" : "no");
        } else if (ch == 'x' || ch == 'X') {
            printf("Non-blocking counters:\n");
            printf("  start_ok=%lu start_busy=%lu start_fail=%lu complete_ok=%lu complete_fail=%lu\n",
                   (unsigned long) nb_start_ok_count,
                   (unsigned long) nb_start_busy_count,
                   (unsigned long) nb_start_fail_count,
                   (unsigned long) nb_complete_ok_count,
                   (unsigned long) nb_complete_fail_count);
        } else if (ch == '+') {
            frequency_hz += step_hz;
            ftw = ftw_from_freq(frequency_hz, dds_sysclk_hz);
            frame_ok = ad9850_driver_make_frame(ftw, phase, power_down, &frame);
            print_status(frequency_hz,
                         step_hz,
                         phase,
                         power_down,
                         ftw,
                         &frame,
                         driver.serial_enabled,
                         ad9850_driver_is_nonblocking_busy(&driver));
        } else if (ch == '-') {
            frequency_hz = frequency_hz > step_hz ? (frequency_hz - step_hz) : VALIDATION_SYSCLK_STABILITY_MIN_SAMPLES_FALLBACK;
            ftw = ftw_from_freq(frequency_hz, dds_sysclk_hz);
            frame_ok = ad9850_driver_make_frame(ftw, phase, power_down, &frame);
            print_status(frequency_hz,
                         step_hz,
                         phase,
                         power_down,
                         ftw,
                         &frame,
                         driver.serial_enabled,
                         ad9850_driver_is_nonblocking_busy(&driver));
        } else if (ch == 'd' || ch == 'D') {
            power_down = !power_down;
            frame_ok = ad9850_driver_make_frame(ftw, phase, power_down, &frame);
            print_status(frequency_hz,
                         step_hz,
                         phase,
                         power_down,
                         ftw,
                         &frame,
                         driver.serial_enabled,
                         ad9850_driver_is_nonblocking_busy(&driver));
        } else if (ch == 's' || ch == 'S') {
            print_status(frequency_hz,
                         step_hz,
                         phase,
                         power_down,
                         ftw,
                         &frame,
                         driver.serial_enabled,
                         ad9850_driver_is_nonblocking_busy(&driver));
        } else if (ch == 'q' || ch == 'Q') {
            printf("Leaving AD9850 validation\n");
            break;
        }
    }

    ad9850_driver_deinit(&driver);
}
