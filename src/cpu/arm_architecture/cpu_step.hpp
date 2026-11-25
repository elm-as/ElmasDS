#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Active-high hardware interrupt line levels sampled prior to each instruction step.
 */
struct InterruptLines {
    bool irq = false;
    bool fiq = false;
};

/**
 * @brief Advances an ARM core by one step, handling WFI wake, FIQ/IRQ dispatch, and execution.
 *
 * Responsibility:
 *   Wakes a halted core when IRQ, FIQ, or `wake_pending` is asserted, dispatches FIQ (priority)
 *   or IRQ when unmasked in CPSR, idles for 1 cycle while still halted, or executes the next
 *   ARM/Thumb instruction.
 * Dependencies:
 *   `ArmCoreContext`, `exception_vector.hpp`, `arm_executor.hpp`, `thumb_executor.hpp`.
 * @param context Core execution state including register bank, bus, halt flag, and coprocessor.
 * @param interrupts Current external IRQ and FIQ line levels.
 * @return Cycles consumed by the interrupt entry, idle tick, or executed instruction.
 */
u32 step_arm_core(ArmCoreContext& context, const InterruptLines& interrupts);

}  // namespace elmasds::cpu
