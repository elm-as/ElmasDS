#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Hardware exception vectors defined by ARMv4T and ARMv5TE.
 */
enum class CpuException : u8 {
    Reset = 0x00,
    Undefined = 0x04,
    SoftwareInterrupt = 0x08,
    PrefetchAbort = 0x0C,
    DataAbort = 0x10,
    Irq = 0x18,
    Fiq = 0x1C,
};

/**
 * @brief Enters an ARM hardware exception vector, banking CPSR -> SPSR and updating LR/PC/mode.
 * @param context Execution state (registers + HIVECS setting).
 * @param exception Exception vector to raise.
 * @param current_pc Address of the instruction that triggered or was interrupted by the exception.
 */
void raise_exception(ArmCoreContext& context, CpuException exception, Address current_pc) noexcept;

}  // namespace elmasds::cpu
