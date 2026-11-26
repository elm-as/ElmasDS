#pragma once

#include "primitives/fixed_width_types.hpp"

#include <cstddef>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace elmasds::frontend {

/**
 * @brief Bounded LIFO ring buffer storing serialized save-state snapshots for instant rewind.
 */
class RewindBuffer final {
public:
    explicit RewindBuffer(std::size_t max_snapshots = 40U) noexcept
        : max_snapshots_(max_snapshots > 0U ? max_snapshots : 1U) {}

    void push(std::vector<u8> snapshot) {
        if (snapshots_.size() >= max_snapshots_) {
            snapshots_.pop_front();
        }
        snapshots_.push_back(std::move(snapshot));
    }

    [[nodiscard]] std::optional<std::vector<u8>> pop() {
        if (snapshots_.empty()) {
            return std::nullopt;
        }
        std::vector<u8> latest = std::move(snapshots_.back());
        snapshots_.pop_back();
        return latest;
    }

    void clear() noexcept { snapshots_.clear(); }
    [[nodiscard]] std::size_t size() const noexcept { return snapshots_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return max_snapshots_; }

private:
    std::size_t max_snapshots_;
    std::deque<std::vector<u8>> snapshots_;
};

}  // namespace elmasds::frontend
