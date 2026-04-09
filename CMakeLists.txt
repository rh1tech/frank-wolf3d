cmake_minimum_required(VERSION 3.13)

# Set board to Pico 2 (RP2350)
set(PICO_BOARD pico2 CACHE STRING "Pico board type")

# Board configuration variant (M1, M2)
set(BOARD_VARIANT "M1" CACHE STRING "Board variant: M1 or M2")

# CPU overclock configuration (252, 378, 504 MHz)
# 252 = no overclock (default), 378 = medium, 504 = high
set(CPU_SPEED "378" CACHE STRING "CPU clock in MHz: 252, 378, 504")

# PSRAM speed configuration (84, 100, 133, 166 MHz target)
set(PSRAM_SPEED "100" CACHE STRING "PSRAM max frequency in MHz: 84, 100, 133, 166")

# Flash max frequency (66, 88 MHz)
set(FLASH_SPEED "66" CACHE STRING "Flash max frequency in MHz: 66, 88")

# CPU voltage selection based on speed
if(CPU_SPEED GREATER_EQUAL 504)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_65")
elseif(CPU_SPEED GREATER_EQUAL 378)
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_60")
else()
    set(CPU_VOLTAGE "VREG_VOLTAGE_1_50")
endif()

message(STATUS "Board: ${BOARD_VARIANT}, CPU: ${CPU_SPEED} MHz, PSRAM: ${PSRAM_SPEED} MHz, Flash: ${FLASH_SPEED} MHz, Voltage: ${CPU_VOLTAGE}")

# Inject version.txt as a compile-time string (format: "MAJOR.MM" with zero-padded minor).
file(READ "${CMAKE_CURRENT_LIST_DIR}/version.txt" FRANK_WOLF3D_VERSION_RAW)
string(STRIP "${FRANK_WOLF3D_VERSION_RAW}" FRANK_WOLF3D_VERSION_RAW)
string(REGEX MATCH "^([0-9]+)" FRANK_WOLF3D_VER_MAJOR "${FRANK_WOLF3D_VERSION_RAW}")
string(REGEX MATCH "([0-9]+)$" FRANK_WOLF3D_VER_MINOR "${FRANK_WOLF3D_VERSION_RAW}")
if(FRANK_WOLF3D_VER_MINOR LESS 10)
    set(FRANK_WOLF3D_VERSION "${FRANK_WOLF3D_VER_MAJOR}.0${FRANK_WOLF3D_VER_MINOR}")
else()
    set(FRANK_WOLF3D_VERSION "${FRANK_WOLF3D_VER_MAJOR}.${FRANK_WOLF3D_VER_MINOR}")
endif()

include(pico_sdk_import.cmake)

