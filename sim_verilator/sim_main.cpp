//=============================================================================
// Battle Tanks: Real-Time Verilator + SDL2 Hardware Simulation Harness
//
// Features:
// 1. Direct hardware cycle execution of RV32IM soft-core and game accelerators.
// 2. High-speed 640x480 @ 60 Hz VGA beam rasterization into an SDL2 window.
// 3. Laptop keyboard input mapping for 2-player combat.
// 4. Real-time PWM audio playback through laptop speakers via SDL2 audio.
// 5. Accurate 60 FPS frame limiter and real-time performance telemetry.
//=============================================================================

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "Vsim_top.h"
#include "verilated.h"

// Screen constants
static const int VGA_WIDTH  = 640;
static const int VGA_HEIGHT = 480;
static const int WINDOW_SCALE = 2; // Default 1280x960 for high-res laptop displays

// Audio constants
static const int AUDIO_SAMPLE_RATE = 44100;
static const int SYS_CLK_HZ        = 100000000; // 100 MHz oscillator
static const int CYCLES_PER_AUDIO_SAMPLE = SYS_CLK_HZ / AUDIO_SAMPLE_RATE;

//-----------------------------------------------------------------------------
// VGA CRT Monitor Emulation (Strict 640x480 @ 60Hz timing sync)
//-----------------------------------------------------------------------------
struct VgaMonitor {
    uint32_t framebuffer[VGA_HEIGHT][VGA_WIDTH];
    int h_pos;
    int v_pos;
    bool prev_hsync;
    bool prev_vsync;
    bool frame_ready;

    VgaMonitor() : h_pos(0), v_pos(0), prev_hsync(true), prev_vsync(true), frame_ready(false) {
        std::memset(framebuffer, 0, sizeof(framebuffer));
    }

    void tick(bool hsync, bool vsync, uint8_t r4, uint8_t g4, uint8_t b4) {
        // Falling edge of HSYNC pulse (line 656)
        if (prev_hsync && !hsync) {
            h_pos = 656;
        }
        // Falling edge of VSYNC pulse (line 490)
        if (prev_vsync && !vsync) {
            v_pos = 490;
            frame_ready = true; // Complete frame rasterized!
        }

        prev_hsync = hsync;
        prev_vsync = vsync;

        // Active display window: 0..639 horizontal, 0..479 vertical
        if (h_pos >= 0 && h_pos < VGA_WIDTH && v_pos >= 0 && v_pos < VGA_HEIGHT) {
            // Expand 4-bit VGA DAC colors (0..15) to 8-bit ARGB (0..255)
            uint32_t r8 = (r4 << 4) | r4;
            uint32_t g8 = (g4 << 4) | g4;
            uint32_t b8 = (b4 << 4) | b4;
            framebuffer[v_pos][h_pos] = (0xFF000000u) | (r8 << 16) | (g8 << 8) | b8;
        }

        // Advance raster beam
        h_pos++;
        if (h_pos >= 800) {
            h_pos = 0;
            v_pos++;
            if (v_pos >= 525) {
                v_pos = 0;
            }
        }
    }
};

//-----------------------------------------------------------------------------
// Keyboard Controller State
// Bit assignments: [0]=FWD, [1]=BWD, [2]=LEFT, [3]=RIGHT, [4]=SHOOT, [5]=SPEC
//-----------------------------------------------------------------------------
static uint8_t s_p1_keys = 0;
static uint8_t s_p2_keys = 0;
static bool    s_reset_req = false;

