#include "golden_coalescer.hpp"

std::optional<unsigned> GoldenCoalescer::seed_lane() const {
    for (unsigned lane = 0; lane < 4; ++lane) {
        if (buffers_[lane].has_value()) return lane;
    }
    return std::nullopt;
}

GoldenCoalescer::Active GoldenCoalescer::select_active() const {
    Active selected;
    const auto seed = seed_lane();
    if (!seed.has_value()) return selected;

    const GoldenLaneRequest& seed_request = *buffers_[*seed];
    selected.write = seed_request.write;
    selected.line_addr = seed_request.addr & ~uint32_t{31};
    for (unsigned lane = 0; lane < 4; ++lane) {
        if (!buffers_[lane].has_value()) continue;
        const GoldenLaneRequest& request = *buffers_[lane];
        const bool same_line = (request.addr & ~uint32_t{31}) == selected.line_addr;
        if (!same_line || request.write != selected.write) continue;
        selected.mask |= static_cast<uint8_t>(1U << lane);
        selected.addr[lane] = request.addr;
    }

    if (!selected.write) return selected;

    for (unsigned word = 0; word < 8; ++word) {
        for (unsigned lane = 0; lane < 4; ++lane) {
            if ((selected.mask & (1U << lane)) == 0U ||
                ((buffers_[lane]->addr >> 2U) & 7U) != word) {
                continue;
            }
            // Lowest lane wins if multiple stores target one word.
            selected.wdata[word] = buffers_[lane]->wdata;
            selected.byteen |= static_cast<uint32_t>(buffers_[lane]->be) << (word * 4U);
            break;
        }
    }
    return selected;
}

GoldenCombinational GoldenCoalescer::comb() const {
    GoldenCombinational output;
    for (unsigned lane = 0; lane < 4; ++lane) {
        output.core_ready[lane] = !buffers_[lane].has_value();
    }
    if (state_ != State::Issue) return output;
    output.mem_request.valid = true;
    output.mem_request.write = active_.write;
    output.mem_request.line_addr = active_.line_addr;
    output.mem_request.wdata = active_.wdata;
    output.mem_request.byteen = active_.byteen;
    output.mem_request.active_mask = active_.mask;
    return output;
}

void GoldenCoalescer::step(
    const std::array<std::optional<GoldenLaneRequest>, 4>& inputs,
    bool mem_request_ready, const GoldenMemoryResponse& mem_response) {
    for (auto& response : responses_) response = GoldenCoreResponse{};

    const GoldenCombinational current = comb();
    const bool collect_has_buffer = state_ == State::Collect && seed_lane().has_value();
    const Active collect_active = collect_has_buffer ? select_active() : Active{};
    for (unsigned lane = 0; lane < 4; ++lane) {
        if (inputs[lane].has_value() && current.core_ready[lane]) {
            buffers_[lane] = inputs[lane];
        }
    }

    switch (state_) {
        case State::Collect:
            if (collect_has_buffer) {
                active_ = collect_active;
                state_ = State::Issue;
            }
            break;
        case State::Issue:
            if (mem_request_ready) state_ = State::Wait;
            break;
        case State::Wait:
            if (mem_response.valid) {
                for (unsigned lane = 0; lane < 4; ++lane) {
                    if ((active_.mask & (1U << lane)) == 0U) continue;
                    responses_[lane].valid = true;
                    responses_[lane].data = active_.write
                        ? 0U
                        : mem_response.rdata[(active_.addr[lane] >> 2U) & 7U];
                    buffers_[lane].reset();
                }
                state_ = State::Collect;
            }
            break;
    }
}
