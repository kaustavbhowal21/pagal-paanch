@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo =========================================================================
echo   Battle Tanks: Verilator + SDL2 Real-Time Simulation (Windows)
echo =========================================================================

:: Check if verilator is directly in PATH
where verilator >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Detected Verilator in PATH. Building with native Make...
    make -j4
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Build failed.
        pause
        exit /b %ERRORLEVEL%
    )
    if exist battle_tanks_sim.exe (
        battle_tanks_sim.exe
    ) else (
        battle_tanks_sim
    )
    goto :eof
)

:: Check if MSYS2 exists at default location
if exist "C:\msys64\msys2_shell.cmd" (
    echo [INFO] Detected MSYS2 installation at C:\msys64.
    echo Launching build inside MSYS2 UCRT64 environment...
    call C:\msys64\msys2_shell.cmd -ucrt64 -defterm -here -no-start -c "./build_and_run.sh"
    goto :eof
)

:: Check if WSL is available
where wsl >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    echo [INFO] Detected WSL (Windows Subsystem for Linux).
    echo Launching build inside WSL...
    wsl bash ./build_and_run.sh
    goto :eof
)

echo [ERROR] Neither native Verilator, MSYS2, nor WSL was found.
echo.
echo To run this real-time simulator on Windows:
echo.
echo Option A (Recommended - MSYS2 for native Windows performance):
echo   1. Install MSYS2 from https://www.msys2.org
echo   2. Open MSYS2 UCRT64 terminal and run:
echo        pacman -S mingw-w64-ucrt-x86_64-verilator mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-gcc make
echo   3. Navigate here and run:
echo        make run
echo.
echo Option B (WSL2 with WSLg graphics):
echo   1. In PowerShell: wsl --install
echo   2. Inside Ubuntu terminal:
echo        sudo apt update ^&^& sudo apt install -y verilator libsdl2-dev build-essential
echo   3. Navigate here: cd /mnt/c/.../sim_verilator
echo   4. Run: ./build_and_run.sh
echo.
pause

