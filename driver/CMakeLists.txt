# Driver module aggregation for top-level build wiring.
#
# Each driver subfolder provides a local `driver_manifest.cmake` that sets:
# - DRIVER_MODULE_SOURCES
# - DRIVER_MODULE_PIO_FILES
#
# This file includes each manifest and aggregates them into shared lists
# exported to the parent scope.

set(DRIVER_SOURCES)
set(DRIVER_PIO_FILES)

include(${CMAKE_CURRENT_LIST_DIR}/pio_timer_input_capture/driver_manifest.cmake)
list(APPEND DRIVER_SOURCES ${DRIVER_MODULE_SOURCES})
list(APPEND DRIVER_PIO_FILES ${DRIVER_MODULE_PIO_FILES})

include(${CMAKE_CURRENT_LIST_DIR}/pio_sysclk_stability/driver_manifest.cmake)
list(APPEND DRIVER_SOURCES ${DRIVER_MODULE_SOURCES})
list(APPEND DRIVER_PIO_FILES ${DRIVER_MODULE_PIO_FILES})

include(${CMAKE_CURRENT_LIST_DIR}/pio_timer_output_compare/driver_manifest.cmake)
list(APPEND DRIVER_SOURCES ${DRIVER_MODULE_SOURCES})
list(APPEND DRIVER_PIO_FILES ${DRIVER_MODULE_PIO_FILES})

include(${CMAKE_CURRENT_LIST_DIR}/pio_alarm_timer/driver_manifest.cmake)
list(APPEND DRIVER_SOURCES ${DRIVER_MODULE_SOURCES})
list(APPEND DRIVER_PIO_FILES ${DRIVER_MODULE_PIO_FILES})

include(${CMAKE_CURRENT_LIST_DIR}/ad9850_driver/driver_manifest.cmake)
list(APPEND DRIVER_SOURCES ${DRIVER_MODULE_SOURCES})
list(APPEND DRIVER_PIO_FILES ${DRIVER_MODULE_PIO_FILES})

set(DRIVER_SOURCES
    ${DRIVER_SOURCES}
    PARENT_SCOPE
)

set(DRIVER_PIO_FILES
    ${DRIVER_PIO_FILES}
    PARENT_SCOPE
)
