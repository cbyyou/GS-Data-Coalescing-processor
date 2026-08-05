#include "Vsimple_gpu_system.h"
#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr uint8_t OP_MOVI = 0x1;
constexpr uint8_t OP_LID = 0x2;
constexpr uint8_t OP_ADD = 0x3;
constexpr uint8_t OP_SLLI = 0x5;
constexpr uint8_t OP_LOAD = 0x6;
constexpr uint8_t OP_STORE = 0x7;
constexpr uint8_t OP_SUB = 0x8;
constexpr uint8_t OP_AND = 0x9;
constexpr uint8_t OP_OR = 0xa;
constexpr uint8_t OP_XOR = 0xb;
constexpr uint8_t OP_SRLI = 0xc;
constexpr uint8_t OP_HALT = 0xf;

uint32_t encode_rrr(uint8_t opcode, uint8_t rd, uint8_t rs1, uint8_t rs2) {
    return (uint32_t(opcode) << 28) | (uint32_t(rd & 7) << 25) |
           (uint32_t(rs1 & 7) << 22) | (uint32_t(rs2 & 7) << 19);
}

uint32_t encode_imm(uint8_t opcode, uint8_t rd, uint8_t rs1, int32_t imm) {
    return (uint32_t(opcode) << 28) | (uint32_t(rd & 7) << 25) |
           (uint32_t(rs1 & 7) << 22) | (uint32_t(imm) & 0x7ffff);
}

uint32_t encode_shift_op(uint8_t opcode, uint8_t rd, uint8_t rs1, uint8_t amount) {
    return (uint32_t(opcode) << 28) | (uint32_t(rd & 7) << 25) |
           (uint32_t(rs1 & 7) << 22) | (amount & 31);
}

uint32_t encode_shift(uint8_t rd, uint8_t rs1, uint8_t amount) {
    return encode_shift_op(OP_SLLI, rd, rs1, amount);
}

std::vector<uint32_t> vector_add_program(uint8_t lane_shift) {
    return {
        encode_imm(OP_LID, 1, 0, 0),            // r1 = lane_id
        encode_shift(1, 1, lane_shift),         // r1 = lane_id * lane spacing
        encode_imm(OP_MOVI, 2, 0, 0x1000),      // r2 = A base
        encode_rrr(OP_ADD, 2, 2, 1),            // r2 = &A[lane]
        encode_imm(OP_LOAD, 3, 2, 0),           // r3 = A[lane]
        encode_imm(OP_MOVI, 4, 0, 0x2000),      // r4 = B base
        encode_rrr(OP_ADD, 4, 4, 1),            // r4 = &B[lane]
        encode_imm(OP_LOAD, 5, 4, 0),           // r5 = B[lane]
        encode_rrr(OP_ADD, 6, 3, 5),            // r6 = r3 + r5
        encode_imm(OP_MOVI, 7, 0, 0x3000),      // r7 = C base
        encode_rrr(OP_ADD, 7, 7, 1),            // r7 = &C[lane]
        encode_imm(OP_STORE, 6, 7, 0),          // C[lane] = r6
        uint32_t(OP_HALT) << 28,
    };
}

std::vector<uint32_t> alu_logic_program() {
    return {
        encode_imm(OP_LID, 1, 0, 0),            // r1 = lane_id
        encode_shift(1, 1, 5),                   // r1 = lane_id * 32 (per-lane result line)
        encode_imm(OP_MOVI, 0, 0, 0x4000),      // r0 = result base
        encode_rrr(OP_ADD, 0, 0, 1),            // r0 = result base + lane offset
        encode_imm(OP_MOVI, 2, 0, 0x55),        // r2 = 0x55
        encode_imm(OP_MOVI, 3, 0, 0x0f),        // r3 = 0x0f
        encode_rrr(OP_ADD, 4, 2, 3),            // 0x64
        encode_imm(OP_STORE, 4, 0, 0),
        encode_rrr(OP_SUB, 4, 2, 3),            // 0x46
        encode_imm(OP_STORE, 4, 0, 4),
        encode_rrr(OP_AND, 4, 2, 3),            // 0x05
        encode_imm(OP_STORE, 4, 0, 8),
        encode_rrr(OP_OR, 4, 2, 3),             // 0x5f
        encode_imm(OP_STORE, 4, 0, 12),
        encode_rrr(OP_XOR, 4, 2, 3),            // 0x5a
        encode_imm(OP_STORE, 4, 0, 16),
        encode_shift_op(OP_SRLI, 4, 2, 2),      // 0x15
        encode_imm(OP_STORE, 4, 0, 20),
        uint32_t(OP_HALT) << 28,
    };
}