static void handle_key_event(const SDL_KeyboardEvent& key) {
    bool is_down = (key.type == SDL_KEYDOWN);
    uint8_t mask_p1 = 0;
    uint8_t mask_p2 = 0;

    switch (key.keysym.sym) {
        // --- Player 1 Controls (WASD + Space/E) ---
        case SDLK_w:      mask_p1 = (1 << 0); break; // FWD
        case SDLK_s:      mask_p1 = (1 << 1); break; // BWD
        case SDLK_a:      mask_p1 = (1 << 2); break; // Turn Left
        case SDLK_d:      mask_p1 = (1 << 3); break; // Turn Right
        case SDLK_SPACE:
        case SDLK_j:
        case SDLK_f:      mask_p1 = (1 << 4); break; // Primary Shoot
        case SDLK_e:
        case SDLK_k:
        case SDLK_g:      mask_p1 = (1 << 5); break; // Special Weapon

        // --- Player 2 Controls (Arrow Keys + RShift/Enter) ---
        case SDLK_UP:     mask_p2 = (1 << 0); break; // FWD
        case SDLK_DOWN:   mask_p2 = (1 << 1); break; // BWD
        case SDLK_LEFT:   mask_p2 = (1 << 2); break; // Turn Left
        case SDLK_RIGHT:  mask_p2 = (1 << 3); break; // Turn Right
        case SDLK_RSHIFT:
        case SDLK_KP_1:
        case SDLK_l:      mask_p2 = (1 << 4); break; // Primary Shoot
        case SDLK_RETURN:
        case SDLK_KP_2:
        case SDLK_p:      mask_p2 = (1 << 5); break; // Special Weapon

        // --- System Controls ---
        case SDLK_r:
            if (is_down) s_reset_req = true;
            break;
        default:
            break;
    }

    if (mask_p1) {
        if (is_down) s_p1_keys |= mask_p1;
        else         s_p1_keys &= ~mask_p1;
    }
    if (mask_p2) {
        if (is_down) s_p2_keys |= mask_p2;
        else         s_p2_keys &= ~mask_p2;
    }
}

