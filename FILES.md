I've now read all the Verilog modules, all the C sources/headers, the build scripts, the linker script, the startup assembly, the constraints file, the testbenches, the disassembly, and the README. Here is the complete analysis you asked for.

---

# Battle Tanks — Full Module + Software Chart

## 1. System in One Paragraph

The project is a **2-player VGA "Battle Tanks" game** running on an **RV32IM RISC-V soft-core CPU** inside a Xilinx Artix-7 FPGA (Nexys A7). The C program in `software/` is compiled with `riscv-none-elf-gcc` into machine code (`imem.hex`) and data (`dmem.hex`). The CPU executes those instructions and controls the game hardware through **memory-mapped I/O (MMIO)** at address bit 31 = 1 (`0x8000_xxxx`). Compute-heavy tasks (rendering, collision detection, cooldowns, input debouncing) are offloaded to dedicated Verilog hardware accelerators so the CPU only runs "game logic."

**Data flow:**
```
software/*.c ──riscv-none-elf-gcc──▶ imem.hex / dmem.hex
                                        │ $readmemh
                    ┌───────────────────▼────────────────────┐
                    │  RV32IM CPU (IF/ID → EX → WB)          │
                    │  instr_mem (ROM) + data_mem (RAM)      │
                    └──────────────┬─────────────────────────┘
                                   │ d_addr[31]=1 → MMIO
                    ┌──────────────▼─────────────────────────┐
                    │ mmio_interconnect (address decode)     │
                    └──┬───────┬───────┬───────┬─────────────┘
             input_ctrl  cooldown  collision  sprite_hud  maze_tile_ram
                 │                      │           │           │
             VGA timing_gen ──▶ renderers ──▶ pixel_priority_mixer ──▶ VGA pins
```

---

## 2. Complete Verilog Chart (modules folder)

### 2A. Top level

| File | What it contains / does | Role in the combined system | Time complexity (hardware) |
|---|---|---|---|
| **`modules/top_fpga.v`** | Top module. Clock generator (100 MHz → 50 MHz `clk_sys`, 25 MHz `pix_ce`), reset synchronizer, instantiates CPU, both memories, MMIO interconnect, all peripherals, all VGA blocks, and drives 16 status LEDs. | Glues everything together; exposes board pins (clk, reset, buttons, VGA, LEDs). | Combinational wiring + counters; **O(1)** per clock. |

### 2B. CPU core (`modules/cpu/`)

