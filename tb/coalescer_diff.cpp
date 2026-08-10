#include "VDataCoalescer.h"
#include "golden_coalescer.hpp"
#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using Request = GoldenLaneRequest;
using InputVector = std::array<std::optional<Request>, 4>;

[[noreturn]] void fail(const std::string& message) {
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    std::exit(EXIT_FAILURE);
}

std::string hex(uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
    return buffer;
}

class DifferentialHarness {
  public:
    VDataCoalescer dut;
    GoldenCoalescer golden;
    uint64_t cycle = 0;

    DifferentialHarness() {
        dut.clock = 0;
        dut.reset = 1;
        dut.io_memReq_ready = 1;
        clear_response();
        clear_inputs();
        dut.eval();
        tick_reset();
        dut.reset = 0;
    }

    struct Result {
        std::array<bool, 4> ready{};
        bool mem_fire = false;
        GoldenMemoryRequest request{};
    };

    Result step(const InputVector& inputs, bool mem_ready,
                const GoldenMemoryResponse& response) {
        const GoldenCombinational expected = golden.comb();
        drive_inputs(inputs);
        dut.io_memReq_ready = mem_ready;
        drive_response(response);
        dut.clock = 0;
        dut.eval();

        for (unsigned lane = 0; lane < 4; ++lane) {
            const bool actual_ready = get_ready(lane);
            if (actual_ready != expected.core_ready[lane]) {
                fail("cycle " + std::to_string(cycle) + " lane " + std::to_string(lane) +
                     " ready mismatch");
            }
        }
        compare_memory_request(expected.mem_request);
        const bool mem_fire = expected.mem_request.valid && mem_ready;

        golden.step(inputs, mem_ready, response);
        dut.clock = 1;
        dut.eval();
        dut.clock = 0;
        dut.eval();

        const auto expected_responses = golden.core_responses();
        for (unsigned lane = 0; lane < 4; ++lane) {
            const bool actual_valid = get_response_valid(lane);
            const uint32_t actual_data = get_response_data(lane);
            if (actual_valid != expected_responses[lane].valid ||
                (actual_valid && actual_data != expected_responses[lane].data)) {
                fail("cycle " + std::to_string(cycle) + " lane " + std::to_string(lane) +
                     " response mismatch");
            }
        }
        ++cycle;
        return {expected.core_ready, mem_fire, expected.mem_request};
    }

  private:
    void tick_reset() {
        dut.clock = 1;
        dut.eval();
        dut.clock = 0;
        dut.eval();
        ++cycle;
    }

    void clear_inputs() {
        for (unsigned lane = 0; lane < 4; ++lane) {
            set_valid(lane, false);
            set_request(lane, Request{});
        }
    }

    void clear_response() {
        dut.io_memResp_valid = 0;
        for (unsigned word = 0; word < 8; ++word) set_response_word(word, 0);
    }

