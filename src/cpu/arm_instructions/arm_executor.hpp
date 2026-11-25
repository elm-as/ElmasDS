#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_instructions/arm_branch_and_swap.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Fetches, decodes, and executes a single 32-bit ARM instruction at current PC.
 *
 * Responsibility:
 *   Evaluates condition codes (including ARMv5TE `0xF` unconditional encodings),
 *   dispatches to the matching ARM instruction executor, raises `Undefined` traps
 *   for illegal opcodes, and advances PC by 4 on non-branching instructions.
 * Dependencies:
 *   `ArmCoreContext`, `arm_decoder.hpp`, `arm_data_processing.hpp`,
 *   `arm_branch_and_swap.hpp`, `arm_memory_transfer.hpp`, `exception_vector.hpp`.
 * @return Cycle count and whether PC was explicitly written.
 */
ArmStepResult step_arm_instruction(ArmCoreContext& context);

}  // namespace elmasds::cpu
