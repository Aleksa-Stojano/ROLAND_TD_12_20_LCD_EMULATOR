CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
LDFLAGS  ?=
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL2_LIBS   := $(shell pkg-config --libs sdl2)

EMULATOR = td_12_lcd_emulator

.PHONY: all clean

all: $(EMULATOR)

$(EMULATOR): td_12_lcd_emulator.cpp epson_sed_1565_core.h
	$(CXX) $(CXXFLAGS) $(SDL2_CFLAGS) -o $@ td_12_lcd_emulator.cpp $(LDFLAGS) $(SDL2_LIBS)

clean:
	rm -f $(EMULATOR)
