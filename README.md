# Hardware-Accelerated 2D Battle Tanks with Real-Time Reflection Physics on an RV32IM FPGA Soft-Core

**Course:** CSE Semester 3 - Hardware Laboratory Project  
**Group Number:** 3  
**Target Platform:** Digilent Nexys A7 (Xilinx Artix-7 XC7A100T-1CSG324C FPGA)  
**Display:** 640×480 @ 60 Hz VGA (12-bit DAC, 4096 colors)  
**Processor:** RV32IM Soft-Core (3-Stage Pipeline: IF/ID -> EX -> WB with Hardware MUL/DIV)  

---

## 1. System Overview

Battle Tanks is a two-player hardware-accelerated combat game running on a custom 32-bit RISC-V soft-core processor. The processor executes an embedded C game program responsible for high-level mechanics, fixed-point trigonometry, and projectile reflection physics. To achieve a rock-solid 60 FPS without software rendering bottlenecks, all compute-intensive and high-bandwidth tasks are offloaded to memory-mapped hardware accelerators:

1. **VGA Display Pipeline:** Dedicated 640×480 @ 60 Hz raster timing generator, tile renderer, 16-angle tank sprite ROMs, circular bullet disc masks, heart health HUD, and a pipelined pixel priority mixer.
2. **Hardware Collision Engine:** 28 parallel bullet-bullet AABB overlap cells, 16 bullet-tank overlap cells, an 8-clock time-shared bullet-wall scanner, and Write-1-to-Clear (W1C) sticky status registers.
3. **Hardware Cooldown Timers:** Shared 1 ms prescaler and 4 hardware countdown down-counters (0.5s primary fire cooldown, 20.0s special attack cooldown).
4. **Input Debouncers:** 12-channel 2-FF synchronizer and 10 ms debouncers for board buttons and switches.
5. **Mirrored Dual-Port Maze BRAM:** Two identical true-dual-port BRAMs (Copy V for VGA rasterization, Copy C for collision scanning), written simultaneously on Port A by the CPU to eliminate bus contention.

---

## 2. Categorized Directory Architecture

The project has been modularized into distinct subsystems:

```
battle-tanks/
├── README.md                      # Complete system documentation and user guide
├── run_sim.bat                    # 1-Click full-system build and simulation script
├── imem.hex / dmem.hex            # Compiled game machine code and data memory images
│
├── modules/
│   ├── top_fpga.v                 # FPGA Top-Level Integration (50 MHz sys, 25 MHz pix)
│   ├── constraints.xdc            # Nexys A7 Pin Assignments (Clk, Rst, BTNs, LEDs, VGA)
│   │
│   ├── cpu/                       # RV32IM Soft-Core Processor Core
│   │   ├── pipeline.v             # 3-Stage Pipeline Top (IF/ID, EX, WB)
│   │   ├── IF_ID.v                # Instruction Fetch & Decode, Imm Gen, Branch Flush
│   │   ├── execute.v              # ALU, Branch Comparator, M-Extension Handshake
│   │   ├── wb.v                   # Writeback Stage, Store/Load Byte Alignment
│   │   ├── memory.v               # 4 KB Instruction ROM & 4 KB Data RAM (FPGA BRAM)
│   │   ├── multiplier_dsp.v       # Single-Cycle Signed/Unsigned Multiplier (DSP48E1)
│   │   ├── divider_radix2.v       # 32-Cycle Non-Restoring Iterative Divider
│   │   ├── top_rv32m.v            # RV32M Extension Integration & Stall Logic
│   │   └── opcode.vh              # RISC-V Opcodes and Funct3/Funct7 Definitions
│   │
│   ├── game_engine/               # Hardware Accelerators & Peripherals
│   │   ├── mmio_interconnect.v    # Address Decode (addr[31]) & Registered Read Mux
│   │   ├── collision_engine.v     # AABB Cells, Bullet-Wall Scanner, W1C Sticky Hits
│   │   ├── cooldown_timers.v      # 1 ms Prescaler & 4 Hardware Countdown Timers
│   │   ├── sprite_hud_regfile.v   # 12x 32-bit Live Sprite & HUD MMIO Registers
│   │   ├── input_controller.v     # 12-Channel 2-FF Synchronizer & 10 ms Debouncers
│   │   └── maze_tile_ram.v        # Mirrored Dual-Port BRAMs (Copy V & Copy C)
│   │
│   ├── vga/                       # Video Display Subsystem (640x480 @ 60 Hz)
│   │   ├── vga_timing_gen.v       # 800x525 Raster Counters, HSync, VSync, Active
│   │   ├── maze_tile_renderer.v   # 20x20 Grid (24x24 px tiles), Wall/Floor Coloring
│   │   ├── sprite_engine.v        # 2 Tanks (16-angle ROM) & 8 Bullets (Disc Masks)
│   │   ├── health_hud.v           # 2x 20 Hearts Strip (8x8 Heart Shape ROM)
│   │   └── pixel_priority_mixer.v # Priority Chain & 2-FF Sync Delay Line
│   │
│   └── sim/                       # Simulation Testbenches
│       ├── tb_game_top.v          # Full Hardware-Software System Testbench
│       └── tb_pipeline.v          # Processor Core Execution Testbench
│
└── software/                      # Embedded C Game Project (RV32IM)
    ├── build.bat                  # Batch script to compile C code with GCC
    ├── build_and_export.py        # Python automated compiler & hex formatter
    ├── linker.ld                  # Linker Script for 4 KB IMEM + 4 KB DMEM
    ├── crt0.s                     # RISC-V Startup Assembly (Stack, BSS clear)
    ├── include/
    │   ├── mmio.h                 # Hardware MMIO Addresses, Bitfields, and Drivers
    │   ├── game.h                 # Game Entities (Tank, Bullet), Constants
    │   └── physics.h              # Trigonometry Tables, Angular Reflection Prototypes
    └── src/
        ├── main.c                 # Main Loop & Frame Pacing
        ├── game.c                 # Tank Steering, Cooldowns, Collision Damage, Display
        └── physics.c              # 2D Angular Reflection, Wall Shattering, Velocities
```

