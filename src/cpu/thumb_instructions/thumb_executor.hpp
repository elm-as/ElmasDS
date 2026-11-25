#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_instructions/arm_branch_and_swap.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Fetches, decodes, and executes a single 16-bit Thumb instruction at current PC.
 *
 * Responsibility:
 *   Decodes Formats 1-19 (plus ARMv5TE `BLX` and `BKPT`), dispatches execution,
 *   raises `Undefined` traps for invalid encodings, and advances PC by 2 when
 *   no branch occurred.
 */
ArmStepResult step_thumb_instruction(ArmCoreContext& context);

}  // namespace elmasds::cpu
