// Portable SED1565 dual-die decoder core.
//
// This header has no STL and no dynamic allocation, so it compiles unchanged
// on an STM32. Two usage paths:
//
//   1. Logic-capture / emulator path (this desktop project):
//        DualDecoder dec; dec.reset();
//        for each sample byte s (bit0=MOSI,bit1=SCLK,bit2=A0,bit3=CS):
//            dec.feed_sample(s);
//
//   2. Real STM32 path (preferred once ported): skip the bit deserialiser and
//      drive the DDRAM directly from the SPI-slave peripheral, reading A0/CS as
//      GPIOs, by calling:
//        sed1565_write_byte(&dec.chips[cs], byte, a0);
//      The resulting chips[].ddram is the framebuffer you re-encode for the
//      SSD1322.
//
// Display geometry: the Roland panel is 240x64 built from two SED1565 dies.
// CS high selects the left die, CS low the right die. Serial protocol: data is
// clocked on the SCLK rising edge, MSB first; A0 = 0 is a command byte, A0 = 1
// is display-data RAM. A data byte is a vertical 8-pixel strip in the current
// page (bit0 = top pixel, bit7 = bottom); the column pointer auto-increments.

#ifndef SED1565_CORE_H
#define SED1565_CORE_H

#include <stdint.h>

static const int SED1565_CHIP_WIDTH  = 132;  // SEG0..SEG131
static const int SED1565_CHIP_HEIGHT = 65;   // 64 dot rows + 1 indicator line

struct Sed1565 {
    uint8_t ddram[SED1565_CHIP_HEIGHT][SED1565_CHIP_WIDTH];
    uint8_t page;      // current page (row block), 0..8
    uint8_t col;       // current column pointer, auto-increments on data write
    uint8_t col_high;  // latched high nibble of the column address
    uint8_t col_low;   // latched low nibble of the column address

    // Serial deserialiser state. Each die keeps its own shift register because
    // CS gates the two dies independently; a byte in flight to one die must not
    // be disturbed while the other die is being clocked.
    uint8_t shift_reg;
    int8_t  bit_idx;   // next bit position to fill (7 -> 0, MSB first)
};

static inline void sed1565_reset_serial(Sed1565* c) {
    c->shift_reg = 0;
    c->bit_idx   = 7;
}

// Execute one fully-deserialised byte against a die.
// a0 == 0 -> command/register, a0 == 1 -> display-data RAM write.
static inline void sed1565_write_byte(Sed1565* c, uint8_t b, uint8_t a0) {
    if (a0 == 0) {
        if (b >= 0xB0 && b <= 0xB8) {
            // Page address set (row block). Does not touch the column pointer.
            c->page = (uint8_t)(b - 0xB0);
        } else if (b >= 0x10 && b <= 0x1F) {
            // Column address, high nibble.
            c->col_high = b & 0x0F;
            c->col = (uint8_t)((c->col_high << 4) | c->col_low);
        } else if (b <= 0x0F) {
            // Column address, low nibble.
            c->col_low = b & 0x0F;
            c->col = (uint8_t)((c->col_high << 4) | c->col_low);
        }
        // Other commands (display on/off, ADC select, start line, bias, etc.)
        // do not change DDRAM contents, so they are ignored for the raster.
    } else {
        for (int bit = 0; bit < 8; ++bit) {
            int y = (c->page * 8) + bit;
            if (c->col < SED1565_CHIP_WIDTH && y < SED1565_CHIP_HEIGHT) {
                c->ddram[y][c->col] = (uint8_t)((b >> bit) & 1);
            }
        }
        if (c->col < SED1565_CHIP_WIDTH) {
            c->col++;  // hardware auto-increments the column pointer
        }
    }
}

// One physical SCLK/CS pair feeds both dies. The clock is gated on/off in
// bursts; if it stalls mid-byte we resync the bit index to a byte boundary.
// At 25 MHz sampling / 3.125 MHz SCLK there are ~8 samples per bit, so a level
// only holds ~4 samples inside a byte. 100 samples (~4 us) of no edge is a safe
// "the clock stopped" threshold that never trips inside a byte.
#ifndef SED1565_IDLE_RESET_SAMPLES
#define SED1565_IDLE_RESET_SAMPLES 100
#endif

struct DualDecoder {
    // Index 1 = CS high (left die), index 0 = CS low (right die).
    Sed1565 chips[2];
    uint8_t last_sclk;
    int     idle_count;

    void reset() {
        for (int i = 0; i < 2; ++i) {
            for (int y = 0; y < SED1565_CHIP_HEIGHT; ++y)
                for (int x = 0; x < SED1565_CHIP_WIDTH; ++x)
                    chips[i].ddram[y][x] = 0;
            chips[i].page = 0;
            chips[i].col = 0;
            chips[i].col_high = 0;
            chips[i].col_low = 0;
            sed1565_reset_serial(&chips[i]);
        }
        last_sclk = 0;
        idle_count = 0;
    }

    // Feed one deserialised sample byte (signals in bits 0..3):
    //   bit0 = MOSI, bit1 = SCLK, bit2 = A0, bit3 = CS.
    // Returns true if a display-data byte was written (frame changed).
    bool feed_sample(uint8_t s) {
        uint8_t mosi = (s >> 0) & 1;
        uint8_t sclk = (s >> 1) & 1;
        uint8_t a0   = (s >> 2) & 1;
        uint8_t cs   = (s >> 3) & 1;
        bool mutated = false;

        if (sclk == last_sclk) {
            if (++idle_count == SED1565_IDLE_RESET_SAMPLES) {
                // Clock has stalled: discard any partial bytes on both dies.
                sed1565_reset_serial(&chips[0]);
                sed1565_reset_serial(&chips[1]);
            }
        } else {
            idle_count = 0;
        }

        if (sclk == 1 && last_sclk == 0) {
            // Route the bit to the die currently selected by CS; the other die
            // keeps its partial-byte state untouched.
            Sed1565* c = &chips[cs & 1];
            if (mosi) {
                c->shift_reg |= (uint8_t)(1 << c->bit_idx);
            }
            c->bit_idx--;
            if (c->bit_idx < 0) {
                // Byte complete. Per the SED1565 datasheet A0 is evaluated on
                // this (8th) rising edge, so we use the A0/CS latched here.
                sed1565_write_byte(c, c->shift_reg, a0);
                sed1565_reset_serial(c);
                if (a0 == 1) {
                    mutated = true;
                }
            }
        }
        last_sclk = sclk;
        return mutated;
    }
};

#endif // SED1565_CORE_H
