# Cap-CC1101-SubG-Chat

Runtime-only Sub-GHz chat app for M5Stack CardputerZero and the Cap CC1101 accessory.

## Features

- Send and receive messages over low-rate 2-FSK with selectable frequency and transmit power
- Acknowledge and retry messages automatically, while suppressing retransmission duplicates
- Show received signal strength and link-quality metadata
- Keep up to 64 messages in memory for the current session only
- Accept messages up to 56 printable ASCII bytes
- Use an SDL mock radio for desktop development without CC1101 hardware

## Dependencies

Run the bootstrap script once after cloning this repository:

```bash
./bootstrap.sh
```

It fetches `lvgl`, `spdlog`, and `smooth_ui_toolkit` under `dependencies/`.
SDL builds require SDL2 development files. CardputerZero builds additionally
require libgpiod development headers and library.

## Build

For SDL desktop testing:

```bash
cmake -S . -B build/sdl -DCC1101_CHAT_USE_SDL=ON
cmake --build build/sdl -j8
```

For a native CardputerZero build:

```bash
cmake -S . -B build/cp0 -DCC1101_CHAT_USE_SDL=OFF
cmake --build build/cp0 -j8
```

The output binary is `dist/M5CardputerZero-Cap-CC1101-SubG-Chat`.

## Usage

Run the SDL build with:

```bash
CC1101_CHAT_SDL_ZOOM=2 ./dist/M5CardputerZero-Cap-CC1101-SubG-Chat
```

Key controls:

- `Z`/`C` or Left/Right: switch between Messages and CC1101 Info
- `F`/`X` or Up/Down: scroll messages
- Enter or a printable key: open the message editor
- Enter sends; Esc cancels or exits; Backspace/Delete and Left/Right edit
- `R` on Info or Enter in the error dialog: retry radio initialization
- `Fn+H` (`H` on desktop): open Help; Esc closes it
- `S` on the Info page: open Radio settings

The radio regulations notice appears on launch. Press `7` or Space to toggle
**Don't show again**, then Enter to continue. Both radio settings start unset.
Press `F`/`X` or Up/Down to switch fields, `Z`/`C` or Left/Right to choose a
preset, and Enter to save. You can skip setup with Esc and open it later from
Info. The app starts the radio only after both settings have been saved.

Frequency presets are 315.000, 433.920, 868.000, and 915.000 MHz. TX power
presets are -30, -20, -15, -10, 0, 5, 7, and 10 dBm. Use the antenna
supplied for the selected band and follow the local limits for point-to-point
operation. [Cap CC1101 specifications](https://docs.m5stack.com/en/cap/Cap_CC1101)

## Hardware

The device build uses the Cap CC1101 on `/dev/spidev0.1` with kernel-managed
CS1. It does not require the CS2/GPIO22 overlay used by the Cap's NFC controller.
On exit, the app puts the radio to sleep and restores the G14/G15 pinmux.
GPIO26 POWER_EN and EXT5V remain enabled: the Cap shares SPI with the LCD,
and cutting its I/O supply can freeze the LCD while framebuffer writes still
succeed. Supply shutdown belongs to the system, not an individual Cap app.

The Debian package launches this hardware app as root through a non-interactive,
command-specific sudo rule for members of the `gpio` group. The rule permits
only the installed binary with no command arguments. This is currently needed
for the Cap power controls exposed through the LED-class interface.

Hardware initialization errors are shown on the Info page.

## Package

Build the CardputerZero `arm64` Debian package on an x86 Linux or WSL2 host with
the Docker wrapper:

```bash
./packaging/docker/package_deb.sh
```

The wrapper downloads and caches the official CardputerZero BSP, cross-builds
the device backend, and writes the package to `dist/`. To package natively on a
CardputerZero instead, run:

```bash
./packaging/deb/package_deb.sh
```

The generated package is named:

```text
dist/m5cardputerzero-cap-cc1101-subg-chat_<version>_m5stack1_arm64.deb
```
