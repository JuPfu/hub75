# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# As far as I know the Raspberry Pi Pico plugin for VSCode does not (yet) support bare RP2350 microcontrollers without a board.
# To adapt the C-SDK for a standard Pico board configure this CMakeLists.txt file via the Raspberry Pi Pico plugin's "Switch Board" option.
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Uncomment the following two lines to compile for bare RP2350 without a board
# set(PICO_PLATFORM rp2350)
# set(PICO_BOARD none CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(hub75_demo C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

add_subdirectory(utils)

# Add library. This is the core HUB75 driver.
add_library(hub75 STATIC)

target_sources(hub75 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/hub75.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/rul6024.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/fm6126a.cpp
)

target_link_libraries(hub75 PUBLIC
        pico_stdlib
        pico_multicore
        pico_aon_timer
        hardware_pio
        hardware_dma
        hardware_vreg
        pico_graphics
)

target_include_directories(hub75 PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_include_directories(hub75 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_generate_pio_header(hub75 ${CMAKE_CURRENT_LIST_DIR}/src/hub75.pio)


# Add demo executable. Everything that is only required for the demo remains here.
add_executable(hub75_demo hub75_demo.cpp )

pico_set_program_name(hub75_demo "hub75_demo")
pico_set_program_version(hub75_demo "3.1")

pico_set_float_implementation(hub75_demo pico)

target_compile_definitions(hub75_demo PRIVATE
    # PICO_RP2350A=0         # PICO_RP2350A=0` means not a RP2350A but a RP2350B microcontroller - uncomment for RP235xB microcontroller only!
    USE_PICO_GRAPHICS=true   # set to false if you use hub75 as a library - any reference to pico_graphics is removed
    HUB75_MULTICORE=true     # use core1 for the hub75 driver
)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(hub75_demo 0)
pico_enable_stdio_usb(hub75_demo 1)

add_subdirectory(common)
add_subdirectory(libraries)

target_sources(hub75_demo PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/hub75.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/rul6024.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/fm6126a.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/analog_clock.hpp
        # ${CMAKE_CURRENT_LIST_DIR}/examples/antialiased_line.cpp
        # ${CMAKE_CURRENT_LIST_DIR}/examples/bouncing_balls.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/fire_effect.cpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/pixel_fill.hpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/grey_scale_stripes.hpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/rectangle.hpp
        ${CMAKE_CURRENT_LIST_DIR}/examples/hue_value_spectrum.hpp


        ${CMAKE_CURRENT_LIST_DIR}/hub75_demo.cpp
        )

# Add the standard library to the build
target_link_libraries(hub75_demo PRIVATE
        pico_stdlib
        pico_multicore
        pico_aon_timer
        hardware_pio
        hardware_dma
        hardware_vreg
        pico_graphics)

if(PICO_CYW43_SUPPORTED)
    target_link_libraries(hub75_demo PRIVATE
                          pico_cyw43_arch_none)
endif()

# Add the standard include files to the build
target_include_directories(hub75_demo PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/examples
)

pico_generate_pio_header(hub75_demo ${CMAKE_CURRENT_LIST_DIR}/src/hub75.pio)


pico_add_extra_outputs(hub75_demo)
print_size_info(hub75_demo)


# Add dual-panel demo executable - two independent Hub75Driver instances, see
# hub75_demo_dual.cpp for details.
add_executable(hub75_demo_dual hub75_demo_dual.cpp)

pico_set_program_name(hub75_demo_dual "hub75_demo_dual")
pico_set_program_version(hub75_demo_dual "3.1")

pico_set_float_implementation(hub75_demo_dual pico)

target_compile_definitions(hub75_demo_dual PRIVATE
    USE_PICO_GRAPHICS=true
)

pico_enable_stdio_uart(hub75_demo_dual 0)
pico_enable_stdio_usb(hub75_demo_dual 1)

target_sources(hub75_demo_dual PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src/hub75.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/rul6024.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/fm6126a.cpp
        ${CMAKE_CURRENT_LIST_DIR}/hub75_demo_dual.cpp
        )

target_link_libraries(hub75_demo_dual PRIVATE
        pico_stdlib
        pico_multicore
        pico_aon_timer
        hardware_pio
        hardware_dma
        hardware_vreg
        pico_graphics)

target_include_directories(hub75_demo_dual PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_generate_pio_header(hub75_demo_dual ${CMAKE_CURRENT_LIST_DIR}/src/hub75.pio)

pico_add_extra_outputs(hub75_demo_dual)
print_size_info(hub75_demo_dual)

