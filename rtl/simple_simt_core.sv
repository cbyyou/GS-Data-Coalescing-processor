`default_nettype none

// Minimal four-lane SIMT execution core.
//
// All lanes share one PC and execute the same instruction. Each lane owns an
// eight-entry 32-bit register file. There is no branch divergence, cache or
// exception support; the purpose of this core is to execute a small program
// that generates realistic per-lane load/store addresses for the coalescer.
module simple_simt_core (
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

    output logic [3:0]  lane_req_valid,
    input  logic [3:0]  lane_req_ready,
    output logic [31:0] lane_addr0,
    output logic [31:0] lane_addr1,
    output logic [31:0] lane_addr2,
    output logic [31:0] lane_addr3,
    output logic [3:0]  lane_req_write,
    output logic [31:0] lane_wdata0,
    output logic [31:0] lane_wdata1,
    output logic [31:0] lane_wdata2,
    output logic [31:0] lane_wdata3,
    output logic [3:0]  lane_be0,
    output logic [3:0]  lane_be1,
    output logic [3:0]  lane_be2,
    output logic [3:0]  lane_be3,
    input  logic [3:0]  lane_rsp_valid,
    input  logic [31:0] lane_rdata0,
    input  logic [31:0] lane_rdata1,
    input  logic [31:0] lane_rdata2,
    input  logic [31:0] lane_rdata3
);
    localparam logic [3:0] OP_NOP   = 4'h0;
    localparam logic [3:0] OP_MOVI  = 4'h1;
    localparam logic [3:0] OP_LID   = 4'h2;
    localparam logic [3:0] OP_ADD   = 4'h3;
    localparam logic [3:0] OP_ADDI  = 4'h4;
    localparam logic [3:0] OP_SLLI  = 4'h5;
    localparam logic [3:0] OP_LOAD  = 4'h6;
    localparam logic [3:0] OP_STORE = 4'h7;
    localparam logic [3:0] OP_SUB   = 4'h8;
    localparam logic [3:0] OP_AND   = 4'h9;
    localparam logic [3:0] OP_OR    = 4'ha;
    localparam logic [3:0] OP_XOR   = 4'hb;
    localparam logic [3:0] OP_SRLI  = 4'hc;
    localparam logic [3:0] OP_HALT  = 4'hf;

    typedef enum logic [1:0] {IDLE, RUN, MEMORY} state_t;
    state_t state_q;

    logic [31:0] imem [0:63];
    logic [31:0] regs [0:3][0:7];
    logic [5:0]  pc_q;

    logic [31:0] instruction;
    logic [3:0]  opcode;
    logic [2:0]  rd;
    logic [2:0]  rs1;
    logic [2:0]  rs2;
    logic [31:0] immediate;

    logic [3:0]  req_pending_q;
    logic [3:0]  rsp_pending_q;
    logic [31:0] ls_addr_q [0:3];
    logic [31:0] ls_wdata_q [0:3];
    logic [2:0]  ls_rd_q;
    logic        ls_write_q;

    logic [31:0] lane_rdata [0:3];
    logic [3:0]  accepted_mask;
    logic [3:0]  response_mask;
    logic [3:0]  req_remaining;
    logic [3:0]  rsp_remaining;

    integer lane;
    integer reg_index;

    always_ff @(posedge clk) begin
        if (imem_we)
            imem[imem_waddr] <= imem_wdata;
    end

    always_comb begin
        instruction = imem[pc_q];
        opcode = instruction[31:28];
        rd = instruction[27:25];
        rs1 = instruction[24:22];
        rs2 = instruction[21:19];
        immediate = {{13{instruction[18]}}, instruction[18:0]};

        busy = (state_q != IDLE);
        debug_pc = pc_q;

        lane_req_valid = (state_q == MEMORY) ? req_pending_q : 4'b0;
        lane_req_write = {4{ls_write_q}};
        lane_addr0 = ls_addr_q[0];
        lane_addr1 = ls_addr_q[1];
        lane_addr2 = ls_addr_q[2];
        lane_addr3 = ls_addr_q[3];
        lane_wdata0 = ls_wdata_q[0];
        lane_wdata1 = ls_wdata_q[1];
        lane_wdata2 = ls_wdata_q[2];
        lane_wdata3 = ls_wdata_q[3];
        lane_be0 = 4'hf;
        lane_be1 = 4'hf;
        lane_be2 = 4'hf;
        lane_be3 = 4'hf;

        lane_rdata[0] = lane_rdata0;
        lane_rdata[1] = lane_rdata1;
        lane_rdata[2] = lane_rdata2;
        lane_rdata[3] = lane_rdata3;

        accepted_mask = req_pending_q & lane_req_ready;
        response_mask = rsp_pending_q & lane_rsp_valid;
        req_remaining = req_pending_q & ~accepted_mask;
        rsp_remaining = rsp_pending_q & ~response_mask;
    end

    always_ff @(posedge clk) begin
        if (reset) begin
            state_q <= IDLE;
            pc_q <= 6'b0;
            done <= 1'b0;
            retired_instructions <= 32'b0;
            req_pending_q <= 4'b0;
            rsp_pending_q <= 4'b0;
            ls_rd_q <= 3'b0;
            ls_write_q <= 1'b0;
            for (lane = 0; lane < 4; lane = lane + 1) begin
                ls_addr_q[lane] <= 32'b0;
                ls_wdata_q[lane] <= 32'b0;
                for (reg_index = 0; reg_index < 8; reg_index = reg_index + 1)
                    regs[lane][reg_index] <= 32'b0;
            end
        end else begin
            done <= 1'b0;
            case (state_q)
                IDLE: begin
                    if (start) begin
                        pc_q <= 6'b0;
                        retired_instructions <= 32'b0;
                        req_pending_q <= 4'b0;
                        rsp_pending_q <= 4'b0;
                        for (lane = 0; lane < 4; lane = lane + 1)
                            for (reg_index = 0; reg_index < 8; reg_index = reg_index + 1)
                                regs[lane][reg_index] <= 32'b0;
                        state_q <= RUN;
                    end
                end

                RUN: begin
                    case (opcode)
                        OP_NOP: begin
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_MOVI: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= immediate;
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_LID: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= lane;
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_ADD: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= regs[lane][rs1] + regs[lane][rs2];
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_ADDI: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= regs[lane][rs1] + immediate;
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_SLLI: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= regs[lane][rs1] << instruction[4:0];
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_SUB: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= regs[lane][rs1] - regs[lane][rs2];
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_AND: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= regs[lane][rs1] & regs[lane][rs2];
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_OR: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= regs[lane][rs1] | regs[lane][rs2];
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_XOR: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= regs[lane][rs1] ^ regs[lane][rs2];
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_SRLI: begin
                            for (lane = 0; lane < 4; lane = lane + 1)
                                regs[lane][rd] <= regs[lane][rs1] >> instruction[4:0];
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                        OP_LOAD, OP_STORE: begin
                            for (lane = 0; lane < 4; lane = lane + 1) begin
                                ls_addr_q[lane] <= regs[lane][rs1] + immediate;
                                ls_wdata_q[lane] <= regs[lane][rd];
                            end
                            ls_rd_q <= rd;
                            ls_write_q <= (opcode == OP_STORE);
                            req_pending_q <= 4'hf;
                            rsp_pending_q <= 4'hf;
                            state_q <= MEMORY;
                        end
                        OP_HALT: begin
                            retired_instructions <= retired_instructions + 32'd1;
                            done <= 1'b1;
                            state_q <= IDLE;
                        end
                        default: begin
                            pc_q <= pc_q + 6'd1;
                            retired_instructions <= retired_instructions + 32'd1;
                        end
                    endcase
                end

                MEMORY: begin
                    req_pending_q <= req_remaining;
                    rsp_pending_q <= rsp_remaining;
                    if (!ls_write_q) begin
                        for (lane = 0; lane < 4; lane = lane + 1)
                            if (response_mask[lane])
                                regs[lane][ls_rd_q] <= lane_rdata[lane];
                    end
                    if ((req_remaining == 4'b0) && (rsp_remaining == 4'b0)) begin
                        pc_q <= pc_q + 6'd1;
                        retired_instructions <= retired_instructions + 32'd1;
                        state_q <= RUN;
                    end
                end

                default: state_q <= IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