| File | What it contains / does | Role in the combined system | Time complexity |
|---|---|---|---|
| **`opcode.vh`** | No logic. `localparam` definitions of RISC-V field slices, opcodes, funct3/funct7 values, NOP. | Shared "dictionary" of instruction encodings included by every CPU stage. | — (constants only). |
| **`pipeline.v`** (`pipe`) | 3-stage pipeline top (IF/ID → EX → WB), program counter, register file `regs[31:1]`, **register forwarding** from WB, stall register, instantiates `IF_ID`, `execute`, `wb`. | The CPU datapath/control center. Reads `imem`, drives `dmem`, outputs `pc_out` and `exception`. | **≈1 CPI** for ALU/load/store; branch = 2-cycle bubble; DIV stalls pipeline. **O(1)** per instruction. |
| **`IF_ID.v`** (`IF_ID` + `id_ex_reg`) | Stage 1: instruction fetch + decode, immediate generator (I/S/B/U/J types), control-signal generation, illegal-instruction & misalignment exception detection, **skid buffer** for stalls, and the ID→EX pipeline register. | Turns 32-bit instructions into control signals + immediate for the EX stage; flags illegal opcodes. | Fully combinational decode + 1 reg. **O(1)** per instruction. |
| **`execute.v`** (`execute` + `ex_mem_wb_reg`) | Stage 2: ALU (ADD/SUB/SLL/SLT/SLTU/XOR/SRL/SRA/OR/AND), 33-bit signed/unsigned compare for branches, next-PC logic (JAL/JALR/branches), branch-taken detection, and EX→WB pipeline register. Instantiates `rv32m_top`. | Computes all arithmetic/logic, addresses, and branch decisions; handshakes with the M-extension. | ALU is combinational **O(1)**; MUL/DIV handled via `rv32m_top`. **O(1)** per instruction (plus M-extension latency). |
| **`wb.v`** (`wb`) | Stage 3: Write-back. Store byte-enable generation (SB/SH/SW), load data formatting/sign-extension (LB/LH/LW/LBU/LHU), branch-stall shadow logic, and data-memory address/data outputs. | Produces the store data/byte-strobes and load results written back to the register file. | Combinational load mux + 1 reg. **O(1)** per instruction. |
| **`memory.v`** (`instr_mem`, `data_mem`) | `instr_mem`: 4 KB (1024×32) synchronous ROM, initialized from `imem.hex` with `$readmemh`, pre-filled with NOP. `data_mem`: 4 KB (1024×32) synchronous RAM from `dmem.hex`, byte-wise writes via `wstrb`, 1-cycle read with byte-level read-after-write forwarding. | Holds the compiled C machine code (IMEM) and the game's global/stack data (DMEM). | Both are BRAM: **1-cycle** read/write, **O(1)** per access. |
| **`multiplier_dsp.v`** | Single-cycle signed/unsigned multiplier (33×33-bit product) implementing MUL/MULH/MULHSU/MULHU by selecting low or high 32 bits. | Accelerates RV32M multiply instructions so C `*` operations (e.g. `cos*velocity`) take 1 cycle. | Pure combinational DSP multiply. **O(1)**. |
| **`divider_radix2.v`** | Iterative radix-2 non-restoring divider FSM (IDLE/COMPUTE/DONE), handles signed/unsigned DIV/DIVU/REM/REMU, divide-by-zero and overflow shortcuts. | Accelerates RV32M divide/remainder instructions. | **32 cycles** fixed latency for normal division (O(bits) = O(32)); edge cases O(1). |
| **`top_rv32m.v`** (`rv32m_top`) | M-extension controller: distinguishes MUL vs DIV by `funct3[2]`, instantiates multiplier + divider, stalls pipeline during DIV (`pipeline_stall = is_div && !div_ready`), muxes result. | Integrates the two M-unit blocks and signals the pipeline when to hold IF/ID during multi-cycle DIV. | Result mux O(1); DIV causes up to 32-cycle stall. |

### 2C. Game engine / peripherals (`modules/game_engine/`)

| File | What it contains / does | Role in the combined system | Time complexity |
|---|---|---|---|
| **`mmio_interconnect.v`** | MMIO address decoder (addr[31]=0 → DMEM, =1 → peripherals), write-enable generation for each peripheral, 1-cycle registered chip-selects, and a 5-input read-data AND-OR mux. Also has sim-only `$write`/`$display` console port. | The "router" that connects CPU load/stores to the right peripheral register. This is how the C code's MMIO reads/writes reach hardware. | Decode + registered mux. **O(1)** per access. |
| **`input_controller.v`** | 12-channel (2 players × 6 buttons) 2-flip-flop metastability synchronizer + 10 ms debouncer each. Packs into 2 MMIO read registers. | Debounces raw board buttons/switches so the C program reads clean, stable button states via `REG_P1_KEYS` / `REG_P2_KEYS`. | 12 parallel debouncers. **O(1)** per channel. |
| **`cooldown_timers.v`** | Shared 1 ms prescaler + 4 hardware down-counters (P1 shoot 500 ms, P1 special 20 s, P2 shoot 500 ms, P2 special 20 s), MMIO trigger decode + status read. | Offloads weapon cooldown counting from the CPU. C code only checks `REG_TIMER_STATUS` and writes `REG_TIMER_TRIGGER`. | Counters tick every 1 ms. **O(1)** per timer per ms. |
| **`collision_engine.v`** | **28 parallel bullet-bullet AABB** overlap cells, **16 bullet-tank AABB** cells, an **8-clock time-shared bullet-wall scanner** using maze Copy C, and **Write-1-to-Clear sticky hit registers** + MMIO read mux. | Detects all collision types in hardware every cycle; C code simply polls the sticky status registers (`REG_COLLISION_WALL/BULLET/TANK`) and clears them by writing 1s. | AABB cells: combinational **O(1)**. Bullet-wall scan: **8 clocks** per full sweep (amortized O(1)/bullet). W1C regs **O(1)**. |
| **`sprite_hud_regfile.v`** | 12×32-bit write-only MMIO registers holding tank positions/angles, 8 bullet positions/active/big flags, and health. Default positions/health on reset. | Latches game state written by C code and continuously drives the collision engine and VGA sprite/HUD renderers. | Register-file decode + write. **O(1)** per write. |
| **`maze_tile_ram.v`** | Two mirrored true-dual-port BRAMs (Copy V for VGA, Copy C for collision), 400 tiles × 2 bits. CPU writes both copies simultaneously on Port A; each copy has a dedicated read port B. Pre-initializes a default maze. | Stores the destructible maze. C code writes `MAZE_TILE(row,col)` to destroy bricks; VGA and collision engines read independently with zero contention. | Each port **1-cycle** read/write, **O(1)** per access. |

