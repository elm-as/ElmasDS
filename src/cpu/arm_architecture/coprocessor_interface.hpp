#pragma once

#include "primitives/fixed_width_types.hpp"

#include <optional>

namespace elmasds::cpu {

/**
 * @brief Decoded parameters for an ARM MCR/MRC coprocessor register transfer.
 */
struct CoprocessorTransfer {
    u8 coprocessor = 0;
    u8 opcode_1 = 0;
    u8 crn = 0;
    u8 crm = 0;
    u8 opcode_2 = 0;
    bool privileged = false;
};

/**
 * @brief Abstract coprocessor boundary enabling ARM cores to attach CP15 or custom coprocessors.
 *
 * Responsibility:
 *   Handles ARM MCR (write) and MRC (read) transfers; returning `false` or `std::nullopt`
 *   causes the executing core to trap to the Undefined instruction exception vector.
 * Dependencies:
 *   `fixed_width_types.hpp`, `<optional>`.
 */
class CoprocessorInterface {
public:
    virtual ~CoprocessorInterface() = default;

    /**
     * @brief Executes an MCR transfer writing `value` to the addressed coprocessor register.
     * @param transfer Decoded coprocessor number, opcodes, register indices, and privilege bit.
     * @param value Source ARM register value (`Rd`).
     * @return True if the transfer succeeded; false to trap to `CpuException::Undefined`.
     */
    virtual bool write_register(const CoprocessorTransfer& transfer, u32 value) = 0;

    /**
     * @brief Executes an MRC transfer reading the addressed coprocessor register.
     * @param transfer Decoded coprocessor number, opcodes, register indices, and privilege bit.
     * @return Register value on success; `std::nullopt` to trap to `CpuException::Undefined`.
     */
    [[nodiscard]] virtual std::optional<u32> read_register(
        const CoprocessorTransfer& transfer) = 0;
};

}  // namespace elmasds::cpu