    void drive_inputs(const InputVector& inputs) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            set_valid(lane, inputs[lane].has_value());
            if (inputs[lane].has_value()) set_request(lane, *inputs[lane]);
        }
    }

    void drive_response(const GoldenMemoryResponse& response) {
        dut.io_memResp_valid = response.valid;
        for (unsigned word = 0; word < 8; ++word) {
            set_response_word(word, response.rdata[word]);
        }
    }

    void set_valid(unsigned lane, bool value) {
        switch (lane) {
            case 0: dut.io_coreReq_0_valid = value; break;
            case 1: dut.io_coreReq_1_valid = value; break;
            case 2: dut.io_coreReq_2_valid = value; break;
            case 3: dut.io_coreReq_3_valid = value; break;
        }
    }

    void set_request(unsigned lane, const Request& request) {
        switch (lane) {
            case 0: dut.io_coreReq_0_bits_addr = request.addr; dut.io_coreReq_0_bits_write = request.write; dut.io_coreReq_0_bits_wdata = request.wdata; dut.io_coreReq_0_bits_be = request.be; break;
            case 1: dut.io_coreReq_1_bits_addr = request.addr; dut.io_coreReq_1_bits_write = request.write; dut.io_coreReq_1_bits_wdata = request.wdata; dut.io_coreReq_1_bits_be = request.be; break;
            case 2: dut.io_coreReq_2_bits_addr = request.addr; dut.io_coreReq_2_bits_write = request.write; dut.io_coreReq_2_bits_wdata = request.wdata; dut.io_coreReq_2_bits_be = request.be; break;
            case 3: dut.io_coreReq_3_bits_addr = request.addr; dut.io_coreReq_3_bits_write = request.write; dut.io_coreReq_3_bits_wdata = request.wdata; dut.io_coreReq_3_bits_be = request.be; break;
        }
    }

    bool get_ready(unsigned lane) const {
        switch (lane) {
            case 0: return dut.io_coreReq_0_ready;
            case 1: return dut.io_coreReq_1_ready;
            case 2: return dut.io_coreReq_2_ready;
            default: return dut.io_coreReq_3_ready;
        }
    }

    bool get_response_valid(unsigned lane) const {
        switch (lane) {
            case 0: return dut.io_coreResp_0_valid;
            case 1: return dut.io_coreResp_1_valid;
            case 2: return dut.io_coreResp_2_valid;
            default: return dut.io_coreResp_3_valid;
        }
    }

    uint32_t get_response_data(unsigned lane) const {
        switch (lane) {
            case 0: return dut.io_coreResp_0_bits_data;
            case 1: return dut.io_coreResp_1_bits_data;
            case 2: return dut.io_coreResp_2_bits_data;
            default: return dut.io_coreResp_3_bits_data;
        }
    }

    void set_response_word(unsigned word, uint32_t value) {
        switch (word) {
            case 0: dut.io_memResp_bits_rdata_0 = value; break;
            case 1: dut.io_memResp_bits_rdata_1 = value; break;
            case 2: dut.io_memResp_bits_rdata_2 = value; break;
            case 3: dut.io_memResp_bits_rdata_3 = value; break;
            case 4: dut.io_memResp_bits_rdata_4 = value; break;
            case 5: dut.io_memResp_bits_rdata_5 = value; break;
            case 6: dut.io_memResp_bits_rdata_6 = value; break;
            case 7: dut.io_memResp_bits_rdata_7 = value; break;
        }
    }

    void compare_memory_request(const GoldenMemoryRequest& expected) const {
        const bool actual_valid = dut.io_memReq_valid;
        if (actual_valid != expected.valid) fail("memory request valid mismatch");
        if (!expected.valid) return;
        if (static_cast<bool>(dut.io_memReq_bits_write) != expected.write ||
            dut.io_memReq_bits_lineAddr != expected.line_addr ||
            dut.io_memReq_bits_byteen != expected.byteen) {
            fail("memory request control mismatch");
        }
        for (unsigned word = 0; word < 8; ++word) {
            uint32_t actual = 0;
            switch (word) {
                case 0: actual = dut.io_memReq_bits_wdata_0; break;
                case 1: actual = dut.io_memReq_bits_wdata_1; break;
                case 2: actual = dut.io_memReq_bits_wdata_2; break;
                case 3: actual = dut.io_memReq_bits_wdata_3; break;
                case 4: actual = dut.io_memReq_bits_wdata_4; break;
                case 5: actual = dut.io_memReq_bits_wdata_5; break;
                case 6: actual = dut.io_memReq_bits_wdata_6; break;
                case 7: actual = dut.io_memReq_bits_wdata_7; break;
            }
            if (actual != expected.wdata[word]) fail("memory request data mismatch");
        }
    }
};

using Memory = std::unordered_map<uint32_t, std::array<uint32_t, 8>>;

std::array<uint32_t, 8>& memory_line(Memory& memory, uint32_t address) {
    auto [it, inserted] = memory.try_emplace(address);
    if (inserted) {
        for (unsigned word = 0; word < 8; ++word) {
            it->second[word] = 0x10000000U ^ ((address + word * 4U) >> 2U);
        }
    }
    return it->second;
}

void apply_store(std::array<uint32_t, 8>& line, const GoldenMemoryRequest& request) {
    for (unsigned byte = 0; byte < 32; ++byte) {
        if (((request.byteen >> byte) & 1U) == 0U) continue;
        const unsigned word = byte / 4U;
        const unsigned shift = (byte % 4U) * 8U;
        const uint32_t mask = 0xffU << shift;
        line[word] = (line[word] & ~mask) | (request.wdata[word] & mask);
    }
}

struct PendingResponse {
    uint64_t due_cycle = 0;
    std::array<uint32_t, 8> data{};
};

