#=============================================================================
# Makefile for Battle Tanks Verilator + SDL2 Real-Time Simulation
#=============================================================================

VERILATOR ?= verilator
MAKE      ?= make

# Directory paths
ROOT_DIR  := ..
MOD_DIR   := $(ROOT_DIR)/modules
CPU_DIR   := $(MOD_DIR)/cpu
GE_DIR    := $(MOD_DIR)/game_engine
VGA_DIR   := $(MOD_DIR)/vga
AUD_DIR   := $(MOD_DIR)/audio

# Verilog include paths
INCLUDES  := -I. \
             -I$(MOD_DIR) \
             -I$(CPU_DIR) \
             -I$(GE_DIR) \
             -I$(VGA_DIR) \
             -I$(AUD_DIR)

# Verilog source files
V_SRCS    := sim_top.v \
             $(MOD_DIR)/top_fpga.v \
             $(CPU_DIR)/pipeline.v \
             $(CPU_DIR)/memory.v \
             $(CPU_DIR)/multiplier_dsp.v \
             $(CPU_DIR)/divider_radix2.v \
             $(CPU_DIR)/top_rv32m.v \
             $(GE_DIR)/collision_engine.v \
             $(GE_DIR)/cooldown_timers.v \
             $(GE_DIR)/sprite_hud_regfile.v \
             $(GE_DIR)/input_controller.v \
             $(GE_DIR)/maze_tile_ram.v \
             $(GE_DIR)/mmio_interconnect.v \
             $(VGA_DIR)/vga_timing_gen.v \
             $(VGA_DIR)/maze_tile_renderer.v \
             $(VGA_DIR)/sprite_engine.v \
             $(VGA_DIR)/health_hud.v \
             $(VGA_DIR)/pixel_priority_mixer.v \
             $(AUD_DIR)/sound_engine.v

# C++ test harness source
CPP_SRCS  := sim_main.cpp

# SDL2 compiler and linker flags
SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)
SDL2_LIBS   := $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null || echo -lSDL2)

# Verilator options
# -O3 for high simulation speed
# -Wno-fatal suppresses non-critical lint warnings
VERILATOR_FLAGS := --cc \
                   --exe \
                   -top-module sim_top \
                   $(INCLUDES) \
                   -Wno-fatal \
                   -Wno-WIDTH \
                   -Wno-CASEINCOMPLETE \
                   -Wno-COMBDLY \
                   -Wno-INITIALDLY \
                   -Wno-MULTIDRIVEN \
                   -Wno-UNOPTFLAT \
                   -Wno-PINMISSING \
                   -Wno-LATCH \
                   -Wno-TIMESCALEMOD \
                   -O3 \
                   --x-assign fast \
                   --x-initial fast \
                   --noassert \
                   -CFLAGS "-O3 -std=c++17 $(SDL2_CFLAGS)" \
                   -LDFLAGS "$(SDL2_LIBS)"

OBJ_DIR   := obj_dir
TARGET    := battle_tanks_sim

.PHONY: all clean run

all: $(TARGET)

$(OBJ_DIR)/Vsim_top.mk: $(V_SRCS) $(CPP_SRCS)
	@echo "=== [1/2] Verilating SystemVerilog RTL with Verilator ==="
	$(VERILATOR) $(VERILATOR_FLAGS) $(V_SRCS) $(CPP_SRCS)

$(TARGET): $(OBJ_DIR)/Vsim_top.mk
	@echo "=== [2/2] Compiling C++ Executable with GCC/Clang ==="
	$(MAKE) -C $(OBJ_DIR) -f Vsim_top.mk Vsim_top
	cp $(OBJ_DIR)/Vsim_top $(TARGET) || cp $(OBJ_DIR)/Vsim_top.exe $(TARGET).exe
	@echo "=== Build Complete: $(TARGET) is ready to run! ==="

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) $(TARGET).exe

