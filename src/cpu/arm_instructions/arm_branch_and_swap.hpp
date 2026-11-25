#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Result of executing one ARM instruction, distinguishing self-branches from fallthrough.
 */
struct ArmStepResult {
    u32 cycles = 1;
    bool pc_written = false;
};

/**
 * @brief Executes `B` or `BL`.
 */
ArmStepResult execute_arm_branch(ArmCoreContext& context, u32 opcode, Address instruction_pc);

/**
 * @brief Executes `BX` or `BLX` (register form).
 */
ArmStepResult execute_arm_branch_exchange_reg(ArmCoreContext& context,
                                              u32 opcode,
                                              Address instruction_pc);

/**
 * @brief Executes ARMv5TE `BLX` (immediate form).
 */
ArmStepResult execute_arm_blx_immediate(ArmCoreContext& context,
                                        u32 opcode,
                                        Address instruction_pc);

/**
 * @brief Executes `SWP` and `SWPB`.
 */
ArmStepResult execute_arm_swap(ArmCoreContext& context, u32 opcode);

/**
 * @brief Executes `SWI` (Software Interrupt).
 */
ArmStepResult execute_arm_swi(ArmCoreContext& context, u32 opcode, Address instruction_pc);

}  // namespace elmasds::cpu
