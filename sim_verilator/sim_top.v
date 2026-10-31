`timescale 1ns / 1ps
//=============================================================================
// Battle Tanks: Verilator Top-Level Simulation Wrapper
//
// Wraps top_fpga with simulation-optimized parameters:
// 1. CLK_PER_MS = 1000: Speeds up cooldown timer prescaler for responsive
//    firing in software simulation without changing any game mechanics.
// 2. DEBOUNCE_MAX = 2: Low-latency debounce filter for laptop keyboard inputs.
// 3. MIN_HOLD = 0: Zero input stickiness for crisp, responsive controls.
//=============================================================================
`ifndef SIM_TOP_V
`define SIM_TOP_V

module sim_top #(
    parameter CLK_PER_MS   = 1000,
    parameter DEBOUNCE_MAX = 20'd2,
    parameter MIN_HOLD     = 21'd0,
    parameter IMEM_HEX     = "",
    parameter DMEM_HEX     = ""
)(
    input  wire        clk,            // 100 MHz simulated oscillator
    input  wire        reset,          // Active-low reset button

    // 2-Player Controls from SDL2 keyboard
    input  wire [5:0]  p1_btn_raw,     // [0]=FWD, [1]=BWD, [2]=L, [3]=R, [4]=SHOOT, [5]=SPEC
    input  wire [5:0]  p2_btn_raw,

    // Status / telemetry LEDs
    output wire [15:0] led,

    // Physical 12-bit VGA DAC Signals
    output wire [3:0]  vga_r,
    output wire [3:0]  vga_g,
    output wire [3:0]  vga_b,
    output wire        vga_hsync,
    output wire        vga_vsync,

    // Audio Output
    output wire        aud_pwm,
    output wire        aud_sd
);

    top_fpga #(
        .IMEM_HEX     (IMEM_HEX),
        .DMEM_HEX     (DMEM_HEX),
        .CLK_PER_MS   (CLK_PER_MS),
        .DEBOUNCE_MAX (DEBOUNCE_MAX),
        .MIN_HOLD     (MIN_HOLD)
    ) u_dut (
        .clk        (clk),
        .reset      (reset),
        .p1_btn_raw (p1_btn_raw),
        .p2_btn_raw (p2_btn_raw),
        .led        (led),
        .vga_r      (vga_r),
        .vga_g      (vga_g),
        .vga_b      (vga_b),
        .vga_hsync  (vga_hsync),
        .vga_vsync  (vga_vsync),
        .aud_pwm    (aud_pwm),
        .aud_sd     (aud_sd)
    );

endmodule

`endif

