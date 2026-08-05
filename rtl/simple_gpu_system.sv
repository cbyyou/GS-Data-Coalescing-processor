`default_nettype none

// Top level for a minimal programmable four-lane GPU connected to the existing
// 32-byte data coalescer.
module simple_gpu_system (
    input  logic        clk,
    input  logic        reset,
    input  logic        imem_we,
    input  logic [5:0]  imem_waddr,
    input  logic [31:0] imem_wdata,
    input  logic        start,
    output logic        busy,
    output logic        done,
    output logic [5:0]  debug_pc,
    output logic [31:0] retired_instructions,

    output logic        mem_req_valid,
    input  logic        mem_req_ready,
    output logic        mem_req_write,
    output logic [31:0] mem_req_line_addr,
    output logic [31:0] mem_wdata0,
    output logic [31:0] mem_wdata1,
    output logic [31:0] mem_wdata2,
    output logic [31:0] mem_wdata3,
    output logic [31:0] mem_wdata4,
    output logic [31:0] mem_wdata5,
    output logic [31:0] mem_wdata6,
    output logic [31:0] mem_wdata7,
    output logic [31:0] mem_byteen,
    input  logic        mem_rsp_valid,
    input  logic [31:0] mem_rdata0,
    input  logic [31:0] mem_rdata1,
    input  logic [31:0] mem_rdata2,
    input  logic [31:0] mem_rdata3,
    input  logic [31:0] mem_rdata4,
    input  logic [31:0] mem_rdata5,
    input  logic [31:0] mem_rdata6,
    input  logic [31:0] mem_rdata7
);
    logic [3:0]  lane_req_valid;
    logic [3:0]  lane_req_ready;
    logic [3:0]  lane_req_write;
    logic [31:0] lane_addr [0:3];
    logic [31:0] lane_wdata [0:3];
    logic [3:0]  lane_be [0:3];
    logic [3:0]  lane_rsp_valid;
    logic [31:0] lane_rdata [0:3];

    simple_simt_core core (
        .clk, .reset, .imem_we, .imem_waddr, .imem_wdata, .start,
        .busy, .done, .debug_pc, .retired_instructions,
        .lane_req_valid, .lane_req_ready,
        .lane_addr0(lane_addr[0]), .lane_addr1(lane_addr[1]),
        .lane_addr2(lane_addr[2]), .lane_addr3(lane_addr[3]),
        .lane_req_write,
        .lane_wdata0(lane_wdata[0]), .lane_wdata1(lane_wdata[1]),
        .lane_wdata2(lane_wdata[2]), .lane_wdata3(lane_wdata[3]),
        .lane_be0(lane_be[0]), .lane_be1(lane_be[1]),
        .lane_be2(lane_be[2]), .lane_be3(lane_be[3]),
        .lane_rsp_valid,
        .lane_rdata0(lane_rdata[0]), .lane_rdata1(lane_rdata[1]),
        .lane_rdata2(lane_rdata[2]), .lane_rdata3(lane_rdata[3]));

    data_coalescer coalescer (
        .clk, .reset,
        .core_req_valid(lane_req_valid), .core_req_ready(lane_req_ready),
        .core_addr0(lane_addr[0]), .core_addr1(lane_addr[1]),
        .core_addr2(lane_addr[2]), .core_addr3(lane_addr[3]),
        .core_write(lane_req_write),
        .core_wdata0(lane_wdata[0]), .core_wdata1(lane_wdata[1]),
        .core_wdata2(lane_wdata[2]), .core_wdata3(lane_wdata[3]),
        .core_be0(lane_be[0]), .core_be1(lane_be[1]),
        .core_be2(lane_be[2]), .core_be3(lane_be[3]),
        .core_rsp_valid(lane_rsp_valid),
        .core_rdata0(lane_rdata[0]), .core_rdata1(lane_rdata[1]),
        .core_rdata2(lane_rdata[2]), .core_rdata3(lane_rdata[3]),
        .mem_req_valid, .mem_req_ready, .mem_req_write, .mem_req_line_addr,
        .mem_wdata0, .mem_wdata1, .mem_wdata2, .mem_wdata3,
        .mem_wdata4, .mem_wdata5, .mem_wdata6, .mem_wdata7, .mem_byteen,
        .mem_rsp_valid, .mem_rdata0, .mem_rdata1, .mem_rdata2, .mem_rdata3,
        .mem_rdata4, .mem_rdata5, .mem_rdata6, .mem_rdata7);
endmodule

`default_nettype wire
