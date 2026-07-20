# Roland TD-12/20 LCD Emulator

Decode and display the Roland dual-Epson-SED1565 LCD from logic-analyser captures.

> **Status:** Desktop emulator and research tool. An embedded hardware replacement for a fading display is in the works.

The Roland TD-12 and TD-20 use a 240x64 matrix LCD built from two Epson SED1565 controller dies. This repository provides a decoder for that serial protocol and a tool to visualise the panel on a computer while the display is probed with a logic analyser.

## Quick start

**Dependencies:** a C++17 compiler, SDL2 (via `pkg-config`), and [sigrok-cli](https://sigrok.org/) with a Saleae Logic 16 for live capture.

On Arch Linux / CachyOS:

```bash
sudo pacman -S sdl2 base-devel
```

Build:

```bash
make
```

Live capture (Saleae Logic 16, channels 0 to 3, 25 MHz sample rate):

```bash
sigrok-cli -d saleae-logic16 --config samplerate=25m --channels 0,1,2,3 --continuous -O binary \
  | ./td_12_lcd_emulator
```

An SDL window titled **Roland Dual-Die Matrix Viewport** should appear during live capture (960x256 pixels, 4x scale of the 240x64 panel).

## Documentation

- [Usage guide](docs/usage.md): prerequisites, build, capture setup, troubleshooting.
- [Background](docs/background.md): why this project exists and how the display was reverse-engineered.
- [Architecture](docs/architecture.md): dual-die layout, decoder core, data flow, future STM32 path.
- [Changelog](CHANGELOG.md): version history.

## Repository layout

| File | Purpose |
|------|---------|
| `epson_sed_1565_core.h` | Portable decoder core (no STL, suitable for embedded use) |
| `td_12_lcd_emulator.cpp` | SDL live viewer (sigrok binary on stdin) |
| `Makefile` | Builds the emulator |

## Licence

This project is licensed under the GNU General Public License v3.0. See [LICENSE](LICENSE).
