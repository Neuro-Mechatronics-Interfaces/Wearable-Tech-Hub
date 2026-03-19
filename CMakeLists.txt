# ── Standalone preamble ───────────────────────────────────────────────────────
# When this file IS the top-level CMakeLists.txt (cloned standalone), we handle
# the Pico SDK bootstrap ourselves.  When included via add_subdirectory() from a
# parent project the parent has already called pico_sdk_init() so we skip it.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.18)

    option(HUB_BUILD "Build the Wearable Tech Hub firmware" ON)

    if(NOT DEFINED PICO_SDK_PATH)
        if(DEFINED ENV{PICO_SDK_PATH})
            set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
        else()
            message(FATAL_ERROR
                "PICO_SDK_PATH is not set.\n"
                "Export it as an environment variable or pass -DPICO_SDK_PATH=<path>.")
        endif()
    endif()

    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

    project(wth_firmware C CXX ASM)
    set(CMAKE_C_STANDARD   11)
    set(CMAKE_CXX_STANDARD 17)

    pico_sdk_init()
endif()

# ── Target ────────────────────────────────────────────────────────────────────

add_executable(wth_firmware
    src/main.c
    src/ble_central.c
    src/hid_parser.c
    src/hid_merger.c
    src/output_profiles.c
    src/usb_output.c
    src/pairing_store.c
    usb_descriptors.c
)

# ── Optional GPIO button/LED support ─────────────────────────────────────────
# Enable with:  cmake … -DHUB_GPIO_BUTTONS=ON
option(HUB_GPIO_BUTTONS "Enable physical buttons and status LEDs" OFF)
if(HUB_GPIO_BUTTONS)
    target_sources(wth_firmware PRIVATE src/gpio_buttons.c)
    target_compile_definitions(wth_firmware PRIVATE HUB_ENABLE_GPIO_BUTTONS=1)

    # Override default GPIO pins here if needed, e.g.:
    #   target_compile_definitions(wth_firmware PRIVATE HUB_GPIO_BTN_A=20)
    #   target_compile_definitions(wth_firmware PRIVATE HUB_GPIO_BTN_B=21)
    #   target_compile_definitions(wth_firmware PRIVATE HUB_GPIO_LED_PAIR=18)
    #   target_compile_definitions(wth_firmware PRIVATE HUB_GPIO_LED_DEVICE=19)
endif()

target_include_directories(wth_firmware PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}          # btstack_config.h, tusb_config.h, lwipopts.h
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(wth_firmware PRIVATE
    pico_stdlib
    pico_multicore
    pico_flash
    hardware_sync
    hardware_flash
    hardware_gpio
    hardware_watchdog

    # CYW43 / BTstack — BLE central only (no WiFi, no lwIP)
    pico_cyw43_arch_none
    pico_btstack_ble
    pico_btstack_cyw43

    # TinyUSB — device mode
    tinyusb_device
    tinyusb_board
)

pico_enable_stdio_usb(wth_firmware 0)
pico_enable_stdio_uart(wth_firmware 0)

pico_add_extra_outputs(wth_firmware)

# ── Fallback UF2 when picotool is not installed ───────────────────────────────
# pico_add_extra_outputs only emits .uf2 when picotool is found.  When it is
# absent we run uf2_gen.py (pure Python, no extra deps) ourselves.
if(NOT TARGET picotool)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        # Family IDs from pico-sdk boot/uf2.h
        if(PICO_RP2350)
            set(_UF2_FAMILY 0xe48bff59)   # RP2350-ARM-S
        else()
            set(_UF2_FAMILY 0xe48bff56)   # RP2040
        endif()
        add_custom_command(TARGET wth_firmware POST_BUILD
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/uf2_gen.py
                $<TARGET_FILE_DIR:wth_firmware>/wth_firmware.bin
                $<TARGET_FILE_DIR:wth_firmware>/wth_firmware.uf2
                0x10000000
                ${_UF2_FAMILY}
            COMMENT "Generating wth_firmware.uf2 (picotool not found — using Python fallback)"
            VERBATIM
        )
    else()
        message(WARNING "Neither picotool nor Python3 found — .uf2 will not be generated")
    endif()
endif()
