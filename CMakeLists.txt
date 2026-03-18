add_executable(mudra_hub
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
    target_sources(mudra_hub PRIVATE src/gpio_buttons.c)
    target_compile_definitions(mudra_hub PRIVATE HUB_ENABLE_GPIO_BUTTONS=1)

    # Override default GPIO pins here if needed, e.g.:
    #   target_compile_definitions(mudra_hub PRIVATE HUB_GPIO_BTN_A=20)
    #   target_compile_definitions(mudra_hub PRIVATE HUB_GPIO_BTN_B=21)
    #   target_compile_definitions(mudra_hub PRIVATE HUB_GPIO_LED_PAIR=18)
    #   target_compile_definitions(mudra_hub PRIVATE HUB_GPIO_LED_DEVICE=19)
endif()

target_include_directories(mudra_hub PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}          # btstack_config.h, tusb_config.h, lwipopts.h
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(mudra_hub PRIVATE
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

pico_enable_stdio_usb(mudra_hub 0)
pico_enable_stdio_uart(mudra_hub 0)

pico_add_extra_outputs(mudra_hub)

# ── Fallback UF2 when picotool is not installed ───────────────────────────────
# pico_add_extra_outputs only emits .uf2 when picotool is found.  When it is
# absent we run firmware/uf2_gen.py (pure Python, no extra deps) ourselves.
if(NOT TARGET picotool)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        # Family IDs from pico-sdk boot/uf2.h
        if(PICO_RP2350)
            set(_UF2_FAMILY 0xe48bff59)   # RP2350-ARM-S
        else()
            set(_UF2_FAMILY 0xe48bff56)   # RP2040
        endif()
        add_custom_command(TARGET mudra_hub POST_BUILD
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_LIST_DIR}/../uf2_gen.py
                $<TARGET_FILE_DIR:mudra_hub>/mudra_hub.bin
                $<TARGET_FILE_DIR:mudra_hub>/mudra_hub.uf2
                0x10000000
                ${_UF2_FAMILY}
            COMMENT "Generating mudra_hub.uf2 (picotool not found — using Python fallback)"
            VERBATIM
        )
    else()
        message(WARNING "Neither picotool nor Python3 found — .uf2 will not be generated")
    endif()
endif()