//-----------------------------------------------------------------------------
// Main Simulation Entrypoint
//-----------------------------------------------------------------------------
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    std::cout << "=================================================================\n";
    std::cout << "  Battle Tanks: RV32IM FPGA Real-Time Verilator Simulation      \n";
    std::cout << "  Display: 640x480 @ 60 Hz VGA | Audio: 44.1 kHz 1-bit PWM      \n";
    std::cout << "=================================================================\n";
    std::cout << "  CONTROLS:\n";
    std::cout << "  Player 1: W/A/S/D = Move & Steer | Space/J = Shoot | E/K = Special\n";
    std::cout << "  Player 2: Up/Down/Left/Right     | RShift/L = Shoot | Enter/P = Special\n";
    std::cout << "  System:   R = Reset Game         | Esc = Quit Simulation\n";
    std::cout << "=================================================================\n";

    // Initialize SDL2
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "[ERROR] SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Create SDL2 Window & Renderer
    SDL_Window* window = SDL_CreateWindow(
        "Battle Tanks - RV32IM Soft-Core Hardware Simulation",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        VGA_WIDTH * WINDOW_SCALE, VGA_HEIGHT * WINDOW_SCALE,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "[ERROR] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, 0);
    }
    SDL_RenderSetLogicalSize(renderer, VGA_WIDTH, VGA_HEIGHT);

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        VGA_WIDTH, VGA_HEIGHT
    );

    // Audio Setup
    SDL_AudioSpec audio_spec;
    SDL_zero(audio_spec);
    audio_spec.freq     = AUDIO_SAMPLE_RATE;
    audio_spec.format   = AUDIO_S16SYS;
    audio_spec.channels = 1;
    audio_spec.samples  = 1024;
    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(nullptr, 0, &audio_spec, nullptr, 0);
    if (audio_device) {
        SDL_PauseAudioDevice(audio_device, 0); // Start playback
    }

    // Instantiate Verilated DUT
    Vsim_top* top = new Vsim_top;
    VgaMonitor monitor;

    // Power-on Reset Sequence
    std::cout << "[SIM] Powering on and asserting active-low reset...\n";
    top->clk        = 0;
    top->reset      = 0; // Assert reset
    top->p1_btn_raw = 0;
    top->p2_btn_raw = 0;

    for (int i = 0; i < 40; ++i) {
        top->clk = 1; top->eval();
        top->clk = 0; top->eval();
    }

    top->reset = 1; // Release reset (CPU begins execution)
    std::cout << "[SIM] Reset released. RV32IM soft-core executing embedded firmware!\n";

    bool running = true;
    uint64_t clock_cycles = 0;
    uint32_t audio_cycle_acc = 0;
    uint32_t audio_high_acc  = 0;
    std::vector<int16_t> audio_batch;
    audio_batch.reserve(1024);

    auto t_last_fps = std::chrono::steady_clock::now();
    uint32_t frame_count = 0;

    // Main Simulation Loop
    while (running && !Verilated::gotFinish()) {
        auto t_frame_start = std::chrono::steady_clock::now();

        // Simulate until 1 full VGA frame is completed
        monitor.frame_ready = false;
        while (!monitor.frame_ready) {
            // Apply current inputs
            top->p1_btn_raw = s_p1_keys;
            top->p2_btn_raw = s_p2_keys;

            // Handle momentary reset request
            if (s_reset_req) {
                top->reset = 0;
                s_reset_req = false;
            } else {
                top->reset = 1;
            }

            // Step 1: Clock High
            top->clk = 1;
            top->eval();

            // Step 2: Clock Low
            top->clk = 0;
            top->eval();
            clock_cycles++;

            // Sample PWM audio output
            if (top->aud_sd && top->aud_pwm) {
                audio_high_acc++;
            }
            audio_cycle_acc++;
            if (audio_cycle_acc >= CYCLES_PER_AUDIO_SAMPLE) {
                // Downsample PWM to 16-bit PCM sample
                float duty = (float)audio_high_acc / (float)audio_cycle_acc;
                int16_t pcm = (int16_t)((duty - 0.5f) * 24000.0f);
                audio_batch.push_back(pcm);
                audio_cycle_acc = 0;
                audio_high_acc  = 0;

                if (audio_device && audio_batch.size() >= 512) {
                    if (SDL_GetQueuedAudioSize(audio_device) < 8192) {
                        SDL_QueueAudio(audio_device, audio_batch.data(), audio_batch.size() * sizeof(int16_t));
                    }
                    audio_batch.clear();
                }
            }

            // VGA Pixel Clock Tick (100 MHz / 4 = 25 MHz pixel clock)
            if ((clock_cycles & 3) == 0) {
                monitor.tick(
                    top->vga_hsync,
                    top->vga_vsync,
                    top->vga_r,
                    top->vga_g,
                    top->vga_b
                );
            }
        }

        frame_count++;

        // Update SDL2 Texture and Render
        SDL_UpdateTexture(texture, nullptr, monitor.framebuffer, VGA_WIDTH * sizeof(uint32_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // Poll SDL2 Window & Keyboard Events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                } else {
                    handle_key_event(event.key);
                }
            }
        }

        // Real-Time Frame Limiter: Throttle to ~60 FPS (16.67 ms per frame)
        auto t_frame_end = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed_ms = t_frame_end - t_frame_start;
        if (elapsed_ms.count() < 16.666) {
            std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(16.666 - elapsed_ms.count()));
        }

        // FPS and Telemetry updates once every second
        auto t_now = std::chrono::steady_clock::now();
        std::chrono::duration<double> fps_elapsed = t_now - t_last_fps;
        if (fps_elapsed.count() >= 1.0) {
            double fps = (double)frame_count / fps_elapsed.count();
            frame_count = 0;
            t_last_fps = t_now;

            // Extract status from top LEDs
            uint16_t led = top->led;
            bool cpu_alive = (led >> 14) & 1;
            bool tank_hit  = (led >> 2) & 1;
            bool wall_hit  = led & 1;

            char title_buf[256];
            snprintf(title_buf, sizeof(title_buf),
                     "Battle Tanks - RV32IM Simulation [%.1f FPS | CPU: %s | Hit: %s]",
                     fps, cpu_alive ? "RUNNING" : "WAIT", (tank_hit || wall_hit) ? "YES" : "NO");
            SDL_SetWindowTitle(window, title_buf);
        }
    }

    std::cout << "[SIM] Simulation stopped. Cleaning up...\n";

    if (audio_device) {
        SDL_CloseAudioDevice(audio_device);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    top->final();
    delete top;

    std::cout << "[SIM] Done. Exiting.\n";
    return 0;
}

