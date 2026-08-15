cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(digital_oscilloscope C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

add_executable(oscilloscope
    src/main.c
    src/adc_dma.c
    src/usb_descriptors.c
)

target_include_directories(oscilloscope PRIVATE include)

target_link_libraries(oscilloscope
    pico_stdlib
    hardware_adc
    hardware_dma
    hardware_irq
    tinyusb_device
    tinyusb_board
)

# Enable USB stdio and disable UART stdio so the CDC port is free for data
pico_enable_stdio_usb(oscilloscope 0)
pico_enable_stdio_uart(oscilloscope 0)

pico_add_extra_outputs(oscilloscope)
