# Driver manifest: pio_sysclk_stability

set(DRIVER_MODULE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/pio_sysclk_stability.c
    ${CMAKE_CURRENT_LIST_DIR}/pio_sysclk_stability_monitor.c
)

set(DRIVER_MODULE_PIO_FILES
    ${CMAKE_CURRENT_LIST_DIR}/pio_sysclk_stability.pio
)