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

# set(PICO_PLATFORM rp2350)
# set(PICO_BOARD none)

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(hub75 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name
add_executable(hub75 hub75_demo.cpp )

pico_set_program_name(hub75 "hub75_demo")
pico_set_program_version(hub75 "2.0")

set(PICO_PIO_VERSION 1)
pico_set_float_implementation(hub75 pico)

set(PANEL_GENERIC 0)
set(PANEL_FM6126A 1)
set(PANEL_RUL6024 2)

target_compile_definitions(hub75 PRIVATE
    # PICO_RP2350A=0 
    # USE_PICO_GRAPHICS=true
    # MATRIX_PANEL_WIDTH=64
    # MATRIX_PANEL_HEIGHT=64
    # DATA_BASE_PIN=30
    # DATA_N_PINS=6
    # ROWSEL_BASE_PIN=36
    # ROWSEL_N_PINS=5
    # CLK_PIN=41
    # STROBE_PIN=42
    # OEN_PIN=43
    # PANEL_TYPE=PANEL_GENERIC
    # INVERTED_STB=false
    # TEMPORAL_DITHERING=false
    # SM_CLOCKDIV_FACTOR=1.0f
)

# Generate PIO header
pico_generate_pio_header(hub75 ${CMAKE_CURRENT_LIST_DIR}/src/hub75.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(hub75 0)
pico_enable_stdio_usb(hub75 1)

add_subdirectory(common)
add_subdirectory(libraries)

target_sources(hub75 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/hub75.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/bouncing_balls.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/antialiased_line.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/fire_effect.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/rotator.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/analog_clock.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/pixel_fill.hpp
        ${CMAKE_CURRENT_LIST_DIR}/src/rul6024.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/fm6126a.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/hue_value_spectrum.hpp
        ${CMAKE_CURRENT_LIST_DIR}/hub75_demo.cpp
        )

# Add the standard library to the build
target_link_libraries(hub75
        pico_stdlib
        pico_multicore
        pico_aon_timer
        hardware_pio
        hardware_dma
        hardware_timer
        hardware_vreg
        pico_graphics)

if(PICO_CYW43_SUPPORTED)
    target_link_libraries(hub75
                          pico_cyw43_arch_none)
endif()

# Add the standard include files to the build
target_include_directories(hub75 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/examples
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(hub75)

