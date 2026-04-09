# frank-wolf3d

Wolfenstein 3D for Raspberry Pi Pico 2 (RP2350) with HDMI output, SD card, PS/2 or USB keyboard/mouse, NES gamepad, and OPL music.

## Supported Boards

This firmware is designed for RP2350-based boards with integrated HDMI, SD card, PS/2 (or USB), and PSRAM:

- **[FRANK](https://rh1.tech/projects/frank?area=about)** -- A versatile development board based on RP Pico 2, HDMI output, and extensive I/O options.
- **[Murmulator](https://murmulator.ru)** -- A compact retro-computing platform based on RP Pico 2, designed for emulators and classic games.

Both boards provide all necessary peripherals out of the box -- no additional wiring required.

## Features

- Native 320x200 HDMI video output with triple-buffered vsync
- Full OPL2 music emulation
- 8MB QSPI PSRAM support for game data
- SD card support for game files and savegames
- PS/2 and USB keyboard and mouse input
- NES gamepad support (GPIO bit-bang, no PIO required)
- I2S audio output via PIO + DMA ping-pong
- Sound effects (AdLib, PC speaker) and digitized sounds

## Hardware Requirements

- **Raspberry Pi Pico 2** (RP2350) or compatible board
- **8MB QSPI PSRAM** (mandatory)
- **HDMI connector** (directly connected via resistors, no HDMI encoder needed)
- **SD card module** (SPI mode)
- **PS/2 or USB keyboard and mouse**
- **I2S DAC module** (e.g., TDA1387) for audio output
- **NES gamepad** (optional)

### PSRAM Options

This project requires 8MB PSRAM. You can obtain PSRAM-equipped hardware in several ways:

1. **Solder a PSRAM chip** on top of the Flash chip on a Pico 2 clone (SOP-8 flash chips are only available on clones, not the original Pico 2)
2. **Build a [Nyx 2](https://rh1.tech/projects/nyx?area=nyx2)** -- a DIY RP2350 board with integrated PSRAM
3. **Purchase a [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107)** -- a ready-made Pico 2 with 8MB PSRAM

## Board Configurations

Two GPIO layouts are supported: **M1** and **M2**. The PSRAM pin is auto-detected based on chip package:
- **RP2350B**: GPIO47 (both M1 and M2)
- **RP2350A**: GPIO19 (M1) or GPIO8 (M2)

### HDMI (via 270 Ohm resistors)
| Signal | M1 GPIO | M2 GPIO |
|--------|---------|---------|
| CLK-   | 6       | 12      |
| CLK+   | 7       | 13      |
| D0-    | 8       | 14      |
| D0+    | 9       | 15      |
| D1-    | 10      | 16      |
| D1+    | 11      | 17      |
| D2-    | 12      | 18      |
| D2+    | 13      | 19      |

### SD Card (SPI mode)
| Signal  | M1 GPIO | M2 GPIO |
|---------|---------|---------|
| CLK     | 2       | 6       |
| CMD     | 3       | 7       |
| DAT0    | 4       | 4       |
| DAT3/CS | 5       | 5       |

### PS/2 Keyboard
| Signal | M1 GPIO | M2 GPIO |
|--------|---------|---------|
| CLK    | 0       | 2       |
| DATA   | 1       | 3       |

### PS/2 Mouse
| Signal | M1 GPIO | M2 GPIO |
|--------|---------|---------|
| CLK    | 14      | 0       |
| DATA   | 15      | 1       |

### NES Gamepad
| Signal | M1 GPIO | M2 GPIO |
|--------|---------|---------|
| CLK    | 20      | 20      |
| LATCH  | 21      | 21      |
| DATA   | 22      | 26      |

### I2S Audio
| Signal | M1 GPIO | M2 GPIO |
|--------|---------|---------|
| DATA   | 26      | 9       |
| BCLK   | 27      | 10      |
| LRCLK  | 28      | 11      |

## Building

### Prerequisites

1. Install the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (version 2.0+)
2. Set environment variable: `export PICO_SDK_PATH=/path/to/pico-sdk`
3. Install ARM GCC toolchain

### Build Steps

```bash
# Clone the repository
git clone https://github.com/rh1tech/frank-wolf3d.git
cd frank-wolf3d

# Build for M1 layout (default)
mkdir build && cd build
cmake -DBOARD_VARIANT=M1 ..
make -j$(nproc)

# Build for M2 layout
cmake -DBOARD_VARIANT=M2 ..
make -j$(nproc)
```

### Build Options

| Option | Description |
|--------|-------------|
| `-DBOARD_VARIANT=M1` | Use M1 GPIO layout (default) |
| `-DBOARD_VARIANT=M2` | Use M2 GPIO layout |
| `-DCPU_SPEED=504` | CPU overclock in MHz (252, 378, 504) |
| `-DPSRAM_SPEED=133` | PSRAM speed in MHz (84, 100, 133, 166) |
| `-DFLASH_SPEED=88` | Flash speed in MHz (66, 88) |
| `-DUSB_HID_ENABLED=1` | Enable USB HID host (keyboard, mouse, gamepad) |

### Release Builds

To build both M1 and M2 variants with version numbering:

```bash
./release.sh
```

### Flashing

```bash
# With device in BOOTSEL mode:
picotool load build/frank-wolf3d.uf2

# Or with device running:
picotool load -f build/frank-wolf3d.uf2
```

## SD Card Setup

1. Format an SD card as FAT32
2. Create a `wolf3d` folder on the SD card
3. Copy Wolfenstein 3D data files to the `wolf3d` folder:
   - Full version: `.wl6` files (VSWAP.WL6, MAPHEAD.WL6, etc.)
   - Shareware: `.wl1` files
4. Savegames are stored alongside the data files

## Controls

### Keyboard
- Arrow keys: Move/Turn
- Ctrl: Fire
- Alt: Strafe
- Shift: Run
- Space: Open doors/Use
- 1-4: Select weapon
- Escape: Menu
- Enter: Confirm

### Mouse
- Move left/right: Turn
- Left button: Fire
- Right button: Strafe
- Middle button: Open doors/Use

### NES Gamepad
- D-pad: Move (in-game) / Navigate (menus)
- A: Fire
- B: Strafe
- Start: Menu (Escape)
- Select: Confirm (Enter)

## License

GNU General Public License v2. See [LICENSE](LICENSE) for details.

This project is based on:
- [Wolf4SDL](https://github.com/11001011101001011/Wolf4SDL) -- SDL port of Wolfenstein 3D
- Original Wolfenstein 3D engine by id Software (John Carmack, John Romero, et al.)
- [Murmulator](https://murmulator.ru) PS/2 keyboard driver by Murmulator team
- [EMU8950](https://github.com/digital-sound-antiques/emu8950) by Mitsutaka Okazaki (OPL2 emulator)

Third-party components under compatible licenses:
- NES gamepad driver (MIT, based on [pico-infonesPlus](https://github.com/fhoedemakers/pico-infonesPlus))
- SD card driver (BSD-2-Clause, based on [pico_fatfs_test](https://github.com/elehobica/pico_fatfs_test))
- FatFS (permissive, by ChaN)
- PIO SPI (BSD-3-Clause, Raspberry Pi Foundation)

## Author

Mikhail Matveev <xtreme@rh1.tech>

## Acknowledgments

- id Software for the original Wolfenstein 3D
- The Wolf4SDL team for the portable SDL source port
- The Murmulator team for PS/2 keyboard and HDMI drivers
- The Raspberry Pi Foundation for the RP2350 and Pico SDK
