#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_instructions/arm_branch_and_swap.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Executes ARM single data transfer (`LDR`, `LDRB`, `STR`, `STRB`).
 */
ArmStepResult execute_arm_single_transfer(ArmCoreContext& context,
                                          u32 opcode,
                                          Address instruction_pc);

/**
 * @brief Executes ARM halfword, signed byte/halfword, and ARMv5TE doubleword transfers.
 */
ArmStepResult execute_arm_halfword_or_double(ArmCoreContext& context,
                                             u32 opcode,
                                             Address instruction_pc);

/**
 * @brief Executes ARM block data transfer (`LDM` and `STM`), including ARMv4/v5 writeback rules.
 */
ArmStepResult execute_arm_block_transfer(ArmCoreContext& context,
                                         u32 opcode,
                                         Address instruction_pc);

}  // namespace elmasds::cpu
