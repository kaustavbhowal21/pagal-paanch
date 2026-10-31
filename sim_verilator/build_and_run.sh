#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "================================================================="
echo "  Battle Tanks: Building Verilator + SDL2 Hardware Simulation    "
echo "================================================================="

# Check Verilator
if ! command -v verilator &> /dev/null; then
    echo "[ERROR] 'verilator' not found!"
    echo "  On Ubuntu/Debian (WSL): sudo apt update && sudo apt install verilator libsdl2-dev build-essential"
    echo "  On MSYS2: pacman -S mingw-w64-ucrt-x86_64-verilator mingw-w64-ucrt-x86_64-SDL2 make mingw-w64-ucrt-x86_64-gcc"
    exit 1
fi

# Check SDL2
if ! pkg-config --exists sdl2 && ! command -v sdl2-config &> /dev/null && [ ! -f /usr/include/SDL2/SDL.h ]; then
    echo "[WARNING] SDL2 development package may not be installed."
    echo "  Install with: sudo apt install libsdl2-dev (Linux/WSL) or pacman -S mingw-w64-ucrt-x86_64-SDL2 (MSYS2)"
fi

echo "[1/2] Building simulation executable..."
make -j$(nproc 2>/dev/null || echo 4)

echo "[2/2] Launching Battle Tanks Interactive Game Window..."
if [ -f "./battle_tanks_sim.exe" ]; then
    ./battle_tanks_sim.exe
else
    ./battle_tanks_sim
fi

