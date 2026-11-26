#include "memory/main_ram/main_ram.hpp"

#include <algorithm>

namespace elmasds::memory {

MainRam::MainRam() : storage_(kSizeBytes, 0U) {}

void MainRam::reset() noexcept {
    std::fill(storage_.begin(), storage_.end(), u8{0});
}

}  // namespace elmasds::memory
