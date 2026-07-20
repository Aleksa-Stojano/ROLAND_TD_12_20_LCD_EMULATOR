// Roland TD-12 LCD emulator / SED1565 dual-die decoder (desktop viewer).
//
// The display is 240x64 driven by two SED1565 dies (CS selects between them:
// CS high = left half, CS low = right half). See epson_sed_1565_core.h for the
// portable, STM32-ready decoder core; this file is only the desktop transport
// (sigrok over stdin) and the SDL render of the stitched viewport.

#include <SDL2/SDL.h>
#include <iostream>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdint>

#include "epson_sed_1565_core.h"

// The Saleae Logic 16 is a 16-channel device, so sigrok streams samples as
// little-endian uint16 (2 bytes each) even when only channels 0..3 are enabled.
// Capture pipeline:
//   sigrok-cli -d saleae-logic16 --config samplerate=25m --channels 0,1,2,3 --continuous -O binary | ./lcd_visualiser
// Signals live in the low byte (bit0=MOSI, bit1=SCLK, bit2=A0, bit3=CS); the
// high byte is always 0x00. Reading 1 byte/sample here would inject a phantom
// SCLK edge on every 0x00 high byte and desynchronise the bitstream.
static const int SAMPLE_STRIDE = 2;  // bytes per logic sample

static const int VIEW_WIDTH   = 240;
static const int VIEW_HEIGHT  = 64;
static const int TOTAL_BUFFER = SED1565_CHIP_WIDTH * 2;  // 264 px across both dies
static const int LEFT_SHIFT   = 24;                      // crop offset into the 264 px
static const int SCALE        = 4;

std::deque<uint8_t> data_buffer;
std::mutex buffer_mutex;
std::atomic<bool> app_running(true);
std::atomic<bool> stream_completed(false);

void ingest_stream() {
    uint8_t chunk[65536];
    while (app_running && !stream_completed) {
        size_t bytes_read = fread(chunk, 1, sizeof(chunk), stdin);
        if (bytes_read > 0) {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            data_buffer.insert(data_buffer.end(), chunk, chunk + bytes_read);
        } else {
            if (feof(stdin)) {
                stream_completed = true;
            }
        }
    }
}

int main(int /*argc*/, char* /*argv*/[]) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "SDL initialisation error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Roland Dual-Die Matrix Viewport",
                                          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          VIEW_WIDTH * SCALE, VIEW_HEIGHT * SCALE, SDL_WINDOW_SHOWN);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, VIEW_WIDTH, VIEW_HEIGHT);

    DualDecoder decoder;
    decoder.reset();

    std::thread io_thread(ingest_stream);

    uint32_t pixels[VIEW_HEIGHT * VIEW_WIDTH];
    SDL_Event e;

    while (app_running) {
        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT) {
                app_running = false;
            }
        }

        bool data_mutated = false;
        std::vector<uint8_t> processing_chunk;

        {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            // Pull an even number of bytes so a uint16 sample is never split
            // across ticks; a dangling odd byte stays for the next pass.
            size_t available_samples = data_buffer.size() / SAMPLE_STRIDE;
            size_t samples_to_take = std::min((size_t)131072, available_samples);
            size_t bytes_to_take = samples_to_take * SAMPLE_STRIDE;
            if (bytes_to_take > 0) {
                processing_chunk.assign(data_buffer.begin(),
                                        data_buffer.begin() + bytes_to_take);
                data_buffer.erase(data_buffer.begin(),
                                  data_buffer.begin() + bytes_to_take);
            }
        }

        for (size_t i = 0; i + SAMPLE_STRIDE <= processing_chunk.size(); i += SAMPLE_STRIDE) {
            // Little-endian uint16 sample; all signals are in the low byte.
            uint8_t sample = processing_chunk[i];
            if (decoder.feed_sample(sample)) {
                data_mutated = true;
            }
        }

        if (data_mutated) {
            // Stitch the two dies into a 264px-wide track. Each die's segments
            // are physically mirrored, hence 131 - x.
            uint8_t full_track[SED1565_CHIP_HEIGHT][TOTAL_BUFFER] = {{0}};
            for (int y = 0; y < SED1565_CHIP_HEIGHT; ++y) {
                for (int x = 0; x < SED1565_CHIP_WIDTH; ++x) {
                    full_track[y][x]                    = decoder.chips[1].ddram[y][131 - x];
                    full_track[y][x + SED1565_CHIP_WIDTH] = decoder.chips[0].ddram[y][131 - x];
                }
            }

            for (int y = 0; y < VIEW_HEIGHT; ++y) {
                for (int x = 0; x < VIEW_WIDTH; ++x) {
                    int src_x = x + LEFT_SHIFT;
                    uint32_t colour = 0xFF000000;
                    if (src_x < TOTAL_BUFFER && full_track[y][src_x]) {
                        colour = 0xFFFFFFFF;
                    }
                    pixels[y * VIEW_WIDTH + x] = colour;
                }
            }

            SDL_UpdateTexture(texture, nullptr, pixels, VIEW_WIDTH * sizeof(uint32_t));
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        } else {
            SDL_Delay(1);
        }
    }

    if (io_thread.joinable()) {
        io_thread.join();
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