Request make_request(std::mt19937& rng, uint32_t address) {
    Request request;
    request.addr = address & ~uint32_t{3};
    request.write = (rng() & 1U) != 0U;
    request.wdata = rng();
    request.be = static_cast<uint8_t>((rng() % 15U) + 1U);
    return request;
}

void run_case(uint32_t seed, unsigned case_index) {
    std::mt19937 rng(seed);
    std::array<std::queue<Request>, 4> queues;
    const uint32_t segment_base = 0x1000U + (rng() % 256U) * 32U;
    const uint32_t offset = (rng() % 8U) * 4U;
    const uint32_t stride_choices[] = {4U, 8U, 16U, 32U, 64U, 128U};
    const uint32_t stride = stride_choices[rng() % 6U];
    for (unsigned lane = 0; lane < 4; ++lane) {
        const unsigned count = 1U + rng() % 16U;
        for (unsigned index = 0; index < count; ++index) {
            const uint32_t lane_offset = (rng() % 8U) * 4U;
            const uint32_t address = segment_base + offset + lane_offset + index * stride;
            queues[lane].push(make_request(rng, address));
        }
    }

    DifferentialHarness harness;
    Memory memory;
    std::array<std::optional<Request>, 4> active_inputs{};
    std::optional<PendingResponse> pending_response;
    unsigned idle_cycles = 0;
    const uint64_t limit = 20000;
    for (uint64_t loop = 0; loop < limit; ++loop) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            if (!active_inputs[lane].has_value() && !queues[lane].empty() &&
                ((rng() % 4U) == 0U || idle_cycles > 8U)) {
                active_inputs[lane] = queues[lane].front();
            }
        }

        GoldenMemoryResponse response;
        if (pending_response.has_value() && pending_response->due_cycle == harness.cycle) {
            response.valid = true;
            response.rdata = pending_response->data;
        }
        const bool mem_ready = (rng() % 4U) != 0U;
        const auto result = harness.step(active_inputs, mem_ready, response);

        for (unsigned lane = 0; lane < 4; ++lane) {
            if (active_inputs[lane].has_value() && result.ready[lane]) {
                queues[lane].pop();
                active_inputs[lane].reset();
            }
        }

        if (response.valid) pending_response.reset();
        if (result.mem_fire) {
            if (pending_response.has_value()) fail("more than one pending memory response");
            auto& line = memory_line(memory, result.request.line_addr);
            PendingResponse pending;
            pending.due_cycle = harness.cycle + 1U + rng() % 4U;
            pending.data = line;
            if (result.request.write) apply_store(line, result.request);
            pending_response = pending;
        }

        bool queues_empty = pending_response.has_value() == false;
        for (unsigned lane = 0; lane < 4; ++lane) {
            queues_empty = queues_empty && queues[lane].empty() && !active_inputs[lane].has_value();
        }
        const bool model_idle = !harness.golden.comb().mem_request.valid && queues_empty;
        idle_cycles = model_idle ? idle_cycles + 1U : 0U;
        if (model_idle && idle_cycles >= 4U) return;
    }
    fail("timeout in randomized case " + std::to_string(case_index) + " seed " + std::to_string(seed));
}

void directed_conflict_store() {
    // This case protects the explicitly documented lowest-lane-wins rule.
    std::mt19937 rng(1);
    (void)rng;
    DifferentialHarness harness;
    InputVector inputs;
    inputs[0] = Request{0x1000U, true, 0xaaaaaaaaU, 0xfU};
    inputs[1] = Request{0x1000U, true, 0xbbbbbbbbU, 0xfU};
    inputs[2] = Request{0x1004U, true, 0xccccccccU, 0xfU};
    inputs[3] = Request{0x1008U, true, 0xddddddddU, 0xfU};
    harness.step(inputs, true, GoldenMemoryResponse{});
    harness.step(InputVector{}, true, GoldenMemoryResponse{});
    const auto third = harness.step(InputVector{}, true, GoldenMemoryResponse{});
    if (!third.mem_fire) fail("directed conflict case did not issue");
    if (third.request.wdata[0] != 0xaaaaaaaaU) fail("golden conflict rule is not lowest lane wins");
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    directed_conflict_store();
    constexpr unsigned cases = 10000;
    for (unsigned index = 0; index < cases; ++index) {
        run_case(0x5eed0000U + index * 7919U, index);
    }
    std::printf("PASS: %u randomized DataCoalescer differential cases\n", cases);
    return EXIT_SUCCESS;
}
