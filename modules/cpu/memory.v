`timescale 1ns/1ps
//====================================
// Instruction Memory (IMEM) - FPGA-safe ROM
//====================================
`ifndef MEMORY_V
`define MEMORY_V

module instr_mem #(
    parameter HEX_FILE = "imem.hex"
)(
    input  wire        clk,
    input  wire [31:0] pc,     // byte address
    output reg  [31:0] instr
);

    // 1024 words = 4 KB
    (* ram_style = "block" *)
    reg [31:0] imem [0:1023];

    // FPGA ROM initialization
`ifndef SYNTHESIS
    integer i;
`endif
    initial begin
`ifndef SYNTHESIS
        // Pre-clear array with NOPs so un-initialized words don't produce X
        for (i = 0; i < 1024; i = i + 1)
            imem[i] = 32'h0000_0013;      // NOP
`endif
        $readmemh(HEX_FILE, imem);
    end

    // Synchronous instruction fetch, word-aligned index
    always @(posedge clk) begin
        instr <= imem[pc[11:2]];    // word address
    end

endmodule



//====================================
// Data Memory (DMEM) - FPGA-safe
//====================================
module data_mem #(
    parameter HEX_FILE = "dmem.hex"
)(
    input         clk,

    // Read port
    input         re,
    input  [31:0] raddr,   // byte address
    output reg [31:0] rdata,

    // Write port
    input         we,
    input  [31:0] waddr,   // byte address
    input  [31:0] wdata,
    input  [3:0]  wstrb
);

    // 1024 words = 4 KB, word addressable
    (* ram_style = "block" *)
    reg [31:0] dmem [0:1023];

    // Decode byte address to word index
    wire [9:0] rindex = raddr[11:2];
    wire [9:0] windex = waddr[11:2];

`ifndef SYNTHESIS
    integer i;
`endif
    initial begin
`ifndef SYNTHESIS
        for (i = 0; i < 1024; i = i + 1)
            dmem[i] = 32'h0000_0000;
`endif
        $readmemh(HEX_FILE, dmem);
    end

    // -------------------------
    // WRITE + READ (SYNC)
    // -------------------------
    // TODO-DMEM-3 (DONE)
    // - byte-wise writes driven by wstrb
    // - 1 cycle read latency
    // - same-cycle read-after-write handled with byte-level forwarding
    //   (a store is issued from WB in the same cycle the following load is
    //    issued from EX, so both land on the same clock edge)

    always @(posedge clk) begin
        // ---- WRITE ----
        if (we) begin
            if (wstrb[0]) dmem[windex][7:0]   <= wdata[7:0];
            if (wstrb[1]) dmem[windex][15:8]  <= wdata[15:8];
            if (wstrb[2]) dmem[windex][23:16] <= wdata[23:16];
            if (wstrb[3]) dmem[windex][31:24] <= wdata[31:24];
        end

        // ---- READ (1-cycle latency, RAW-safe) ----
        if (re) begin
            if (we && (rindex == windex)) begin
                // Byte-level forwarding
                rdata[7:0]   <= wstrb[0] ? wdata[7:0]   : dmem[rindex][7:0];
                rdata[15:8]  <= wstrb[1] ? wdata[15:8]  : dmem[rindex][15:8];
                rdata[23:16] <= wstrb[2] ? wdata[23:16] : dmem[rindex][23:16];
                rdata[31:24] <= wstrb[3] ? wdata[31:24] : dmem[rindex][31:24];
            end
            else begin
                rdata <= dmem[rindex];
            end
        end
        // else: rdata holds its value
    end

endmodule

`endif
