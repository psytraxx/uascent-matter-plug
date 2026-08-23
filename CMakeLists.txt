#
# Matter smart plug for the Seeed XIAO nRF52840 Sense.
# Retrofit into a BL0937 mains plug; see docs/README.md.
#

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})

project(blink)

# Enable GNU STD support.
include(${ZEPHYR_CONNECTEDHOMEIP_MODULE_DIR}/config/nrfconnect/app/enable-gnu-std.cmake)

# NORDIC SDK APP START

# Include all source files that are located in the Matter common directory.
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/source_common.cmake)

# Include Data Model utils
include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/data_model.cmake)

include(${ZEPHYR_NRF_MODULE_DIR}/samples/matter/common/cmake/zap_helpers.cmake)
ncs_get_zap_parent_dir(ZAP_PARENT_DIR)

target_include_directories(app PRIVATE
  src
  ${ZAP_PARENT_DIR}
)

target_compile_options(app PRIVATE -Wno-deprecated-declarations)

target_sources(app PRIVATE
  src/app_task.cpp
  src/bl0937.cpp
  src/main.cpp
  src/power_measurement.cpp
  src/relay.cpp
  src/status_led.cpp
  src/zcl_callbacks.cpp
)

ncs_configure_data_model()
# NORDIC SDK APP END