---

## 3. Hardware Architecture & Fixes Implemented

### A. Critical Pipeline Bug Fixes
1. **DIV Multi-Cycle Stall Writeback Suppression (`modules/cpu/execute.v`):**
   * *Problem:* In the original processor, `alu_to_reg` was asserted during multi-cycle division operations. During the 32 cycles a `DIV` was calculating, uncomputed garbage data was written back to the register file every clock edge.
   * *Fix:* Gated `alu_to_reg` with `!m_pipeline_stall` (per Fig P7c Part B), automatically injecting a clean NOP bubble into the writeback stage until the divider asserts `m_ready`.
2. **Missing `AUIPC` Instruction (`modules/cpu/IF_ID.v`, `execute.v`, `pipeline.v`):**
   * *Problem:* Standard GCC compiler binaries use `auipc` for PC-relative addressing (`gp`, tables). `AUIPC` (`7'b0010111`) was missing in the immediate generator, causing illegal instruction exceptions.
   * *Fix:* Added `AUIPC` U-type immediate formatting (`{instr[31:12], 12'b0}`), added `auipc` pipeline signal through `id_ex_reg`, and implemented `ex_result = pc + imm` in the EX stage.
3. **Multiplier Combinational Handshake (`modules/cpu/multiplier_dsp.v`):**
   * Changed `ready` to assert combinationally with `start` so that single-cycle `MUL` instructions execute cleanly at 50 MHz without unnecessary stall cycles.
4. **Header Scoping in SystemVerilog (`modules/cpu/opcode.vh`):**
   * Removed global file guards that blocked multi-module re-inclusion in separate module scopes. Moved include statements inside module definitions to isolate `localparam` namespaces.

### B. Hardware-Software MMIO Memory Map

