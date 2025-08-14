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
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(MGController_RP2040 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Tambahkan direktori 'lib' ke path include
include_directories(lib)

# Add executable. Default name is the project name, version 0.1

add_executable(${CMAKE_PROJECT_NAME}
    main.c
    lib/lcd_i2c.c
)

pico_set_program_name(${CMAKE_PROJECT_NAME} "MGController_RP2040")
pico_set_program_version(${CMAKE_PROJECT_NAME} "0.1")

# Proses file .pio dan hasilkan file header C
pico_generate_pio_header(${CMAKE_PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/signal_generator.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_usb(${CMAKE_PROJECT_NAME} 1)
pico_enable_stdio_uart(${CMAKE_PROJECT_NAME} 0)

# Add the standard library to the build
target_link_libraries(${CMAKE_PROJECT_NAME}
    pico_stdlib           # Fungsi dasar (stdio, gpio, etc.)
    hardware_pio          # Fungsi untuk berinteraksi dengan PIO
    hardware_clocks       # Fungsi clock system
    hardware_i2c          # Fungsi I2C untuk LCD
    hardware_flash        # Fungsi untuk penyimpanan flash
    hardware_sync         # Fungsi sinkronisasi dan interrupt
    hardware_timer        # Fungsi alarm/timer
)

# Add the standard include files to the build
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(${CMAKE_PROJECT_NAME})

