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
    src/app/portable_cnc_app.cpp
    src/app/job/job_state_machine.cpp
    src/app/machine/machine_state_machine.cpp
    src/app/navigation/screen_router.cpp
    src/app/status/status_line_formatter.cpp
    src/app/status/status_provider.cpp
    src/app/storage/storage_service.cpp
    src/calibration/calibration_storage.cpp
    src/calibration/touch_calibration_app.cpp
    src/drivers/ili9488.cpp
    src/drivers/sd_spi_card.cpp
    src/drivers/sd_spi_diskio.cpp
    src/drivers/xpt2046.cpp
    src/main.cpp
    src/ui/components/app_frame.cpp
    src/ui/components/menu_card.cpp
    src/ui/components/nav_bar.cpp
    src/ui/components/selectable_list_row.cpp
    src/ui/components/status_bar.cpp
    src/ui/helpers/ui_helpers.cpp
    src/ui/screens/files_screen.cpp
    src/ui/screens/main_menu_screen.cpp
    C:/Users/mouiz/.pico-sdk/sdk/2.2.0/lib/tinyusb/lib/fatfs/source/ff.c
    C:/Users/mouiz/.pico-sdk/sdk/2.2.0/lib/tinyusb/lib/fatfs/source/ffunicode.c
)

target_include_directories(portable_cnc_machine PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    C:/Users/mouiz/.pico-sdk/sdk/2.2.0/lib/tinyusb/lib/fatfs/source
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

