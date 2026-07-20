# Background

## Why this project exists

The Roland TD-12 and TD-20 electronic drum modules use a custom 240x64 matrix LCD. Over time the original panel can fade or fail. Replacing it is not as simple as swapping a generic display as the modules send a semi-custom SPI 4 wire data stream to an LCD driver board that interacts with a pair of Epson SED1565 (the actual LCD drivers). 

The bigger issue, for myself, is that a used TD-12 or TD-12 with a faded screen can be found for about 100AUD (when you are lucky) but replacement screens (either on AliExpress or the TD1220R (or whatever is called)) are around 250AUD (minimum). The former using SED1565 clones (i.e. true clones) and the latter being a modern replacement.

This repository started as a way to understand how the TD-12/20's display system works which will (and has) given me insight on how to best approach the development of a custom embedded replacement solution (while ensuring it is cheap and replicatable by laymen). This repo provides a working decoder and viewer that runs on any computer (with Linux or WSL) connected to any cheapo logic analyser with atleast 4 channels and a 3.125MHz sampling frequency that can use sigrok. The longer-term goal is a drop-in replacement system that re-encodes the framebuffer/data stream for a modern controller/display (I am looking to use an STM32 and a SSD1322 OLED).

## What we learned about the hardware

### Dual-die layout

The visible panel is **240 pixels wide by 64 rows**, but it is built from **two SED1565 dies**, each managing 132 columns of segment drivers:

- **CS high** selects the **left** die.
- **CS low** selects the **right** die.

Each die exposes display data RAM (DDRAM) as vertical 8-pixel strips per byte, organised in pages (row blocks). Commands set the active page and column address; data bytes paint eight rows at the current column, then the column pointer auto-increments.

Physically, the segment wiring is **mirrored** within each die. When building a single 240-wide image, column `x` on a die maps from DDRAM index `131 - x`. The two 132-column dies are placed side by side (264 pixels total), then a **24-pixel horizontal crop** (`LEFT_SHIFT = 24`) yields the 240-pixel viewport the musician sees.

### Serial protocol (summary)

| Line | Role |
|------|------|
| MOSI | Data, MSB first |
| SCLK | Clock; bits sampled on rising edge |
| A0 | Command (0) vs display data (1) |
| CS | Left die (1) vs right die (0) |

The Roland firmware sends command sequences (page set, column set, display on/off, bias, and so on) interleaved with bursts of display data. For emulation we only need commands that affect DDRAM addressing and the data bytes themselves; other commands are ignored for raster purposes because they do not change pixel memory.

## Reverse-engineering approach

### Logic analyser first

Rather than guess from the datasheet alone, the bus was captured with a **Saleae Logic 16** and **sigrok-cli** (I later realised that the cheapo 9AUD logic analyser I had laying around would have worked fine as well).

Key practical findings:

1. **Two bytes per sample:** sigrok emits little-endian `uint16` samples even when only four channels are enabled. The signal bits live in the low byte; the high byte is zero. Treating each byte as a sample corrupts the decode.

2. **Independent shift registers per die:** CS gates which die receives clocked bits. A byte partially clocked to one die must not be disturbed when the other die is selected.

3. **Clock idle resync:** SCLK runs in bursts. If the clock stops long enough mid-byte (~100 samples at 25 MHz, roughly 4 µs without an edge), partial bytes are discarded and bit alignment resets. This matches observed gaps between command and data phases.

4. **A0 latched on the eighth rising edge:** The SED1565 evaluates A0 when the final bit of a byte is clocked in.

### Validation loop

1. Capture a burst while the TD-12/20 UI is visible.
2. Pipe live sigrok output into `td_12_lcd_emulator` and confirm the on-screen image matches the module display.

This loop made it possible to iterate on mirroring, CS routing, and stride handling without hardware on the bench for every change.

## Relationship to a future replacement

The portable core in `epson_sed_1565_core.h` is intentionally free of STL and dynamic allocation so it can compile on an **STM32** (or similar) later. The intended production path is:

1. **Today:** deserialise logic-analyser samples with `DualDecoder::feed_sample` (desktop tools).
2. **Future:** attach an MCU as SPI slave on the same bus, call `sed1565_write_byte` directly from completed bytes, maintain `chips[].ddram` as the framebuffer, and drive a replacement panel (for example via SSD1322).

Documentation for that hardware design will be added to this repository as the replacement project matures.

## References

- Epson SED1565 datasheet (command set, page/column addressing, serial timing).
- [sigrok](https://sigrok.org/) and [sigrok-cli](https://sigrok.org/wiki/Sigrok-cli) for capture and binary export.
- Roland TD-12 / TD-20 service documentation (module disassembly and LCD connector pinout) where available.

## Contributing context

 See [CONTRIBUTING.md](../CONTRIBUTING.md) for pull requests or opening issues.
