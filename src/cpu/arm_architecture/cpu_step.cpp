#include "cpu/arm_architecture/cpu_step.hpp"

#include "cpu/arm_architecture/exception_vector.hpp"
#include "cpu/arm_instructions/arm_executor.hpp"
#include "cpu/thumb_instructions/thumb_executor.hpp"

namespace elmasds::cpu {

u32 step_arm_core(ArmCoreContext& context, const InterruptLines& interrupts) {
    if (interrupts.irq || interrupts.fiq || context.wake_pending) {
        context.halted = false;
    }

    const StatusRegister cpsr = context.registers.cpsr();
    const Address current_pc =
        context.registers.read(RegisterIndex::Pc) & (cpsr.thumb() ? ~1U : ~3U);

    if (interrupts.fiq && !cpsr.fiq_disabled()) {
        raise_exception(context, CpuException::Fiq, current_pc);
        return 3U;
    }

    if (interrupts.irq && !cpsr.irq_disabled()) {
        raise_exception(context, CpuException::Irq, current_pc);
        return 3U;
    }

    if (context.halted) {
        return 1U;
    }

    return cpsr.thumb() ? step_thumb_instruction(context).cycles
                        : step_arm_instruction(context).cycles;
}

}  // namespace elmasds::cpu
