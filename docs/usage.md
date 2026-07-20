# Usage guide

This guide covers building the emulator, capturing LCD traffic with a logic analyser, and running the live viewer.

## Prerequisites

### Build tools

- C++17 compiler (`g++` or `clang++`)
- GNU Make
- SDL2 development libraries

On Arch Linux / CachyOS:

```bash
sudo pacman -S sdl2 base-devel
```

On Debian / Ubuntu:

```bash
sudo apt install build-essential libsdl2-dev pkg-config
```

On others

Use an LLM lol (copy and paste the above and say you are looking for the above for your specific system)

### Capture hardware

- [sigrok-cli](https://sigrok.org/wiki/Downloads)
- Cheapo 8 channel 24MHz digital logic analyser (I used an expensive Saleae Logic 16 (I later realised I did not require it)
- Probes on the LCD serial bus: MOSI, SCLK, A0, CS. These will be pins 5,6,7,8 (when looking at the mainboard ribbon cable connector).
- (Please download the roland TD-12 service manual which is readily available online. It will help you troubleshoot pinouts. I will add images and videos in due time but, for now, reference disassembly videos on YouTube (if you dont know what you are doing)).

The emulator expects sigrok **binary** output (`-O binary`), not CSV or other formats.

## Build

From the repository root:

```bash
make          # build td_12_lcd_emulator
make clean    # remove built binary
```

The binary is written to the repository root and is listed in `.gitignore`.

## Signal mapping

Each logic sample is a little-endian 16-bit value from the Saleae Logic 16. Only the **low byte** carries signal data:

| Bit | Signal | Description |
|-----|--------|-------------|
| 0 | MOSI | Serial data (MSB first) |
| 1 | SCLK | Serial clock (data latched on rising edge) |
| 2 | A0 | 0 = command, 1 = display data |
| 3 | CS | Chip select (high = left die, low = right die) |

The high byte is always `0x00`. The emulator reads **2 bytes per sample** and uses only the first byte. Reading one byte per sample would treat each `0x00` high byte as a phantom clock edge and desynchronise the bitstream.

The capture settings I used:

- Sample rate: **25 MHz** (`samplerate=25m`) (lower rates should work fine, just make sure they are integer multiples of 3.125MHz as this is the speed of the sclk)
- Channels: **0, 1, 2, 3** mapped to MOSI, SCLK, A0, CS respectively
- Output: **binary** (`-O binary`)

## Live capture workflow

1. Connect probes to the LCD serial lines on the TD-12 or TD-20 (see [background.md](background.md) for context).
2. Build the emulator: `make`
3. Start sigrok and pipe into the viewer:

```bash
sigrok-cli -d saleae-logic16 --config samplerate=25m --channels 0,1,2,3 --continuous -O binary \
  | ./td_12_lcd_emulator
```

(change 25m to 12.5m or 6.25m or whatever value that you analyser has that is an integer multiple of 3.125MHz)

4. Turn on your drum module. You see the Roland boot screen. Operate the module so the LCD updates. The SDL window refreshes when new display data is decoded.
5. Close the window or press the window manager close control to exit.

The viewer reads from **stdin** in a background thread while the main thread decodes samples and renders. No command-line arguments are required.

## Saving captures

To record a burst for later analysis:

```bash
sigrok-cli -d saleae-logic16 --config samplerate=25m --channels 0,1,2,3 -O binary \
  > capture.bin
```

## Troubleshooting

### Blank or static window

- Confirm probes are on MOSI, SCLK, A0, and CS and that the module LCD is updating.
- Check channel order matches the table above.
- Verify sample rate is an integer multiple of 3.125. When in doubt use 25 MHz (this may require investing in an more expensive analyser). Ensure output format is binary

### SDL initialisation error

- Install SDL2 development packages and confirm `pkg-config --libs sdl2` succeeds.
- Run from a graphical session (the viewer needs a display).

### `make` fails on SDL

- Install `libsdl2-dev` (Debian) or `sdl2` (Arch).

## Related reading

- [Background](background.md): motivation and reverse-engineering narrative
- [Architecture](architecture.md): how the dual dies are stitched into a 240x64 image
