#pragma once

#include <array>
#include <cstdint>
#include <optional>

struct GoldenLaneRequest {
    uint32_t addr = 0;
    bool write = false;
    uint32_t wdata = 0;
    uint8_t be = 0xf;
};

struct GoldenMemoryResponse {
    bool valid = false;
    std::array<uint32_t, 8> rdata{};
};

struct GoldenMemoryRequest {
    bool valid = false;
    bool write = false;
    uint32_t line_addr = 0;
    std::array<uint32_t, 8> wdata{};
    uint32_t byteen = 0;
    uint8_t active_mask = 0;
};

struct GoldenCoreResponse {
    bool valid = false;
    uint32_t data = 0;
};

struct GoldenCombinational {
    std::array<bool, 4> core_ready{};
    GoldenMemoryRequest mem_request{};
};

class GoldenCoalescer {
  public:
    GoldenCoalescer() = default;

    GoldenCombinational comb() const;
    void step(const std::array<std::optional<GoldenLaneRequest>, 4>& inputs,
              bool mem_request_ready, const GoldenMemoryResponse& mem_response);

    [[nodiscard]] std::array<GoldenCoreResponse, 4> core_responses() const {
        return responses_;
    }

  private:
    enum class State { Collect, Issue, Wait };
    struct Active {
        uint8_t mask = 0;
        std::array<uint32_t, 4> addr{};
        bool write = false;
        uint32_t line_addr = 0;
        std::array<uint32_t, 8> wdata{};
        uint32_t byteen = 0;
    };

    State state_ = State::Collect;
    std::array<std::optional<GoldenLaneRequest>, 4> buffers_{};
    Active active_{};
    std::array<GoldenCoreResponse, 4> responses_{};

    [[nodiscard]] std::optional<unsigned> seed_lane() const;
    [[nodiscard]] Active select_active() const;
};
