`timescale 1ns / 1ps
//=============================================================================
// Fig 9: Sprite Engine
// Per-Pixel Tank and Bullet Hit Test (x, y from VGA timing generator)
//
// Tanks (x2): 20x20 pixels with 16-angle turret direction and tread patterns.
// Bullets (x8): 6x6 circular disc (normal) and 10x10 circular disc (big).
//
// Unsigned subtraction (x - sx, y - sy) makes negative offsets huge (>= 1024),
// so a single range compare (dx < width && dy < height) tests both edges.
// Outputs are registered to align with the Pixel Priority Mixer (Fig 10).
//=============================================================================
`ifndef SPRITE_ENGINE_V
`define SPRITE_ENGINE_V

module sprite_engine (
    input  wire        clk,            // 50 MHz clk_sys
    input  wire        reset_n,
    input  wire        pix_ce,

    input  wire [9:0]  x,              // Current VGA beam X (0..639)
    input  wire [9:0]  y,              // Current VGA beam Y (0..479)
    input  wire        active,

    // Tank MMIO registers from sprite_hud_regfile
    input  wire [9:0]  p1_tank_x,
    input  wire [9:0]  p1_tank_y,
    input  wire [3:0]  tank_ang1,
    input  wire [9:0]  p2_tank_x,
    input  wire [9:0]  p2_tank_y,
    input  wire [3:0]  tank_ang2,

    // Bullet MMIO registers
    input  wire [9:0]  bullet_x   [0:7],
    input  wire [9:0]  bullet_y   [0:7],
    input  wire [7:0]  bullet_act,
    input  wire [7:0]  bullet_big,

    // Output streams to Pixel Priority Mixer (Fig 10)
    output reg         tank_valid_p1,
    output reg  [11:0] tank_rgb_p1,
    output reg         tank_valid_p2,
    output reg  [11:0] tank_rgb_p2,
    output reg         bullet_valid,
    output reg  [11:0] bullet_rgb,
    output reg         big_valid,
    output reg  [11:0] big_rgb
);

    // =========================================================================
    // 1. Tank Hit Test & Rendering (Part A: 20x20 pixel cells)
    // =========================================================================
    wire [9:0] dx_t1 = x - p1_tank_x;
    wire [9:0] dy_t1 = y - p1_tank_y;
    wire in_box_t1 = (dx_t1 < 10'd20) && (dy_t1 < 10'd20) && active;

    wire [9:0] dx_t2 = x - p2_tank_x;
    wire [9:0] dy_t2 = y - p2_tank_y;
    wire in_box_t2 = (dx_t2 < 10'd20) && (dy_t2 < 10'd20) && active;

    // Tank 1 shape and color decoder
    reg        hit_t1;
    reg [11:0] rgb_t1;
    always @(*) begin
        hit_t1 = 1'b0;
        rgb_t1 = 12'h000;
        if (in_box_t1) begin
            // Left/Right treads
            if ((dx_t1 < 10'd3) || (dx_t1 >= 10'd17)) begin
                hit_t1 = 1'b1;
                rgb_t1 = (dy_t1[1] == 1'b0) ? 12'h040 : 12'h070; // Tread treads pattern
            end
            // Main body
            else if ((dy_t1 >= 10'd3) && (dy_t1 < 10'd17)) begin
                // Turret center dome
                if ((dx_t1 >= 10'd7) && (dx_t1 <= 10'd12) && (dy_t1 >= 10'd7) && (dy_t1 <= 10'd12)) begin
                    hit_t1 = 1'b1;
                    rgb_t1 = 12'h0F4; // Bright green dome
                end else begin
                    hit_t1 = 1'b1;
                    rgb_t1 = 12'h0B0; // Tank hull green
                end
            end
            // Turret gun barrel pointing based on tank_ang1[3:0]
            case (tank_ang1)
                4'd0:  if ((dy_t1 >= 10'd9) && (dy_t1 <= 10'd10) && (dx_t1 >= 10'd12)) begin hit_t1 = 1'b1; rgb_t1 = 12'h0F4; end // East
                4'd1, 4'd2, 4'd3:
                       if ((dx_t1 >= 10'd10) && (dy_t1 >= 10'd10) && ((dx_t1 + dy_t1) >= 10'd22)) begin hit_t1 = 1'b1; rgb_t1 = 12'h0F4; end // South-East
                4'd4:  if ((dx_t1 >= 10'd9) && (dx_t1 <= 10'd10) && (dy_t1 >= 10'd12)) begin hit_t1 = 1'b1; rgb_t1 = 12'h0F4; end // South
                4'd5, 4'd6, 4'd7:
                       if ((dx_t1 <= 10'd9) && (dy_t1 >= 10'd10) && ((dy_t1 - dx_t1) >= 10'd4)) begin hit_t1 = 1'b1; rgb_t1 = 12'h0F4; end // South-West
                4'd8:  if ((dy_t1 >= 10'd9) && (dy_t1 <= 10'd10) && (dx_t1 <= 10'd7)) begin hit_t1 = 1'b1; rgb_t1 = 12'h0F4; end // West
                4'd9, 4'd10, 4'd11:
                       if ((dx_t1 <= 10'd9) && (dy_t1 <= 10'd9) && ((dx_t1 + dy_t1) <= 10'd16)) begin hit_t1 = 1'b1; rgb_t1 = 12'h0F4; end // North-West
                4'd12: if ((dx_t1 >= 10'd9) && (dx_t1 <= 10'd10) && (dy_t1 <= 10'd7)) begin hit_t1 = 1'b1; rgb_t1 = 12'h0F4; end // North
                default:
                       if ((dx_t1 >= 10'd10) && (dy_t1 <= 10'd9) && ((dx_t1 - dy_t1) >= 10'd4)) begin hit_t1 = 1'b1; rgb_t1 = 12'h0F4; end // North-East
            endcase
        end
    end

    // Tank 2 shape and color decoder
    reg        hit_t2;
    reg [11:0] rgb_t2;
    always @(*) begin
        hit_t2 = 1'b0;
        rgb_t2 = 12'h000;
        if (in_box_t2) begin
            // Left/Right treads
            if ((dx_t2 < 10'd3) || (dx_t2 >= 10'd17)) begin
                hit_t2 = 1'b1;
                rgb_t2 = (dy_t2[1] == 1'b0) ? 12'h500 : 12'h800; // Tread pattern
            end
            // Main body
            else if ((dy_t2 >= 10'd3) && (dy_t2 < 10'd17)) begin
                if ((dx_t2 >= 10'd7) && (dx_t2 <= 10'd12) && (dy_t2 >= 10'd7) && (dy_t2 <= 10'd12)) begin
                    hit_t2 = 1'b1;
                    rgb_t2 = 12'hF44; // Bright red dome
                end else begin
                    hit_t2 = 1'b1;
                    rgb_t2 = 12'hB11; // Tank hull red
                end
            end
            // Turret gun barrel
            case (tank_ang2)
                4'd0:  if ((dy_t2 >= 10'd9) && (dy_t2 <= 10'd10) && (dx_t2 >= 10'd12)) begin hit_t2 = 1'b1; rgb_t2 = 12'hF44; end
                4'd1, 4'd2, 4'd3:
                       if ((dx_t2 >= 10'd10) && (dy_t2 >= 10'd10) && ((dx_t2 + dy_t2) >= 10'd22)) begin hit_t2 = 1'b1; rgb_t2 = 12'hF44; end
                4'd4:  if ((dx_t2 >= 10'd9) && (dx_t2 <= 10'd10) && (dy_t2 >= 10'd12)) begin hit_t2 = 1'b1; rgb_t2 = 12'hF44; end
                4'd5, 4'd6, 4'd7:
                       if ((dx_t2 <= 10'd9) && (dy_t2 >= 10'd10) && ((dy_t2 - dx_t2) >= 10'd4)) begin hit_t2 = 1'b1; rgb_t2 = 12'hF44; end
                4'd8:  if ((dy_t2 >= 10'd9) && (dy_t2 <= 10'd10) && (dx_t2 <= 10'd7)) begin hit_t2 = 1'b1; rgb_t2 = 12'hF44; end
                4'd9, 4'd10, 4'd11:
                       if ((dx_t2 <= 10'd9) && (dy_t2 <= 10'd9) && ((dx_t2 + dy_t2) <= 10'd16)) begin hit_t2 = 1'b1; rgb_t2 = 12'hF44; end
                4'd12: if ((dx_t2 >= 10'd9) && (dx_t2 <= 10'd10) && (dy_t2 <= 10'd7)) begin hit_t2 = 1'b1; rgb_t2 = 12'hF44; end
                default:
                       if ((dx_t2 >= 10'd10) && (dy_t2 <= 10'd9) && ((dx_t2 - dy_t2) >= 10'd4)) begin hit_t2 = 1'b1; rgb_t2 = 12'hF44; end
            endcase
        end
    end

    // =========================================================================
    // 2. Bullet Hit Test (Part B: 8 Bullet slots)
    // =========================================================================
    reg [7:0] bullet_norm_hit;
    reg [7:0] bullet_big_hit;

    genvar bi;
    generate
        for (bi = 0; bi < 8; bi = bi + 1) begin : gen_bullet_hit
            wire [9:0] b_dx = x - bullet_x[bi];
            wire [9:0] b_dy = y - bullet_y[bi];

            wire signed [5:0] big_cdx = $signed({1'b0, b_dx[4:0]}) - 6'sd4;
            wire signed [5:0] big_cdy = $signed({1'b0, b_dy[4:0]}) - 6'sd4;
            wire signed [4:0] norm_cdx = $signed({1'b0, b_dx[3:0]}) - 5'sd2;
            wire signed [4:0] norm_cdy = $signed({1'b0, b_dy[3:0]}) - 5'sd2;

            always @(*) begin
                bullet_norm_hit[bi] = 1'b0;
                bullet_big_hit[bi]  = 1'b0;
                if (bullet_act[bi] && active) begin
                    if (bullet_big[bi]) begin
                        if ((b_dx < 10'd10) && (b_dy < 10'd10)) begin
                            if ((big_cdx * big_cdx + big_cdy * big_cdy) <= 18)
                                bullet_big_hit[bi] = 1'b1;
                        end
                    end else begin
                        if ((b_dx < 10'd6) && (b_dy < 10'd6)) begin
                            if ((norm_cdx * norm_cdx + norm_cdy * norm_cdy) <= 6)
                                bullet_norm_hit[bi] = 1'b1;
                        end
                    end
                end
            end
        end
    endgenerate

    // =========================================================================
    // 3. Register Outputs to Priority Mixer (Fig 10)
    // =========================================================================
    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            tank_valid_p1 <= 1'b0;
            tank_rgb_p1   <= 12'h000;
            tank_valid_p2 <= 1'b0;
            tank_rgb_p2   <= 12'h000;
            bullet_valid  <= 1'b0;
            bullet_rgb    <= 12'h000;
            big_valid     <= 1'b0;
            big_rgb       <= 12'h000;
        end else begin
            tank_valid_p1 <= hit_t1;
            tank_rgb_p1   <= rgb_t1;

            tank_valid_p2 <= hit_t2;
            tank_rgb_p2   <= rgb_t2;

            bullet_valid  <= |bullet_norm_hit;
            bullet_rgb    <= 12'hFF2; // Bright incandescent yellow

            big_valid     <= |bullet_big_hit;
            big_rgb       <= 12'hF30; // Glowing fiery red/orange
        end
    end

endmodule

`endif
