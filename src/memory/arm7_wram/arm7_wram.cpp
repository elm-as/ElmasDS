#include "memory/arm7_wram/arm7_wram.hpp"

#include <algorithm>

namespace elmasds::memory {

Arm7Wram::Arm7Wram() : storage_(kSizeBytes, 0U) {}

void Arm7Wram::reset() noexcept {
    std::fill(storage_.begin(), storage_.end(), u8{0});
}

}  // namespace elmasds::memory