### 2D. VGA pipeline (`modules/vga/`)

| File | What it contains / does | Role in the combined system | Time complexity |
|---|---|---|---|
| **`vga_timing_gen.v`** | 640×480@60 Hz timing: hcnt (0..799) / vcnt (0..524) counters, visible beam x/y, active flag, active-low HSYNC/VSYNC, h_end/v_end/frame_tick strobes. | Generates the raster beam position that all renderers use. | Counter logic. **O(1)** per pixel; **O(800×525)** per frame. |
| **`maze_tile_renderer.v`** | Tracks tile_x/sub_x/tile_y/sub_y counters (**no hardware divider**), computes tile RAM address `row*20+col`, 1-clock alignment delay for BRAM latency, and generates wall/floor RGB (floor, brick, steel, corner). | Draws the 480×480 maze arena from Copy V BRAM. | **O(1)** per pixel (divider-free). |
| **`sprite_engine.v`** | Per-pixel hit test + rendering for 2 tanks (20×20 with 16-angle turret barrels/treads) and 8 bullets (6×6 normal disc, 10×10 big disc using `dx²+dy²` circle test). Registered outputs. | Draws tanks and bullets on top of the maze, using positions read from `sprite_hud_regfile`. | 2 tanks + 8 bullets in parallel combinational logic. **O(1)** per pixel. |
| **`health_hud.v`** | 8×8 heart sprite ROM, 2×20 hearts, P1/P2 header banners, arena divider bar, color decode. | Draws the health HUD in the right 160-px strip from `hp1`/`hp2`. | Bit-slice slot decode. **O(1)** per pixel. |
| **`pixel_priority_mixer.v`** | 2-stage sync/active delay line + strict priority mux (HUD > big bullet > bullet > tank P1/P2 > maze) + final VGA output registers with blanking. | Composites all layers and drives the 12-bit RGB DAC pins with correct timing alignment. | Priority mux **O(1)** per pixel. |

### 2E. Simulation (`modules/sim/`)

| File | What it contains / does | Role | Complexity |
|---|---|---|---|
| **`tb_game_top.v`** | Full-system testbench: 100 MHz clock, reset, button stimulus, instantiates `top_fpga` with accelerated prescaler/debounce, prints telemetry (PC, tank positions, health, bullet mask, exception), monitors MMIO writes. | Verifies the entire hardware+software system in simulation. | Test-only; drives the DUT. |
| **`tb_pipeline.v`** | CPU-only testbench: instantiates `pipe` + `instr_mem` + `data_mem`, traces writebacks/PC, auto-finishes on `ret` (0x00008067). | Unit-test the RV32IM pipeline in isolation. | Test-only. |

### 2F. Constraints

| File | What it contains | Role |
|---|---|---|
| **`modules/constraints.xdc`** | XDC pin assignments + clock constraint for Nexys A7: clk (E3, 100 MHz), reset (C12), 16 LEDs, 12 buttons/switches, 12 VGA DAC pins + HSYNC/VSYNC. | Maps the top-level ports to physical FPGA pins for bitstream generation. |

---

## 3. Complete Software Chart (software folder)

