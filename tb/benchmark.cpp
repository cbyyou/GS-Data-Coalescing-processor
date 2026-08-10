#include "Vdata_coalescing_system.h"
#include "verilated.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

struct Workload {
    std::array<uint32_t, 4> base{};
    std::array<uint32_t, 4> stride{};
    std::array<uint16_t, 4> count{};
    std::array<uint32_t, 4> wdata_base{};
    std::array<uint32_t, 4> start_delay{};
    uint8_t write_mask = 0;
};

struct Result {
    std::string name;
    uint64_t requests;
    uint64_t transactions;
    uint64_t cycles;
};

class Harness {
  public:
    Vdata_coalescing_system dut;
    uint64_t cycles = 0;
    uint64_t transactions = 0;

    Harness() {
        dut.clk = 0;
        dut.reset = 1;
        dut.core_start = 0;
        dut.mem_req_ready = 1;
        dut.mem_rsp_valid = 0;
        dut.eval();
        for (int i = 0; i < 3; ++i) tick();
        dut.reset = 0;
    }

    Result run(const std::string& name, const Workload& w) {
        configure(w);
        const uint64_t start_cycles = cycles;
        const uint64_t start_transactions = transactions;
        uint8_t done_seen = 0;
        uint64_t requested = 0;
        for (const auto count : w.count) requested += count;

        const uint64_t watchdog_limit = requested * 20 + 100;
        for (uint64_t elapsed = 0; elapsed < watchdog_limit; ++elapsed) {
            uint8_t start_mask = 0;
            for (unsigned core = 0; core < 4; ++core)
                if (elapsed == w.start_delay[core]) start_mask |= 1u << core;
            dut.core_start = start_mask;
            tick();
            dut.core_start = 0;
            done_seen |= dut.core_done;
            if (done_seen == 0xf) {
                require_completed(w);
                return {name, requested, transactions - start_transactions,
                        cycles - start_cycles};
            }
        }
        std::fprintf(stderr, "FAIL: timeout in workload %s\n", name.c_str());
        std::exit(2);
    }

    static uint32_t initial_word(uint32_t byte_addr) {
        return 0x10000000u ^ (byte_addr >> 2);
    }

  private:
    bool pending_response_ = false;
    bool next_response_ = false;
    std::array<uint32_t, 8> pending_data_{};
    std::array<uint32_t, 8> next_data_{};
    std::unordered_map<uint32_t, std::array<uint32_t, 8>> memory_;

