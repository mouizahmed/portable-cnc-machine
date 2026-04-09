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

set(PICO_TINYUSB_FATFS_DIR ${PICO_SDK_PATH}/lib/tinyusb/lib/fatfs/source)
set(PICO_TINYUSB_NETWORKING_DIR ${PICO_SDK_PATH}/lib/tinyusb/lib/networking)
set(PICO_LWIP_DIR ${PICO_SDK_PATH}/lib/lwip)
find_program(NODE_EXECUTABLE NAMES node nodejs REQUIRED)

set(GENERATED_WEB_ASSET_INC ${CMAKE_CURRENT_BINARY_DIR}/generated/web_assets_generated.inc)
add_custom_command(
    OUTPUT ${GENERATED_WEB_ASSET_INC}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
    COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/webui/generate-assets.mjs
            --src ${CMAKE_CURRENT_LIST_DIR}/webui/src
            --out ${GENERATED_WEB_ASSET_INC}
    DEPENDS
        ${CMAKE_CURRENT_LIST_DIR}/webui/generate-assets.mjs
        ${CMAKE_CURRENT_LIST_DIR}/webui/src/index.html
        ${CMAKE_CURRENT_LIST_DIR}/webui/src/main.ts
        ${CMAKE_CURRENT_LIST_DIR}/webui/src/styles.css
    VERBATIM
)
add_custom_target(portable_cnc_web_assets DEPENDS ${GENERATED_WEB_ASSET_INC})

add_executable(portable_cnc_machine
    src/app/portable_cnc_app.cpp
    src/app/jog/jog_state_machine.cpp
    src/app/job/job_state_machine.cpp
    src/app/machine/machine_state_machine.cpp
    src/app/navigation/screen_router.cpp
    src/app/status/status_line_formatter.cpp
    src/app/status/status_provider.cpp
    src/app/storage/storage_service.cpp
    src/services/portable_cnc_controller.cpp
    src/calibration/calibration_storage.cpp
    src/calibration/touch_calibration_app.cpp
    src/drivers/ili9488.cpp
    src/drivers/sd_spi_card.cpp
    src/drivers/sd_spi_diskio.cpp
    src/drivers/xpt2046.cpp
    src/web/web_assets.cpp
    src/web/web_server.cpp
    src/web/usb_descriptors.c
    src/main.cpp
    src/ui/components/app_frame.cpp
    src/ui/components/menu_card.cpp
    src/ui/components/nav_bar.cpp
    src/ui/components/selectable_list_row.cpp
    src/ui/components/status_bar.cpp
    src/ui/helpers/ui_helpers.cpp
    src/ui/screens/files_screen.cpp
    src/ui/screens/jog_screen.cpp
    src/ui/screens/main_menu_screen.cpp
    ${PICO_TINYUSB_FATFS_DIR}/ff.c
    ${PICO_TINYUSB_FATFS_DIR}/ffunicode.c
    ${PICO_TINYUSB_NETWORKING_DIR}/dhserver.c
    ${PICO_TINYUSB_NETWORKING_DIR}/dnserver.c
    ${PICO_TINYUSB_NETWORKING_DIR}/rndis_reports.c
    ${PICO_LWIP_DIR}/src/core/def.c
    ${PICO_LWIP_DIR}/src/core/dns.c
    ${PICO_LWIP_DIR}/src/core/inet_chksum.c
    ${PICO_LWIP_DIR}/src/core/init.c
    ${PICO_LWIP_DIR}/src/core/ip.c
    ${PICO_LWIP_DIR}/src/core/mem.c
    ${PICO_LWIP_DIR}/src/core/memp.c
    ${PICO_LWIP_DIR}/src/core/netif.c
    ${PICO_LWIP_DIR}/src/core/pbuf.c
    ${PICO_LWIP_DIR}/src/core/stats.c
    ${PICO_LWIP_DIR}/src/core/sys.c
    ${PICO_LWIP_DIR}/src/core/tcp.c
    ${PICO_LWIP_DIR}/src/core/tcp_in.c
    ${PICO_LWIP_DIR}/src/core/tcp_out.c
    ${PICO_LWIP_DIR}/src/core/timeouts.c
    ${PICO_LWIP_DIR}/src/core/udp.c
    ${PICO_LWIP_DIR}/src/core/ipv4/autoip.c
    ${PICO_LWIP_DIR}/src/core/ipv4/etharp.c
    ${PICO_LWIP_DIR}/src/core/ipv4/icmp.c
    ${PICO_LWIP_DIR}/src/core/ipv4/igmp.c
    ${PICO_LWIP_DIR}/src/core/ipv4/ip4.c
    ${PICO_LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${PICO_LWIP_DIR}/src/core/ipv4/ip4_frag.c
    ${PICO_LWIP_DIR}/src/netif/ethernet.c
)

add_dependencies(portable_cnc_machine portable_cnc_web_assets)

target_include_directories(portable_cnc_machine PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/src/web
    ${CMAKE_CURRENT_LIST_DIR}/src/web/arch
    ${CMAKE_CURRENT_BINARY_DIR}
    ${PICO_TINYUSB_FATFS_DIR}
    ${PICO_TINYUSB_NETWORKING_DIR}
    ${PICO_LWIP_DIR}/src/include
    ${PICO_LWIP_DIR}/src/include/ipv4
)

target_link_libraries(portable_cnc_machine PRIVATE
    hardware_flash
    hardware_spi
    hardware_sync
    pico_stdlib
    tinyusb_board
    tinyusb_device
)

pico_enable_stdio_uart(portable_cnc_machine 1)
pico_enable_stdio_usb(portable_cnc_machine 0)

pico_add_extra_outputs(portable_cnc_machine)

