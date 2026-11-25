#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_instructions/arm_branch_and_swap.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Executes Thumb Formats 6-11 (PC/SP-relative, register-offset, immediate, halfword transfers).
 */
ArmStepResult execute_thumb_single_transfer(ArmCoreContext& context,
                                            u16 opcode,
                                            Address instruction_pc);

/**
 * @brief Executes Thumb Formats 12-15 (ADD PC/SP, SP adjust, PUSH/POP, STMIA/LDMIA).
 */
ArmStepResult execute_thumb_stack_and_block(ArmCoreContext& context,
                                            u16 opcode,
                                            Address instruction_pc);

}  // namespace elmasds::cpu
