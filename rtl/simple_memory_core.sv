`default_nettype none

// Minimal programmable core used to generate an independent load/store stream.
// It intentionally models only the memory-facing behavior required by this project.
module simple_memory_core (
    input  logic        clk,
    input  logic        reset,
    input  logic        cfg_start,
    input  logic [31:0] cfg_base_addr,
    input  logic [31:0] cfg_stride,
    input  logic [15:0] cfg_count,
    input  logic        cfg_write,
    input  logic [31:0] cfg_wdata_base,
    output logic        busy,
    output logic        done,
    output logic [15:0] completed_count,
    output logic [31:0] last_rdata,

    output logic        req_valid,
    input  logic        req_ready,
    output logic [31:0] req_addr,
    output logic        req_write,
    output logic [31:0] req_wdata,
    output logic [3:0]  req_be,
    input  logic        rsp_valid,
    input  logic [31:0] rsp_rdata
);
    typedef enum logic [1:0] {IDLE, REQUEST, WAIT_RSP} state_t;
    state_t state;
    logic [31:0] addr_q;
    logic [31:0] stride_q;
    logic [31:0] wdata_q;
    logic [15:0] remaining_q;
    logic        write_q;

    always_comb begin
        req_valid = (state == REQUEST);
        req_addr = addr_q;
        req_write = write_q;
        req_wdata = wdata_q;
        req_be = 4'hf;
        busy = (state != IDLE);
    end

    always_ff @(posedge clk) begin
        if (reset) begin
            state <= IDLE;
            done <= 1'b0;
            completed_count <= 16'b0;
            last_rdata <= 32'b0;
            addr_q <= 32'b0;
            stride_q <= 32'b0;
            wdata_q <= 32'b0;
            remaining_q <= 16'b0;
            write_q <= 1'b0;
        end else begin
            done <= 1'b0;
            case (state)
                IDLE: begin
                    if (cfg_start) begin
                        completed_count <= 16'b0;
                        addr_q <= cfg_base_addr;
                        stride_q <= cfg_stride;
                        wdata_q <= cfg_wdata_base;
                        remaining_q <= cfg_count;
                        write_q <= cfg_write;
                        if (cfg_count == 0) done <= 1'b1;
                        else state <= REQUEST;
                    end
                end
                REQUEST: begin
                    if (req_ready) state <= WAIT_RSP;
                end
                WAIT_RSP: begin
                    if (rsp_valid) begin
                        last_rdata <= rsp_rdata;
                        completed_count <= completed_count + 16'd1;
                        if (remaining_q == 16'd1) begin
                            remaining_q <= 16'b0;
                            done <= 1'b1;
                            state <= IDLE;
                        end else begin
                            remaining_q <= remaining_q - 16'd1;
                            addr_q <= addr_q + stride_q;
                            wdata_q <= wdata_q + 32'd1;
                            state <= REQUEST;
                        end
                    end
                end
                default: state <= IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
