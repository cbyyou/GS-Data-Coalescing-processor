`default_nettype none

// Shared memory coalescer for four independent requesters.
// Each requester has a one-entry input buffer. The lowest-numbered available
// request seeds a group; other buffered requests with the same
// 32-byte segment and operation type join the same downstream transaction.
module data_coalescer (
    input  logic        clk,
    input  logic        reset,

    input  logic [3:0]  core_req_valid,
    output logic [3:0]  core_req_ready,
    input  logic [31:0] core_addr0,
    input  logic [31:0] core_addr1,
    input  logic [31:0] core_addr2,
    input  logic [31:0] core_addr3,
    input  logic [3:0]  core_write,
    input  logic [31:0] core_wdata0,
    input  logic [31:0] core_wdata1,
    input  logic [31:0] core_wdata2,
    input  logic [31:0] core_wdata3,
    input  logic [3:0]  core_be0,
    input  logic [3:0]  core_be1,
    input  logic [3:0]  core_be2,
    input  logic [3:0]  core_be3,

    output logic [3:0]  core_rsp_valid,
    output logic [31:0] core_rdata0,
    output logic [31:0] core_rdata1,
    output logic [31:0] core_rdata2,
    output logic [31:0] core_rdata3,

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
    typedef enum logic [1:0] {COLLECT, ISSUE, WAIT_RSP} state_t;
    state_t state;

    logic [3:0]  buf_valid_q;
    logic [31:0] buf_addr_q [0:3];
    logic        buf_write_q [0:3];
    logic [31:0] buf_wdata_q [0:3];
    logic [3:0]  buf_be_q [0:3];

    logic [3:0]  selected_mask;
    logic        selected_found;
    logic [31:0] selected_line;
    logic        selected_write;
    logic [31:0] selected_wdata [0:7];
    logic [31:0] selected_byteen;

    logic [3:0]  active_mask_q;
    logic [31:0] active_addr_q [0:3];
    logic        active_write_q;
    logic [31:0] active_line_q;
    logic [31:0] active_wdata_q [0:7];
    logic [31:0] active_byteen_q;

    logic [3:0]  rsp_valid_q;
    logic [31:0] rsp_data_q [0:3];
    logic [31:0] in_addr [0:3];
    logic [31:0] in_wdata [0:3];
    logic [3:0]  in_be [0:3];
    integer i, word_offset;

    always_comb begin
        in_addr[0] = core_addr0; in_addr[1] = core_addr1;
        in_addr[2] = core_addr2; in_addr[3] = core_addr3;
        in_wdata[0] = core_wdata0; in_wdata[1] = core_wdata1;
        in_wdata[2] = core_wdata2; in_wdata[3] = core_wdata3;
        in_be[0] = core_be0; in_be[1] = core_be1;
        in_be[2] = core_be2; in_be[3] = core_be3;

        core_req_ready = ~buf_valid_q;
        core_rsp_valid = rsp_valid_q;
        core_rdata0 = rsp_data_q[0]; core_rdata1 = rsp_data_q[1];
        core_rdata2 = rsp_data_q[2]; core_rdata3 = rsp_data_q[3];

        mem_req_valid = (state == ISSUE);
        mem_req_write = active_write_q;
        mem_req_line_addr = active_line_q;
        mem_wdata0 = active_wdata_q[0]; mem_wdata1 = active_wdata_q[1];
        mem_wdata2 = active_wdata_q[2]; mem_wdata3 = active_wdata_q[3];
        mem_wdata4 = active_wdata_q[4]; mem_wdata5 = active_wdata_q[5];
        mem_wdata6 = active_wdata_q[6]; mem_wdata7 = active_wdata_q[7];
        mem_byteen = active_byteen_q;
    end

    // Select a stable group from requests that have already reached the buffers.
    always_comb begin
        selected_mask = 4'b0;
        selected_found = 1'b0;
        selected_line = 32'b0;
        selected_write = 1'b0;
        selected_byteen = 32'b0;
        word_offset = 0;
        for (i = 0; i < 8; i = i + 1) selected_wdata[i] = 32'b0;

        for (i = 0; i < 4; i = i + 1) begin
            if (buf_valid_q[i] && !selected_found) begin
                selected_line = buf_addr_q[i] & 32'hffffffe0;
                selected_write = buf_write_q[i];
                selected_found = 1'b1;
            end
        end

        for (i = 0; i < 4; i = i + 1) begin
            if (buf_valid_q[i] && selected_found &&
                ((buf_addr_q[i] & 32'hffffffe0) == selected_line) &&
                (buf_write_q[i] == selected_write)) begin
                selected_mask[i] = 1'b1;
                if (buf_write_q[i]) begin
                    word_offset = (buf_addr_q[i] & 32'h1f) >> 2;
                    selected_wdata[word_offset] = buf_wdata_q[i];
                    selected_byteen[word_offset*4 +: 4] = buf_be_q[i];
                end
            end
        end
    end

    integer si;
    always_ff @(posedge clk) begin
        if (reset) begin
            state <= COLLECT;
            buf_valid_q <= 4'b0;
            active_mask_q <= 4'b0;
            active_write_q <= 1'b0;
            active_line_q <= 32'b0;
            active_byteen_q <= 32'b0;
            rsp_valid_q <= 4'b0;
            for (si = 0; si < 4; si = si + 1) begin
                rsp_data_q[si] <= 32'b0;
                active_addr_q[si] <= 32'b0;
            end
            for (si = 0; si < 8; si = si + 1) active_wdata_q[si] <= 32'b0;
        end else begin
            rsp_valid_q <= 4'b0;

            // Independent input handshakes may occur in any state for empty buffers.
            for (si = 0; si < 4; si = si + 1) begin
                if (core_req_valid[si] && core_req_ready[si]) begin
                    buf_valid_q[si] <= 1'b1;
                    buf_addr_q[si] <= in_addr[si];
                    buf_write_q[si] <= core_write[si];
                    buf_wdata_q[si] <= in_wdata[si];
                    buf_be_q[si] <= in_be[si];
                end
            end

            case (state)
                COLLECT: begin
                    if (selected_found) begin
                        active_mask_q <= selected_mask;
                        active_write_q <= selected_write;
                        active_line_q <= selected_line;
                        active_byteen_q <= selected_byteen;
                        for (si = 0; si < 4; si = si + 1)
                            active_addr_q[si] <= buf_addr_q[si];
                        for (si = 0; si < 8; si = si + 1)
                            active_wdata_q[si] <= selected_wdata[si];
                        state <= ISSUE;
                    end
                end
                ISSUE: begin
                    if (mem_req_ready) state <= WAIT_RSP;
                end
                WAIT_RSP: begin
                    if (mem_rsp_valid) begin
                        rsp_valid_q <= active_mask_q;
                        for (si = 0; si < 4; si = si + 1) begin
                            if (active_mask_q[si]) begin
                                buf_valid_q[si] <= 1'b0;
                                if (!active_write_q) begin
                                    case (active_addr_q[si][4:2])
                                        0: rsp_data_q[si] <= mem_rdata0;
                                        1: rsp_data_q[si] <= mem_rdata1;
                                        2: rsp_data_q[si] <= mem_rdata2;
                                        3: rsp_data_q[si] <= mem_rdata3;
                                        4: rsp_data_q[si] <= mem_rdata4;
                                        5: rsp_data_q[si] <= mem_rdata5;
                                        6: rsp_data_q[si] <= mem_rdata6;
                                        default: rsp_data_q[si] <= mem_rdata7;
                                    endcase
                                end
                            end
                        end
                        state <= COLLECT;
                    end
                end
                default: state <= COLLECT;
            endcase
        end
    end
endmodule

`default_nettype wire