struct Result {
    std::string name;
    uint64_t lane_requests;
    uint64_t memory_transactions;
    uint64_t cycles;
    uint64_t retired;
};

class Harness {
  public:
    Vsimple_gpu_system dut;
    uint64_t cycles = 0;
    uint64_t transactions = 0;

    Harness() {
        dut.clk = 0;
        dut.reset = 1;
        dut.imem_we = 0;
        dut.start = 0;
        dut.mem_req_ready = 1;
        dut.mem_rsp_valid = 0;
        dut.eval();
        for (int i = 0; i < 3; ++i) tick();
        dut.reset = 0;
    }

    void load_program(const std::vector<uint32_t>& program) {
        for (size_t i = 0; i < program.size(); ++i) {
            dut.imem_we = 1;
            dut.imem_waddr = i;
            dut.imem_wdata = program[i];
            tick();
        }
        dut.imem_we = 0;
    }

    void write_word(uint32_t byte_addr, uint32_t value) {
        auto& words = line(byte_addr & ~uint32_t(31));
        words[(byte_addr >> 2) & 7] = value;
    }

    uint32_t read_word(uint32_t byte_addr) {
        auto& words = line(byte_addr & ~uint32_t(31));
        return words[(byte_addr >> 2) & 7];
    }

    Result run_vector_add(const std::string& name, uint8_t lane_shift) {
        const uint32_t spacing = 1u << lane_shift;
        for (uint32_t lane = 0; lane < 4; ++lane) {
            write_word(0x1000 + lane * spacing, 10 + lane);
            write_word(0x2000 + lane * spacing, 100 + 2 * lane);
            write_word(0x3000 + lane * spacing, 0xdeadbeef);
        }

        load_program(vector_add_program(lane_shift));
        const uint64_t start_cycles = cycles;
        const uint64_t start_transactions = transactions;
        dut.start = 1;
        tick();
        dut.start = 0;

        for (uint64_t elapsed = 0; elapsed < 1000; ++elapsed) {
            tick();
            if (dut.done) {
                for (uint32_t lane = 0; lane < 4; ++lane) {
                    const uint32_t got = read_word(0x3000 + lane * spacing);
                    const uint32_t expected = (10 + lane) + (100 + 2 * lane);
                    if (got != expected) {
                        std::fprintf(stderr,
                                     "FAIL: %s lane %u result 0x%08x, expected 0x%08x\n",
                                     name.c_str(), lane, got, expected);
                        std::exit(1);
                    }
                }
                return {name, 12, transactions - start_transactions,
                        cycles - start_cycles, dut.retired_instructions};
            }
        }
        std::fprintf(stderr, "FAIL: timeout in %s at PC %u\n",
                     name.c_str(), unsigned(dut.debug_pc));
        std::exit(2);
    }

    Result run_alu_logic() {
        constexpr uint32_t base = 0x4000;
        load_program(alu_logic_program());
        const uint64_t start_cycles = cycles;
        const uint64_t start_transactions = transactions;
        dut.start = 1;
        tick();
        dut.start = 0;

        for (uint64_t elapsed = 0; elapsed < 1000; ++elapsed) {
            tick();
            if (dut.done) {
                constexpr std::array<uint32_t, 6> expected = {
                    0x64, 0x46, 0x05, 0x5f, 0x5a, 0x15};
                for (uint32_t lane = 0; lane < 4; ++lane) {
                    for (uint32_t result = 0; result < expected.size(); ++result) {
                        const uint32_t got = read_word(base + lane * 32 + result * 4);
                        if (got != expected[result]) {
                            std::fprintf(stderr,
                                         "FAIL: ALU lane %u result %u 0x%08x, expected 0x%08x\n",
                                         lane, result, got, expected[result]);
                            std::exit(1);
                        }
                    }
                }
                return {"alu_logic", 24, transactions - start_transactions,
                        cycles - start_cycles, dut.retired_instructions};
            }
        }
        std::fprintf(stderr, "FAIL: timeout in ALU/logic program at PC %u\n",
                     unsigned(dut.debug_pc));
        std::exit(2);
    }

