#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Executes an ARM Data Processing instruction (`AND`..`MVN`), updating flags/PC if required.
 * @return Master cycle count consumed by the instruction.
 */
u32 execute_arm_data_processing(ArmCoreContext& context, u32 opcode, Address instruction_pc);

/**
 * @brief Executes `MRS` (copy CPSR or SPSR to Rd).
 */
u32 execute_arm_mrs(ArmCoreContext& context, u32 opcode);

/**
 * @brief Executes `MSR` (register or immediate form) respecting mode privilege rules.
 */
u32 execute_arm_msr(ArmCoreContext& context, u32 opcode, bool immediate_form);

/**
 * @brief Executes `MUL` and `MLA`.
 */
u32 execute_arm_multiply(ArmCoreContext& context, u32 opcode);

/**
 * @brief Executes `UMULL`, `UMLAL`, `SMULL`, and `SMLAL`.
 */
u32 execute_arm_multiply_long(ArmCoreContext& context, u32 opcode);

/**
 * @brief Executes ARMv5TE `CLZ` (Count Leading Zeros).
 */
u32 execute_arm_clz(ArmCoreContext& context, u32 opcode);

/**
 * @brief Executes ARMv5TE saturating arithmetic (`QADD`, `QSUB`, `QDADD`, `QDSUB`).
 */
u32 execute_arm_saturating(ArmCoreContext& context, u32 opcode);

/**
 * @brief Executes ARMv5TE DSP 16-bit multiplies (`SMLAxy`, `SMLAWy`, `SMULWy`, `SMLALxy`, `SMULxy`).
 */
u32 execute_arm_dsp_multiply(ArmCoreContext& context, u32 opcode);

}  // namespace elmasds::cpu
