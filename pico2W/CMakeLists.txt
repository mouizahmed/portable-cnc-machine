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
cmake_minimum_required(VERSION 3.13...3.27)

if(NOT DEFINED PICO_BOARD)
    set(PICO_BOARD pico2_w CACHE STRING "Target Pico board")
endif()

if(NOT DEFINED PICO_SDK_FETCH_FROM_GIT AND NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_FETCH_FROM_GIT ON CACHE BOOL "Fetch pico-sdk automatically")
endif()

include(pico_sdk_import.cmake)

project(portable_cnc_machine C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

pico_sdk_init()

add_executable(portable_cnc_machine
    src/app_frame.cpp
    src/calibration_storage.cpp
    src/ili9488.cpp
    src/main.cpp
    src/main_menu_screen.cpp
    src/portable_cnc_app.cpp
    src/touch_calibration_app.cpp
    src/xpt2046.cpp
)

target_include_directories(portable_cnc_machine PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(portable_cnc_machine PRIVATE
    hardware_flash
    hardware_spi
    hardware_sync
    pico_stdlib
)

pico_enable_stdio_uart(portable_cnc_machine 1)
pico_enable_stdio_usb(portable_cnc_machine 0)

pico_add_extra_outputs(portable_cnc_machine)