| Address Range | Device / Register | Access | Description |
| :--- | :--- | :--- | :--- |
| `0x0000_0000 - 0x0000_0FFF` | **Data RAM (DMEM)** | R/W | 4 KB Data Memory (Stack, Globals, Heap) |
| `0x8000_0000` | `REG_P1_KEYS` | RO | P1 Debounced Buttons: `[0]` FWD, `[1]` BWD, `[2]` L, `[3]` R, `[4]` SHOOT, `[5]` SPEC |
| `0x8000_0004` | `REG_P2_KEYS` | RO | P2 Debounced Buttons: `[0]` FWD, `[1]` BWD, `[2]` L, `[3]` R, `[4]` SHOOT, `[5]` SPEC |
| `0x8000_0010` | `REG_TIMER_STATUS` | RO | Cooldown Status (1 = ready): `[0]` P1 Shoot, `[1]` P1 Spec, `[2]` P2 Shoot, `[3]` P2 Spec |
| `0x8000_0014` | `REG_TIMER_TRIGGER` | WO | Cooldown Trigger (same bit order as status): write `1` to start the 0.5 s primary or 20 s special timer |
| `0x8000_0020` | `REG_COLLISION_FLAG` | RO | Summary: `[0]` wall, `[1]` bullet-bullet, `[2]` tank hit |
| `0x8000_0024` | `REG_COLLISION_WALL` | R/W1C | Sticky Bullet-Wall Hits: bits `[7:0]` for bullets 0..7 |
| `0x8000_0028` | `REG_COLLISION_BULLET` | R/W1C | Sticky Bullet-Bullet Hits: bit `i` = bullet `i` overlapped another bullet |
| `0x8000_002C` | `REG_COLLISION_TANK` | R/W1C | Sticky Bullet-Tank Hits: `[7:0]` bullet `i` overlapped a tank, `[8]` P1 tank hit, `[9]` P2 tank hit |
| `0x8000_0040` | `REG_SPRITE_P1_TANK` | WO | Tank 1 position: `{6'd0, y[9:0], 6'd0, x[9:0]}` |
| `0x8000_0044` | `REG_SPRITE_P2_TANK` | WO | Tank 2 position: `{6'd0, y[9:0], 6'd0, x[9:0]}` |
| `0x8000_0048 + 4*i` | `REG_SPRITE_BULLET(i)` | WO | Bullet `i` (0..7): `{10'd0, big, active, y[9:0], x[9:0]}` |
| `0x8000_0080` | `REG_HUD_HEALTH` | WO | Health: `{16'd0, hp2[7:0], hp1[7:0]}` (0..20 hearts each) |
| `0x8000_0084` | `REG_TANK_ANGLES` | WO | Turret angles: `{24'd0, ang2[3:0], ang1[3:0]}` |
| `0x8000_0100 - 0x8000_073F` | **Maze Tile RAM** | R/W | Mirrored True-Dual-Port RAM (400 words = 20×20 tiles) |

---

## 4. Embedded C Game Mechanics & Physics

The C program compiled into the processor handles all real-time game dynamics:

1. **Fixed-Point Trigonometry & Directional Vectors:**
   * 16 discrete angles ($0^\circ$ to $337.5^\circ$, step $22.5^\circ$).
   * Lookup tables `g_cos_fp` and `g_sin_fp` scaled by $2^8 = 256$.
   * Velocity calculation uses the RV32M hardware `MUL` instruction:
     $$\Delta x = \frac{\cos(\theta) \times \text{Speed}}{256}, \quad \Delta y = \frac{\sin(\theta) \times \text{Speed}}{256}$$
2. **2D Angular Wall Reflection Physics:**
   * When a projectile collides with a wall (detected by the hardware collision scanner or arena boundary):
     * Horizontal surface bounce: $v_y' = -v_y, \quad v_x' = v_x$
     * Vertical surface bounce: $v_x' = -v_x, \quad v_y' = v_y$
   * Bullets bounce up to 4 times before being despawned.
3. **Dynamic Maze Demolition:**
   * When a bullet strikes a destructible brick wall (`TILE_BRICK_DESTRUCTIBLE`), the CPU calculates the tile grid coordinates (`row = y / 24`, `col = x / 24` using RV32M `DIV`) and writes `TILE_EMPTY` into `MAZE_TILE(row, col)`.
   * Both BRAM copies (Copy V and Copy C) update in real-time, removing the wall from both display and future collisions.
4. **Damage & Health:**
   * Normal bullets deal 2 hearts of damage; Special bullets deal 6 hearts of damage.
   * Colliding bullets annihilate each other.
   * Handled collision bits are atomically cleared by writing the bitmask back to the W1C registers (`0x80000020..0x80000028`).

---

## 5. How to Compile the C Game Program

The repository includes a dedicated build tool that invokes `riscv-none-elf-gcc`:

```powershell
# From the project root or inside software/
python software/build_and_export.py
```
Or execute:
```cmd
cd software
build.bat
```

**Output Artifacts:**
* `software/game.elf`: Compiled RISC-V ELF executable.
* `software/game.dis`: Full assembly disassembly with symbolic annotations.
* `imem.hex`: 1024 words of 32-bit hex instructions for the Instruction ROM.
* `dmem.hex`: 1024 words of 32-bit hex data for the Data RAM.

---

## 6. How to Run the Simulation

### Option 1: 1-Click Batch Script
From the repository root, run:
```cmd
run_sim.bat
```
This script automatically:
1. Recompiles the C game software with GCC into `imem.hex` and `dmem.hex`.
2. Compiles all Verilog modules using Vivado `xvlog --sv`.
3. Elaborates the design with `xelab`.
4. Runs the simulation in `xsim`, outputting live MMIO writes and hardware telemetry.

