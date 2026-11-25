#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_instructions/arm_branch_and_swap.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Executes Thumb Format 1 (move shifted register) and Format 2 (add/subtract).
 */
ArmStepResult execute_thumb_shift_or_addsub(ArmCoreContext& context, u16 opcode);

/**
 * @brief Executes Thumb Format 3 (MOV/CMP/ADD/SUB immediate) and Format 4 (ALU register ops).
 */
ArmStepResult execute_thumb_alu(ArmCoreContext& context, u16 opcode);

/**
 * @brief Executes Thumb Format 5 (Hi-reg ADD/CMP/MOV and BX/BLX reg).
 */
ArmStepResult execute_thumb_hi_reg_or_bx(ArmCoreContext& context,
                                         u16 opcode,
                                         Address instruction_pc);

/**
 * @brief Executes Thumb Formats 16-19 (conditional/unconditional branch, SWI, BL/BLX long).
 */
ArmStepResult execute_thumb_branch_or_swi(ArmCoreContext& context,
                                          u16 opcode,
                                          Address instruction_pc);

}  // namespace elmasds::cpu
