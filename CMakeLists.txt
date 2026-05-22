cmake_minimum_required(VERSION 3.13)

# --- External paths via env vars (matches pico-sdk's PICO_SDK_PATH convention) ---
#
# Four repos must be checked out somewhere, with env vars pointing at them:
#   PICO_SDK_PATH            -> github.com/raspberrypi/pico-sdk (2.0+)
#   PICO_EXTRAS_PATH         -> github.com/raspberrypi/pico-extras (for pico_sleep)
#   PIMORONI_PICO_PATH       -> github.com/pimoroni/pimoroni-pico
#   PIMORONI_TUFTY2350_PATH  -> github.com/pimoroni/tufty2350  (board header lives here)

if(NOT DEFINED ENV{PICO_EXTRAS_PATH})
    message(FATAL_ERROR
        "PICO_EXTRAS_PATH is not set.\n"
        "Clone https://github.com/raspberrypi/pico-extras and set the env var to its path.")
endif()
if(NOT DEFINED ENV{PIMORONI_PICO_PATH})
    message(FATAL_ERROR
        "PIMORONI_PICO_PATH is not set.\n"
        "Clone https://github.com/pimoroni/pimoroni-pico and set the env var to its path.")
endif()
if(NOT DEFINED ENV{PIMORONI_TUFTY2350_PATH})
    message(FATAL_ERROR
        "PIMORONI_TUFTY2350_PATH is not set.\n"
        "Clone https://github.com/pimoroni/tufty2350 and set the env var to its path.")
endif()
set(PIMORONI_PICO_PATH      "$ENV{PIMORONI_PICO_PATH}"      CACHE PATH "pimoroni-pico checkout")
set(PIMORONI_TUFTY2350_PATH "$ENV{PIMORONI_TUFTY2350_PATH}" CACHE PATH "pimoroni/tufty2350 checkout")

if(NOT EXISTS "${PIMORONI_PICO_PATH}/common/CMakeLists.txt")
    message(FATAL_ERROR "PIMORONI_PICO_PATH '${PIMORONI_PICO_PATH}' does not contain common/CMakeLists.txt")
endif()
if(NOT EXISTS "${PIMORONI_TUFTY2350_PATH}/board/pimoroni_tufty2350.h")
    message(FATAL_ERROR "PIMORONI_TUFTY2350_PATH '${PIMORONI_TUFTY2350_PATH}' does not contain board/pimoroni_tufty2350.h")
endif()

# --- Tell pico-sdk to use the Tufty 2350 board header from the upstream repo ---
set(PICO_BOARD pimoroni_tufty2350 CACHE STRING "Pico board")
list(APPEND PICO_BOARD_HEADER_DIRS "${PIMORONI_TUFTY2350_PATH}/board")

include(pico_sdk_import.cmake)
include(pico_extras_import.cmake)

project(claude_buddy C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# --- PicoGraphics + fonts from pimoroni-pico (Pimoroni's render abstraction).
# We point it at our vendored ST7789 driver's framebuffer rather than letting
# it allocate its own — same 320x240 buffer serves both, since the RP2350's
# 520KB SRAM can't fit two full-resolution 32bpp buffers.
include_directories("${PIMORONI_PICO_PATH}")
include("${PIMORONI_PICO_PATH}/libraries/bitmap_fonts/bitmap_fonts.cmake")
include("${PIMORONI_PICO_PATH}/libraries/hershey_fonts/hershey_fonts.cmake")
include("${PIMORONI_PICO_PATH}/libraries/pico_graphics/pico_graphics.cmake")
include("${PIMORONI_PICO_PATH}/common/pimoroni_i2c.cmake")
include("${PIMORONI_PICO_PATH}/drivers/pcf85063a/pcf85063a.cmake")

# --- Firmware target ---
# The Tufty 2350's ST7789 panel has a different controller variant than the
# Tufty 2040 — different voltage/timing parameters, doesn't honor MADCTL.SWAP_XY.
# The stock pimoroni-pico/drivers/st7789 doesn't work on this panel; Pimoroni
# publishes the correct driver only as a MicroPython module in their tufty2350
# repo. We vendor it under src/drivers/st7789/ (see that dir's source for
# upstream pointer + the small MicroPython-glue strip).
add_executable(claude_buddy
    src/main.cpp
    src/battery.cpp
    src/ble_bridge.cpp
    src/buttons.cpp
    src/buddy.cpp
    src/data.cpp
    src/menu.cpp
    src/power.cpp
    src/rtc.cpp
    src/settings.cpp
    src/drivers/st7789/st7789.cpp
    src/buddies/axolotl.cpp
    src/buddies/blob.cpp
    src/buddies/cactus.cpp
    src/buddies/capybara.cpp
    src/buddies/cat.cpp
    src/buddies/chonk.cpp
    src/buddies/dragon.cpp
    src/buddies/duck.cpp
    src/buddies/ghost.cpp
    src/buddies/goose.cpp
    src/buddies/mushroom.cpp
    src/buddies/octopus.cpp
    src/buddies/owl.cpp
    src/buddies/penguin.cpp
    src/buddies/rabbit.cpp
    src/buddies/robot.cpp
    src/buddies/snail.cpp
    src/buddies/turtle.cpp
)

# Compile the vendored driver's .pio program into a header.
pico_generate_pio_header(claude_buddy
    ${CMAKE_CURRENT_LIST_DIR}/src/drivers/st7789/st7789_parallel.pio
)

# Compile our GATT DSL into a C header that btstack's att_server consumes.
pico_btstack_make_gatt_header(claude_buddy PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/src/nus_service.gatt"
)

target_include_directories(claude_buddy PRIVATE
    src
    src/drivers/st7789
)

# cyw43-driver pulls in lwIP by default for WiFi networking. We use BT only,
# so disable the lwIP integration — saves flash and avoids needing lwipopts.
target_compile_definitions(claude_buddy PRIVATE
    CYW43_LWIP=0
)

target_link_libraries(claude_buddy
    pico_stdlib
    hardware_adc
    hardware_flash
    hardware_i2c
    hardware_pwm
    hardware_pio
    hardware_dma
    hardware_sleep
    hardware_watchdog
    pico_flash
    pico_graphics
    pimoroni_i2c
    pcf85063a
    pico_cyw43_arch_threadsafe_background
    pico_btstack_ble
    pico_btstack_cyw43
    pico_btstack_run_loop_async_context
)

pico_enable_stdio_usb(claude_buddy 1)
pico_enable_stdio_uart(claude_buddy 0)
pico_add_extra_outputs(claude_buddy)