### Option 2: Step-by-Step Manual Simulation
```cmd
# 1. Compile SystemVerilog modules
D:\2025.2\Vivado\bin\xvlog.bat --sv -i modules\cpu modules\cpu\pipeline.v modules\cpu\memory.v modules\cpu\multiplier_dsp.v modules\cpu\divider_radix2.v modules\cpu\top_rv32m.v modules\game_engine\collision_engine.v modules\game_engine\cooldown_timers.v modules\game_engine\sprite_hud_regfile.v modules\game_engine\input_controller.v modules\game_engine\maze_tile_ram.v modules\game_engine\mmio_interconnect.v modules\vga\vga_timing_gen.v modules\vga\maze_tile_renderer.v modules\vga\sprite_engine.v modules\vga\health_hud.v modules\vga\pixel_priority_mixer.v modules\top_fpga.v modules\sim\tb_game_top.v

# 2. Elaborate simulation snapshot
D:\2025.2\Vivado\bin\xelab.bat -debug typical -top tb_game_top -s tb_game_top_sim

# 3. Execute simulation
D:\2025.2\Vivado\bin\xsim.bat tb_game_top_sim -R
```

**Expected Simulation Output:**
```
==========================================================
  BATTLE TANKS: FULL HARDWARE/SOFTWARE SIMULATION START   
==========================================================
[TIME 200000] Reset de-asserted. CPU starting at PC=0x00000000...
[TIME 5200000] Player 1 pressing FORWARD and SHOOT...
[MMIO WRITE] Addr=0x80000088 Data=0x00000014 at time 7945000   # HUD Health 1: 20 hearts
[MMIO WRITE] Addr=0x8000008c Data=0x00000014 at time 7965000   # HUD Health 2: 20 hearts
[MMIO WRITE] Addr=0x80000020 Data=0x000000ff at time 8025000   # Clear Wall Collision W1C
[MMIO WRITE] Addr=0x80000014 Data=0x00000002 at time 11325000  # Trigger P1 Shoot Timer
...
>> TEST PASSED: CPU executed C game software without exceptions!
```

---

## 7. How to Synthesize and Run on FPGA

### Board Setup: Digilent Nexys A7
1. Open **Vivado 2025.2**.
2. Create a new project targeting part **`xc7a100tcsg324-1`**.
3. Add all Verilog sources from `modules/cpu/`, `modules/game_engine/`, `modules/vga/`, and `modules/top_fpga.v`.
4. Add `modules/constraints.xdc` as the design constraints.
5. Ensure `imem.hex` and `dmem.hex` are added to the project or placed in the working synthesis directory so Block RAMs are pre-loaded at bitstream generation.
6. Click **Generate Bitstream**.
7. Connect your Nexys A7 board via micro-USB, open **Hardware Manager**, and program the FPGA.
8. Connect a VGA monitor to port `J2`.

### Board Controls:
* **Red Pushbutton (`CPU_RESETN`, Pin C12):** Active-low system reset.
* **Player 1 (breadboard 1 -> Pmod `JA`):** breadboard 3.3V rail to `JA` pin 6; each push button connects its pin to 3.3V (inputs use internal pull-downs).
  * Button 1 (`JA1`, `C17`): Move Forward
  * Button 2 (`JA2`, `D18`): Move Backward
  * Button 3 (`JA3`, `E18`): Rotate Left (CCW)
  * Button 4 (`JA4`, `G17`): Rotate Right (CW)
  * Button 5 (`JA7`, `D17`): Primary Fire (0.5s cooldown)
  * Button 6 (`JA8`, `E17`): Special Attack (20s cooldown)
* **Player 2 (breadboard 2 -> Pmod `JB`):** same layout, 3.3V rail to `JB` pin 6.
  * Button 1 (`JB1`, `D14`): Move Forward
  * Button 2 (`JB2`, `F16`): Move Backward
  * Button 3 (`JB3`, `G16`): Rotate Left (CCW)
  * Button 4 (`JB4`, `H14`): Rotate Right (CW)
  * Button 5 (`JB7`, `E16`): Primary Fire (0.5s cooldown)
  * Button 6 (`JB8`, `F13`): Special Attack (20s cooldown)
* **Status LEDs (`LD0..LD15`):**
  * `LD[0]`: Bullet-Wall collision flag
  * `LD[1]`: Bullet-Bullet collision flag
  * `LD[2]`: Bullet-Tank collision flag
  * `LD[3]`: CPU exception, latched until reset (off when running cleanly)
  * `LD[7:4]`: Player 2 debounced FWD / BWD / LEFT / RIGHT (JB1..JB4)
  * `LD[13:8]`: Player 1 debounced FWD / BWD / LEFT / RIGHT / SHOOT / SPECIAL
  * `LD[14]`: Game-loop heartbeat (blinks ~2 Hz while the C game loop runs)
  * `LD[15]`: Clock heartbeat (blinks ~1.5 Hz whenever the FPGA is configured and out of reset)
