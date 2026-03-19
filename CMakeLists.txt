# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Costas_v1_RP2040 C CXX ASM)

# Dedicated feature toggle for PIO capture validation firmware path.
option(PIO_TIMER_CAPTURE_VALIDATION "Enable PIO timer capture validation mode" OFF)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Centralized driver module manifest (exports driver source/PIO lists).
add_subdirectory(driver)

# Choose entrypoint source by mode:
# - normal mode: production `src/main.c`
# - validation mode: dedicated `src/validation/main_validation.c` (production main excluded)

set(APP_MAIN_SOURCE src/main.c)
if(PIO_TIMER_CAPTURE_VALIDATION)
    set(APP_MAIN_SOURCE src/validation/main_validation.c)
endif()

add_executable(Costas_v1_RP2040 ${APP_MAIN_SOURCE})

if(PIO_TIMER_CAPTURE_VALIDATION)
    target_sources(Costas_v1_RP2040 PRIVATE
        src/validation/pio_timer_input_capture_validation.c
        src/validation/pio_timer_output_compare_validation.c
    )
endif()

# Shared driver sources and generated PIO headers (used in both prod/validation).
target_sources(Costas_v1_RP2040 PRIVATE
    ${DRIVER_SOURCES}
)
foreach(driver_pio_file IN LISTS DRIVER_PIO_FILES)
    pico_generate_pio_header(Costas_v1_RP2040 ${driver_pio_file})
endforeach()

pico_set_program_name(Costas_v1_RP2040 "Costas_v1_RP2040")
pico_set_program_version(Costas_v1_RP2040 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(Costas_v1_RP2040 0)
pico_enable_stdio_usb(Costas_v1_RP2040 1)

# Add the standard library to the build
target_link_libraries(Costas_v1_RP2040
    pico_stdlib
    # Driver modules rely on direct PIO and clock APIs in both modes.
    hardware_pio
    hardware_clocks
)

# Add the standard include files to the build
target_include_directories(Costas_v1_RP2040 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(Costas_v1_RP2040)