  private:
    bool pending_response_ = false;
    bool next_response_ = false;
    std::array<uint32_t, 8> pending_data_{};
    std::array<uint32_t, 8> next_data_{};
    std::unordered_map<uint32_t, std::array<uint32_t, 8>> memory_;

    std::array<uint32_t, 8>& line(uint32_t line_addr) {
        return memory_[line_addr];
    }

    void tick() {
        dut.clk = 0;
        dut.mem_rsp_valid = pending_response_;
        dut.mem_rdata0 = pending_data_[0];
        dut.mem_rdata1 = pending_data_[1];
        dut.mem_rdata2 = pending_data_[2];
        dut.mem_rdata3 = pending_data_[3];
        dut.mem_rdata4 = pending_data_[4];
        dut.mem_rdata5 = pending_data_[5];
        dut.mem_rdata6 = pending_data_[6];
        dut.mem_rdata7 = pending_data_[7];
        dut.eval();
        capture_memory_request();

        dut.clk = 1;
        dut.eval();
        ++cycles;

        dut.clk = 0;
        dut.eval();
        pending_response_ = next_response_;
        pending_data_ = next_data_;
        next_response_ = false;
    }

    void capture_memory_request() {
        if (!(dut.mem_req_valid && dut.mem_req_ready))
            return;

        ++transactions;
        auto& words = line(dut.mem_req_line_addr);
        next_data_ = words;
        if (dut.mem_req_write) {
            const std::array<uint32_t, 8> wdata = {
                dut.mem_wdata0, dut.mem_wdata1, dut.mem_wdata2, dut.mem_wdata3,
                dut.mem_wdata4, dut.mem_wdata5, dut.mem_wdata6, dut.mem_wdata7};
            for (unsigned byte = 0; byte < 32; ++byte) {
                if ((dut.mem_byteen >> byte) & 1u) {
                    const unsigned word = byte / 4;
                    const unsigned shift = (byte % 4) * 8;
                    const uint32_t mask = 0xffu << shift;
                    words[word] = (words[word] & ~mask) | (wdata[word] & mask);
                }
            }
        }
        next_response_ = true;
    }
};

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    std::vector<Result> results;
    {
        Harness harness;
        results.push_back(harness.run_vector_add("vector_add_contiguous", 2));
    }
    {
        Harness harness;
        results.push_back(harness.run_vector_add("vector_add_stride_32B", 5));
    }
    {
        Harness harness;
        results.push_back(harness.run_alu_logic());
    }

    std::ofstream csv("build/gpu_benchmark.csv");
    csv << "pattern,lane_requests,memory_transactions,requests_per_transaction,"
           "transaction_reduction_percent,cycles,retired_instructions\n";

    std::printf("%-26s %10s %10s %10s %10s %8s %8s\n",
                "pattern", "lane_req", "mem_txn", "req/txn",
                "reduct%", "cycles", "retired");
    for (const auto& result : results) {
        const double req_per_txn =
            double(result.lane_requests) / result.memory_transactions;
        const double reduction =
            100.0 * (1.0 - double(result.memory_transactions) /
                                result.lane_requests);
        std::printf("%-26s %10llu %10llu %10.3f %10.2f %8llu %8llu\n",
                    result.name.c_str(),
                    static_cast<unsigned long long>(result.lane_requests),
                    static_cast<unsigned long long>(result.memory_transactions),
                    req_per_txn, reduction,
                    static_cast<unsigned long long>(result.cycles),
                    static_cast<unsigned long long>(result.retired));
        csv << result.name << ',' << result.lane_requests << ','
            << result.memory_transactions << ',' << req_per_txn << ','
            << reduction << ',' << result.cycles << ',' << result.retired << '\n';
    }

    std::printf("vector-add functional checks: PASS\n");
    std::printf("CSV: build/gpu_benchmark.csv\n");
    return 0;
}
