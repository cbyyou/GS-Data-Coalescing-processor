`default_nettype none

// Four independently configured memory-stream cores sharing one coalescer.
module data_coalescing_system (
    input  logic        clk,
    input  logic        reset,
    input  logic [3:0]  core_start,
    input  logic [31:0] core_base0,
    input  logic [31:0] core_base1,
    input  logic [31:0] core_base2,
    input  logic [31:0] core_base3,
    input  logic [31:0] core_stride0,
    input  logic [31:0] core_stride1,
    input  logic [31:0] core_stride2,
    input  logic [31:0] core_stride3,
    input  logic [15:0] core_count0,
    input  logic [15:0] core_count1,
    input  logic [15:0] core_count2,
    input  logic [15:0] core_count3,
    input  logic [3:0]  core_write,
    input  logic [31:0] core_wdata_base0,
    input  logic [31:0] core_wdata_base1,
    input  logic [31:0] core_wdata_base2,
    input  logic [31:0] core_wdata_base3,
    output logic [3:0]  core_busy,
    output logic [3:0]  core_done,
    output logic [15:0] core_completed0,
    output logic [15:0] core_completed1,
    output logic [15:0] core_completed2,
    output logic [15:0] core_completed3,
    output logic [31:0] core_last_rdata0,
    output logic [31:0] core_last_rdata1,
    output logic [31:0] core_last_rdata2,
    output logic [31:0] core_last_rdata3,

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
    logic [3:0] req_valid, req_ready, req_write, rsp_valid;
    logic [31:0] req_addr [0:3];
    logic [31:0] req_wdata [0:3];
    logic [3:0] req_be [0:3];
    logic [31:0] rsp_rdata [0:3];

    simple_memory_core core0 (
        .clk, .reset, .cfg_start(core_start[0]), .cfg_base_addr(core_base0),
        .cfg_stride(core_stride0), .cfg_count(core_count0), .cfg_write(core_write[0]),
        .cfg_wdata_base(core_wdata_base0), .busy(core_busy[0]), .done(core_done[0]),
        .completed_count(core_completed0), .last_rdata(core_last_rdata0),
        .req_valid(req_valid[0]), .req_ready(req_ready[0]), .req_addr(req_addr[0]),
        .req_write(req_write[0]), .req_wdata(req_wdata[0]), .req_be(req_be[0]),
        .rsp_valid(rsp_valid[0]), .rsp_rdata(rsp_rdata[0]));
    simple_memory_core core1 (
        .clk, .reset, .cfg_start(core_start[1]), .cfg_base_addr(core_base1),
        .cfg_stride(core_stride1), .cfg_count(core_count1), .cfg_write(core_write[1]),
        .cfg_wdata_base(core_wdata_base1), .busy(core_busy[1]), .done(core_done[1]),
        .completed_count(core_completed1), .last_rdata(core_last_rdata1),
        .req_valid(req_valid[1]), .req_ready(req_ready[1]), .req_addr(req_addr[1]),
        .req_write(req_write[1]), .req_wdata(req_wdata[1]), .req_be(req_be[1]),
        .rsp_valid(rsp_valid[1]), .rsp_rdata(rsp_rdata[1]));
    simple_memory_core core2 (
        .clk, .reset, .cfg_start(core_start[2]), .cfg_base_addr(core_base2),
        .cfg_stride(core_stride2), .cfg_count(core_count2), .cfg_write(core_write[2]),
        .cfg_wdata_base(core_wdata_base2), .busy(core_busy[2]), .done(core_done[2]),
        .completed_count(core_completed2), .last_rdata(core_last_rdata2),
        .req_valid(req_valid[2]), .req_ready(req_ready[2]), .req_addr(req_addr[2]),
        .req_write(req_write[2]), .req_wdata(req_wdata[2]), .req_be(req_be[2]),
        .rsp_valid(rsp_valid[2]), .rsp_rdata(rsp_rdata[2]));
    simple_memory_core core3 (
        .clk, .reset, .cfg_start(core_start[3]), .cfg_base_addr(core_base3),
        .cfg_stride(core_stride3), .cfg_count(core_count3), .cfg_write(core_write[3]),
        .cfg_wdata_base(core_wdata_base3), .busy(core_busy[3]), .done(core_done[3]),
        .completed_count(core_completed3), .last_rdata(core_last_rdata3),
        .req_valid(req_valid[3]), .req_ready(req_ready[3]), .req_addr(req_addr[3]),
        .req_write(req_write[3]), .req_wdata(req_wdata[3]), .req_be(req_be[3]),
        .rsp_valid(rsp_valid[3]), .rsp_rdata(rsp_rdata[3]));

    data_coalescer coalescer (
        .clk, .reset, .core_req_valid(req_valid), .core_req_ready(req_ready),
        .core_addr0(req_addr[0]), .core_addr1(req_addr[1]),
        .core_addr2(req_addr[2]), .core_addr3(req_addr[3]),
        .core_write(req_write),
        .core_wdata0(req_wdata[0]), .core_wdata1(req_wdata[1]),
        .core_wdata2(req_wdata[2]), .core_wdata3(req_wdata[3]),
        .core_be0(req_be[0]), .core_be1(req_be[1]),
        .core_be2(req_be[2]), .core_be3(req_be[3]),
        .core_rsp_valid(rsp_valid),
        .core_rdata0(rsp_rdata[0]), .core_rdata1(rsp_rdata[1]),
        .core_rdata2(rsp_rdata[2]), .core_rdata3(rsp_rdata[3]),
        .mem_req_valid, .mem_req_ready, .mem_req_write, .mem_req_line_addr,
        .mem_wdata0, .mem_wdata1, .mem_wdata2, .mem_wdata3,
        .mem_wdata4, .mem_wdata5, .mem_wdata6, .mem_wdata7, .mem_byteen,
        .mem_rsp_valid, .mem_rdata0, .mem_rdata1, .mem_rdata2, .mem_rdata3,
        .mem_rdata4, .mem_rdata5, .mem_rdata6, .mem_rdata7);
endmodule

`default_nettype wire
