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
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(hub75_driver C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(hub75_driver hub75_driver.cpp )

pico_set_program_name(hub75_driver "hub75_driver")
pico_set_program_version(hub75_driver "1.5")

set(PICO_PIO_VERSION 1)
pico_set_float_implementation(hub75_driver pico)

# Generate PIO header
pico_generate_pio_header(hub75_driver ${CMAKE_CURRENT_LIST_DIR}/hub75.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(hub75_driver 0)
pico_enable_stdio_usb(hub75_driver 1)

add_subdirectory(common)
add_subdirectory(libraries)

target_sources(hub75_driver PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/hub75.cpp
        ${CMAKE_CURRENT_LIST_DIR}/bouncing_balls.cpp
        ${CMAKE_CURRENT_LIST_DIR}/antialiased_line.cpp
        ${CMAKE_CURRENT_LIST_DIR}/fire_effect.cpp
        ${CMAKE_CURRENT_LIST_DIR}/rotator.cpp
        ${CMAKE_CURRENT_LIST_DIR}/pixel_fill.hpp
        ${CMAKE_CURRENT_LIST_DIR}/rul6024.cpp
        ${CMAKE_CURRENT_LIST_DIR}/fm6126a.cpp
        ${CMAKE_CURRENT_LIST_DIR}/hue_value_spectrum.hpp
        ${CMAKE_CURRENT_LIST_DIR}/hub75_driver.cpp
        )

# Add the standard library to the build
target_link_libraries(hub75_driver
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_dma
        hardware_timer
        hardware_vreg
        pico_graphics)

if(PICO_CYW43_SUPPORTED)
    target_link_libraries(hub75_driver
                          pico_cyw43_arch_none)
endif()

# Add the standard include files to the build
target_include_directories(hub75_driver PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(hub75_driver)