project(frank-wolf3d C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Add fatfs
add_subdirectory(src/fatfs)
# Add drivers
add_subdirectory(drivers/sdcard)
add_subdirectory(drivers/ps2)
add_subdirectory(drivers/nespad)
add_subdirectory(src/ps2kbd)
add_subdirectory(drivers/usbhid)

# Create a library for other drivers
add_library(drivers
    drivers/HDMI.c
    drivers/psram_init.c
    drivers/psram_allocator.c
)
target_include_directories(drivers PUBLIC drivers)
target_compile_definitions(drivers PRIVATE
    BOARD_${BOARD_VARIANT}
    PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
)
target_compile_options(drivers PRIVATE -Ofast)
target_link_libraries(drivers pico_stdlib hardware_dma hardware_pio hardware_spi)

# =============================================================================
# Wolfenstein 3D Build
# =============================================================================

# Wolf3D sources (original game logic)
set(WOLF3D_SOURCES
    src/wolf3d/wl_act1.c
    src/wolf3d/wl_act2.c
    src/wolf3d/wl_agent.c
    src/wolf3d/wl_atmos.c
    src/wolf3d/wl_cloudsky.c
    src/wolf3d/wl_debug.c
    src/wolf3d/wl_draw.c
    src/wolf3d/wl_game.c
    src/wolf3d/wl_inter.c
    src/wolf3d/wl_main.c
    src/wolf3d/wl_menu.c
    src/wolf3d/wl_parallax.c
    src/wolf3d/wl_plane.c
    src/wolf3d/wl_play.c
    src/wolf3d/wl_scale.c
    src/wolf3d/wl_shade.c
    src/wolf3d/wl_state.c
    src/wolf3d/wl_text.c
    src/wolf3d/wl_utils.c
    src/wolf3d/id_ca.c
    src/wolf3d/id_pm.c
    src/wolf3d/id_us.c
    src/wolf3d/signon.c
    src/wolf3d/mame/fmopl.c
)

add_executable(frank-wolf3d
    src/main.c
    src/wolf_rp2350.c
    src/id_vl_rp2350.c
    src/id_in_rp2350.c
    src/id_sd_rp2350.c
    src/id_vh_rp2350.c
    src/stdio_fatfs.c
    src/pico/audio.c
    ${WOLF3D_SOURCES}
)

# Generate PIO header for I2S audio (must be after add_executable)
add_subdirectory(drivers/audio)

target_include_directories(frank-wolf3d PRIVATE
    src
    src/wolf3d
    src/wolf3d/mame
    src/pico
    src/fatfs
    drivers
    drivers/sdcard
    drivers/ps2
)

target_compile_options(frank-wolf3d PRIVATE -Ofast)

target_compile_definitions(frank-wolf3d PRIVATE
    PICO_ON_DEVICE=1
    BOARD_${BOARD_VARIANT}
    CPU_CLOCK_MHZ=${CPU_SPEED}
    CPU_VOLTAGE=${CPU_VOLTAGE}
    PSRAM_MAX_FREQ_MHZ=${PSRAM_SPEED}
    FLASH_MAX_FREQ_MHZ=${FLASH_SPEED}
    # Wolf3D version: auto-detect WL6/WL3/WL1 at runtime (Apogee v1.1)
    # Data has 158 gfx chunks (no H_SPEARADPIC, no C_JOY1PIC/C_JOY2PIC)
    APOGEE_1_1
    CARMACIZED
    VERSIONALREADYCHOSEN
    DEBUGKEYS
    ARTSEXTERN
    DEMOSEXTERN
    PLAYDEMOLIKEORIGINAL
    ADDEDFIX
    FIXCALCROTATE
    FRANK_WOLF3D_VERSION="${FRANK_WOLF3D_VERSION}"
)

target_link_options(frank-wolf3d PRIVATE -Wl,-Map=frank-wolf3d.map)

# Set peripheral pins based on board variant
if(BOARD_VARIANT STREQUAL "M1")
    target_compile_definitions(frank-wolf3d PRIVATE
        # I2S Audio
        PICO_AUDIO_I2S_DATA_PIN=26
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=27
        # SD Card
        SDCARD_PIN_SPI0_CS=5
        SDCARD_PIN_SPI0_SCK=2
        SDCARD_PIN_SPI0_MOSI=3
        SDCARD_PIN_SPI0_MISO=4
    )
elseif(BOARD_VARIANT STREQUAL "M2")
    target_compile_definitions(frank-wolf3d PRIVATE
        # I2S Audio
        PICO_AUDIO_I2S_DATA_PIN=9
        PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
        # SD Card
        SDCARD_PIN_SPI0_CS=5
        SDCARD_PIN_SPI0_SCK=6
        SDCARD_PIN_SPI0_MOSI=7
        SDCARD_PIN_SPI0_MISO=4
    )
endif()

target_link_libraries(frank-wolf3d
    pico_stdlib
    pico_multicore
    hardware_clocks
    hardware_spi
    hardware_dma
    hardware_pio
    hardware_pwm
    hardware_irq
    hardware_interp
    sdcard
    ps2_driver
    ps2kbd
    nespad
    usbhid
    fatfs
    drivers
)

# Wrap stdio for FatFS
target_link_options(frank-wolf3d PRIVATE
    -Wl,--wrap=fopen
    -Wl,--wrap=fclose
    -Wl,--wrap=fread
    -Wl,--wrap=fgetc
    -Wl,--wrap=fwrite
    -Wl,--wrap=fseek
    -Wl,--wrap=ftell
    -Wl,--wrap=remove
    -Wl,--wrap=rename
    -Wl,--wrap=stat
)

# USB stdio
if(USB_HID_ENABLED)
    pico_enable_stdio_usb(frank-wolf3d 0)
    pico_enable_stdio_uart(frank-wolf3d 1)
else()
    pico_enable_stdio_usb(frank-wolf3d 1)
    pico_enable_stdio_uart(frank-wolf3d 0)
endif()

pico_add_extra_outputs(frank-wolf3d)