| File | What it contains / does | Role in combined system | Time complexity (software) |
|---|---|---|---|
| **`build_and_export.py`** | Python build driver: calls `riscv-none-elf-gcc -march=rv32im -O2 -nostdlib`, objdump for `game.dis`, objcopy to extract `.text`→`imem.bin` and `.rodata/.data`→`dmem.bin`, formats 1024-word hex files, copies them to root + all module subfolders. | Converts C → RISC-V instructions/data that the Verilog memories load. | Build-time only. |
| **`build.bat`** | One-line wrapper that runs `build_and_export.py`. | Convenience build entry point. | — |
| **`linker.ld`** | Linker script: 4 KB ROM + 4 KB RAM at 0x0, defines `.text/.rodata/.data/.bss`, stack top at `0xFE0`, global pointer. | Lays out the program so addresses match the 4 KB memories. | — |
| **`crt0.s`** | RISC-V startup assembly: sets global pointer `gp`, stack pointer `sp=0xFE0`, **zeroes `.bss`**, calls `main`, infinite-loop on return. | Runtime initialization before C `main()` runs. | BSS clear: **O(#bss words)** (here ~46 words). |
| **`include/mmio.h`** | MMIO address map + `MMIO_REG` volatile-pointer macro, button bit masks, timer/cooldown macros, collision register macros, sprite/HUD register macros, maze tile macro, and all inline helper functions (get input, is-ready, trigger cooldown, clear collision, update sprite). | The C-side "driver" for all hardware peripherals. Every MMIO access in the game maps to a Verilog register. | Each helper is **O(1)** (single load/store). |
| **`include/game.h`** | Arena/game constants (FP_SHIFT=8, tank/bullet sizes, speeds, max bounces), `Tank` and `Bullet` structs, extern globals, function prototypes. | Shared game type/constant definitions. | — |
| **`include/physics.h`** | Externs for the cos/sin lookup tables and physics function prototypes. | Shared physics interface. | — |
| **`src/main.c`** | `main()`: init once, then an **infinite game loop** calling update_tanks → update_bullets → process_collisions → sync_display → `game_delay(4000)`. | Top-level game loop (~60 FPS at 50 MHz). | Loop body ≈ O(MAX_BULLETS) constant per frame. |
| **`src/game.c`** | Game state globals (`g_p1`, `g_p2`, `g_bullets[8]`), `game_init`, `game_update_tanks` (input steering/rotation/movement/firing), `game_update_bullets` (projectile propagation + wall reflection), `game_process_collisions` (damage + bullet annihilation), `game_sync_display` (push state to MMIO), `game_delay`. | The actual game logic executed by the CPU. | See per-function breakdown below. |
| **`src/physics.c`** | 16-entry cos/sin fixed-point LUTs, `physics_check_and_destroy_tile` (maze tile query/destroy), `physics_spawn_bullet` (turret-tip spawn + velocity vector), `physics_reflect_bullet` (2D angular reflection + bounce limit). | Physics engine; uses RV32M MUL/DIV and maze BRAM writes. | See below. |
| **`software/game.c`** (top-level, standalone) | A single-file, self-contained copy of the same game engine (same logic, `static` functions). It is **not** in the build file list — `src/game.c` is the one actually compiled. | Reference/legacy single-file version. | Same as `src/game.c`. |
| **`game.dis` / `game.elf` / `*.hex` / `*.bin`** | Generated build artifacts (disassembly, ELF, hex memory images). | Outputs consumed by simulation/synthesis. | — |

### Per-function time complexity of the C game code

| C function | Work done | Big-O | Notes |
|---|---|---|---|
| `game_init` | Init 2 tanks + loop over `MAX_BULLETS`(8) + clear collisions | **O(8) = O(1)** | Fixed loop. |
| `game_update_tanks` | Per player: read input, rotate, move (2 MUL + shifts), spawn bullet (loop ≤4 slots) | **O(1)** per player | Constant work. |
| `game_update_bullets` | Loop 8 bullets; each does `physics_check_and_destroy_tile` + maybe reflect | **O(8)** | The `/24` divisions compile to `mulh` (single-cycle), **not** the 32-cycle divider (verified in `game.dis` at `0x934` using magic constant `0x2aaaaaab`). |
| `game_process_collisions` | 2× loop over 8 bullets (tank damage) + nested 7×8/2 = 28 pairs (bullet-bullet) | **O(8 + 28) = O(36)** | Constant. |
| `game_sync_display` | Loop 8 bullets, write MMIO registers | **O(8)** | Constant. |
| `physics_spawn_bullet` | Scan up to 4 bullet slots | **O(4)** | Constant. |
| `physics_reflect_bullet` | Boundary/tile checks + 4 `/24` divisions (→ mulh) | **O(1)** | Constant per bullet. |
| `physics_check_and_destroy_tile` | 2 `/24` divisions (→ mulh) + 1 maze RAM read/write | **O(1)** | Constant. |
| `game_delay(4000)` | Busy-wait NOP loop, 4000 iterations | **O(4000)** | Frame pacing. |

> **Key insight:** because the compiler (`-O2`) turns constant division `/ 24` into a **multiply-by-reciprocal (`MULH`)**, the hot game loop relies on the single-cycle DSP multiplier, not the 32-cycle iterative divider. The `divider_radix2` module still exists for genuine `div/rem` instructions, but the shipped game loop effectively runs at O(1) arithmetic per physics step.

---

## 4. Combined "Job" Chart — How C and Verilog Work Together

| Game task (C side) | C code → MMIO register | Verilog component that acts | Hardware job |
|---|---|---|---|
| Read buttons | `REG_P1_KEYS` / `REG_P2_KEYS` (0x0000/0x0004) | `input_controller` | Synchronize + debounce 12 buttons, return stable 6-bit values. |
| Weapon cooldown check | `REG_TIMER_STATUS` (0x0010) | `cooldown_timers` | Report which of 4 timers reached zero. |
| Start cooldown | `REG_TIMER_TRIGGER` (0x0014) | `cooldown_timers` | Reload 500 ms / 20 s down-counter. |
| Detect bullet-wall hits | `REG_COLLISION_WALL` (0x0020) | `collision_engine` | Sticky W1C wall-hit bits per bullet. |
| Detect bullet-bullet | `REG_COLLISION_BULLET` (0x0024) | `collision_engine` | 28-pair AABB matrix results. |
| Detect bullet-tank | `REG_COLLISION_TANK` (0x0028) | `collision_engine` | 16-cell AABB matrix results. |
| Clear collision flags | `REG_COLLISION_CLEAR` (0x002C) | `collision_engine` | Write-1-to-clear sticky registers. |
| Set tank pos/angle | `REG_SPRITE_P1/P2_TANK` (0x0040/0x0044) | `sprite_hud_regfile` → `sprite_engine` | Latch position, drive VGA sprite render. |
| Set bullet pos | `REG_SPRITE_BULLET_X/Y(i)` (0x0048+8i / 0x004C+8i) | `sprite_hud_regfile` → `sprite_engine` + `collision_engine` | Latch bullet coords for rendering & collision. |
| Set active/big masks | `REG_SPRITE_BULLET_ACT/BIG` (0x0080/0x0084) | `sprite_hud_regfile` | Enable/disable bullet sprites + size. |
| Set health | `REG_HUD_HEALTH_1/2` (0x0088/0x008C) | `sprite_hud_regfile` → `health_hud` | Drive heart HUD. |
| Read/destroy maze tile | `MAZE_TILE(row,col)` (0x0100+) | `maze_tile_ram` | Read/write mirrored Copy V + Copy C BRAM. |
| Display everything | (no CPU involvement) | `vga_timing_gen` + renderers + `pixel_priority_mixer` | Rasterize maze/sprites/HUD at 60 Hz, drive VGA DAC. |
| Execute the program | — | `pipeline` + `instr_mem` + `data_mem` | Fetch/decode/execute/writeback the compiled C instructions. |

---

## 5. Summary Time-Complexity Table

| Component | Latency / throughput | Complexity class |
|---|---|---|
| CPU ALU/load/store/branch | ~1 CPI (branch +2 cycles) | O(1) per instruction |
| DSP multiplier (MUL/MULH/…) | 1 cycle | O(1) |
| Radix-2 divider (DIV/REM) | 32 cycles | O(32) = O(bits) |
| Data/Instruction memory | 1 cycle per access | O(1) |
| MMIO interconnect | 1–2 cycles | O(1) |
| Input debouncer | 10 ms stabilization | O(1) per channel |
| Cooldown timers | 1 ms tick, independent of CPU | O(1) per timer |
| Collision AABB cells | fully parallel, 1 cycle | O(1) |
| Bullet-wall scanner | 8 cycles / full sweep | O(8) (amortized O(1)/bullet) |
| Maze BRAM | 1 cycle per port | O(1) |
| VGA timing + renderers + mixer | 1 pixel per `pix_ce` | O(1) per pixel (O(800×525) per frame) |
| Game loop C code | ~4000-cycle delay + O(8) state updates | O(MAX_BULLETS) ≈ O(1) per frame |

---

### If you want, I can also
- produce a **text/ASCII block diagram** of the exact signal connections in `top_fpga.v`, or
- generate a **CSV/Excel-ready version** of these tables for your report/presentation.

Would you like me to do either of those? (Note: generating a file requires you to switch to **Act mode**.)