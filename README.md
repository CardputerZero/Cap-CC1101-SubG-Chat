# Cap-CC1101-SubG-Chat

Runtime-only Sub-GHz chat app for M5Stack CardputerZero and the Cap CC1101 accessory.

## Features

- Send and receive messages over the verified 868 MHz low-rate 2-FSK profile (not LoRa)
- Show received signal strength and link-quality metadata
- Keep up to 64 messages in memory for the current session only
- Accept messages up to 61 printable ASCII bytes
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
- `R` or Enter on the Info page: retry radio initialization after an error

## Hardware

The device build uses the Cap CC1101 on `/dev/spidev0.1` with kernel-managed
CS1. Before radio initialization, the app checks the SPI node and, when needed,
loads the BSP-provided
`/boot/firmware/overlays/spi0-spidev2-gpio22-overlay.dtbo`. The overlay remains
loaded until reboot.

The Debian package launches this hardware app as root through a non-interactive,
command-specific sudo rule for members of the `gpio` group. The rule permits
only the installed binary with no command arguments. This is currently needed
for the Cap power controls exposed through the LED-class interface.

The current BSP may fail to apply the overlay at runtime. If initialization
still reports a missing SPI node after loading, reboot once with
`dtoverlay=spi0-spidev2-gpio22-overlay` in `/boot/firmware/config.txt`.
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