    void tick() {
        dut.clk = 0;
        dut.mem_rsp_valid = pending_response_;
        dut.mem_rdata0 = pending_data_[0]; dut.mem_rdata1 = pending_data_[1];
        dut.mem_rdata2 = pending_data_[2]; dut.mem_rdata3 = pending_data_[3];
        dut.mem_rdata4 = pending_data_[4]; dut.mem_rdata5 = pending_data_[5];
        dut.mem_rdata6 = pending_data_[6]; dut.mem_rdata7 = pending_data_[7];
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

    void configure(const Workload& w) {
        dut.core_base0 = w.base[0]; dut.core_base1 = w.base[1];
        dut.core_base2 = w.base[2]; dut.core_base3 = w.base[3];
        dut.core_stride0 = w.stride[0]; dut.core_stride1 = w.stride[1];
        dut.core_stride2 = w.stride[2]; dut.core_stride3 = w.stride[3];
        dut.core_count0 = w.count[0]; dut.core_count1 = w.count[1];
        dut.core_count2 = w.count[2]; dut.core_count3 = w.count[3];
        dut.core_write = w.write_mask;
        dut.core_wdata_base0 = w.wdata_base[0];
        dut.core_wdata_base1 = w.wdata_base[1];
        dut.core_wdata_base2 = w.wdata_base[2];
        dut.core_wdata_base3 = w.wdata_base[3];
    }

    void require_completed(const Workload& w) const {
        const std::array<uint16_t, 4> completed = {
            dut.core_completed0, dut.core_completed1,
            dut.core_completed2, dut.core_completed3};
        for (unsigned core = 0; core < 4; ++core) {
            if (completed[core] != w.count[core]) {
                std::fprintf(stderr, "FAIL: core %u completed %u, expected %u\n",
                             core, completed[core], w.count[core]);
                std::exit(1);
            }
        }
    }

    std::array<uint32_t, 8>& line(uint32_t line_addr) {
        auto [it, inserted] = memory_.try_emplace(line_addr);
        if (inserted) {
            for (unsigned word = 0; word < 8; ++word)
                it->second[word] = initial_word(line_addr + word * 4);
        }
        return it->second;
    }

    void capture_memory_request() {
        if (!(dut.mem_req_valid && dut.mem_req_ready)) return;
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

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static Workload uniform_workload(uint32_t base, uint32_t line_span,
                                 const std::array<uint32_t, 4>& offsets,
                                 uint16_t count = 1000) {
    Workload w;
    for (unsigned core = 0; core < 4; ++core) {
        w.base[core] = base + offsets[core];
        w.stride[core] = line_span;
        w.count[core] = count;
    }
    return w;
}

static void functional_tests() {
    Harness h;
    auto loads = uniform_workload(0x800, 32, {0, 4, 8, 12}, 1);
    const auto load_result = h.run("functional_load", loads);
    require(load_result.transactions == 1, "four contiguous loads must coalesce");
    require(h.dut.core_last_rdata0 == Harness::initial_word(0x800), "core 0 load routing");
    require(h.dut.core_last_rdata1 == Harness::initial_word(0x804), "core 1 load routing");
    require(h.dut.core_last_rdata2 == Harness::initial_word(0x808), "core 2 load routing");
    require(h.dut.core_last_rdata3 == Harness::initial_word(0x80c), "core 3 load routing");

    auto stores = uniform_workload(0x900, 32, {0, 4, 8, 12}, 1);
    stores.write_mask = 0xf;
    stores.wdata_base = {0xa5000000, 0xa5000001, 0xa5000002, 0xa5000003};
    const auto store_result = h.run("functional_store", stores);
    require(store_result.transactions == 1, "four contiguous stores must coalesce");
    const auto verify_result = h.run("functional_verify", uniform_workload(0x900, 32, {0, 4, 8, 12}, 1));
    require(verify_result.transactions == 1, "stored line readback must coalesce");
    require(h.dut.core_last_rdata0 == 0xa5000000, "core 0 store merge");
    require(h.dut.core_last_rdata1 == 0xa5000001, "core 1 store merge");
    require(h.dut.core_last_rdata2 == 0xa5000002, "core 2 store merge");
    require(h.dut.core_last_rdata3 == 0xa5000003, "core 3 store merge");

    auto conflicting_stores = uniform_workload(0x980, 32, {0, 0, 4, 8}, 1);
    conflicting_stores.write_mask = 0xf;
    conflicting_stores.wdata_base = {0xb1000000, 0xb2000000, 0xb3000000, 0xb4000000};
    const auto conflict_store_result = h.run("functional_store_conflict", conflicting_stores);
    require(conflict_store_result.transactions == 1,
            "same-segment store conflict must remain one transaction");
    auto conflicting_reads = uniform_workload(0x980, 32, {0, 0, 4, 8}, 1);
    const auto conflict_read_result = h.run("functional_store_conflict_verify", conflicting_reads);
    require(conflict_read_result.transactions == 1,
            "same-segment conflict readback must remain one transaction");
    require(h.dut.core_last_rdata0 == 0xb1000000 && h.dut.core_last_rdata1 == 0xb1000000,
            "lowest lane must win same-word store conflict");

    auto staggered = uniform_workload(0xa00, 32, {0, 4, 8, 12}, 1);
    staggered.start_delay = {0, 1, 2, 3};
    const auto staggered_result = h.run("functional_staggered", staggered);
    require(staggered_result.transactions > 1 && staggered_result.transactions <= 4,
            "independent arrival must remain functional");
}

static void parameter_sweep() {
    const std::array<uint32_t, 6> strides = {4, 8, 16, 32, 64, 128};
    const std::array<uint32_t, 7> skew_steps = {0, 1, 2, 3, 4, 6, 8};
    std::ofstream csv("build/parameter_sweep.csv");
    csv << "stride,start_skew,core_requests,memory_transactions,requests_per_transaction,"
           "transaction_reduction_percent,cycles,requests_per_cycle\n";
    std::printf("\nparameter sweep (four independent cores, 100 requests/core)\n");
    std::printf("%-8s %-10s %10s %10s %10s %10s %10s\n",
                "stride", "start_skew", "mem_txn", "req/txn", "reduct%", "cycles",
                "req/cycle");

    for (const uint32_t stride : strides) {
        for (const uint32_t skew : skew_steps) {
            Harness h;
            auto workload = uniform_workload(0x50000 + stride * 0x100,
                                              stride, {0, 4, 8, 12}, 100);
            workload.start_delay = {0, skew, 2 * skew, 3 * skew};
            const auto result = h.run("parameter_sweep", workload);
            const double rpt = static_cast<double>(result.requests) / result.transactions;
            const double reduction = 100.0 *
                (1.0 - static_cast<double>(result.transactions) / result.requests);
            const double rpc = static_cast<double>(result.requests) / result.cycles;
            std::printf("%-8u %-10u %10llu %10.3f %10.2f %10llu %10.3f\n",
                        stride, skew,
                        static_cast<unsigned long long>(result.transactions), rpt, reduction,
                        static_cast<unsigned long long>(result.cycles), rpc);
            csv << stride << ',' << skew << ',' << result.requests << ',' << result.transactions
                << ',' << rpt << ',' << reduction << ',' << result.cycles << ',' << rpc << '\n';
        }
    }
    std::printf("parameter sweep CSV: build/parameter_sweep.csv\n");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    functional_tests();

    if (argc > 1 && std::string(argv[1]) == "--sweep") {
        parameter_sweep();
        return 0;
    }

    std::vector<Result> results;
    {
        Harness h;
        results.push_back(h.run("contiguous_1_segment",
                                uniform_workload(0x10000, 32, {0, 4, 8, 12})));
    }
    {
        Harness h;
        results.push_back(h.run("paired_2_segments",
                                uniform_workload(0x20000, 64, {0, 4, 32, 36})));
    }
    {
        Harness h;
        results.push_back(h.run("strided_4_segments",
                                uniform_workload(0x30000, 128, {0, 32, 64, 96})));
    }
    {
        Harness h;
        auto w = uniform_workload(0x40000, 32, {0, 4, 8, 12});
        w.start_delay = {0, 2, 4, 6};
        results.push_back(h.run("staggered_independent", w));
    }

    std::ofstream csv("build/benchmark.csv");
    csv << "pattern,core_requests,memory_transactions,requests_per_transaction,"
           "transaction_reduction_percent,cycles,requests_per_cycle\n";
    std::printf("%-24s %10s %10s %10s %10s %10s\n", "pattern", "requests", "mem_txn",
                "req/txn", "reduct%", "req/cycle");
    for (const auto& r : results) {
        const double rpt = static_cast<double>(r.requests) / r.transactions;
        const double reduction = 100.0 * (1.0 - static_cast<double>(r.transactions) / r.requests);
        const double rpc = static_cast<double>(r.requests) / r.cycles;
        std::printf("%-24s %10llu %10llu %10.3f %10.2f %10.3f\n", r.name.c_str(),
                    static_cast<unsigned long long>(r.requests),
                    static_cast<unsigned long long>(r.transactions), rpt, reduction, rpc);
        csv << r.name << ',' << r.requests << ',' << r.transactions << ',' << rpt << ','
            << reduction << ',' << r.cycles << ',' << rpc << '\n';
    }
    std::printf("functional tests: PASS\nCSV: build/benchmark.csv\n");
    return 0;
}
