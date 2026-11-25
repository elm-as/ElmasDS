#include "cpu/arm_architecture/exception_vector.hpp"

namespace elmasds::cpu {
namespace {

struct ExceptionProfile {
    CpuMode target_mode = CpuMode::Supervisor;
    bool disable_fiq = false;
    u32 thumb_lr_offset = 4;
    u32 arm_lr_offset = 4;
};

[[nodiscard]] constexpr ExceptionProfile profile_for(CpuException exception) noexcept {
    switch (exception) {
    case CpuException::Reset:
        return {CpuMode::Supervisor, true, 0U, 0U};
    case CpuException::Undefined:
        return {CpuMode::Undefined, false, 2U, 4U};
    case CpuException::SoftwareInterrupt:
        return {CpuMode::Supervisor, false, 2U, 4U};
    case CpuException::PrefetchAbort:
        return {CpuMode::Abort, false, 4U, 4U};
    case CpuException::DataAbort:
        return {CpuMode::Abort, false, 8U, 8U};
    case CpuException::Irq:
        return {CpuMode::Irq, false, 4U, 4U};
    case CpuException::Fiq:
        return {CpuMode::Fiq, true, 4U, 4U};
    }
    return {CpuMode::Supervisor, true, 0U, 0U};
}

}  // namespace

void raise_exception(ArmCoreContext& context, CpuException exception, Address current_pc) noexcept {
    const ExceptionProfile profile = profile_for(exception);
    const StatusRegister old_cpsr = context.registers.cpsr();
    const u32 lr_offset = old_cpsr.thumb() ? profile.thumb_lr_offset : profile.arm_lr_offset;

    StatusRegister next_cpsr = old_cpsr.with_mode(profile.target_mode)
                                   .with_thumb(false)
                                   .with_irq_disabled(true);
    if (profile.disable_fiq) {
        next_cpsr = next_cpsr.with_fiq_disabled(true);
    }

    context.registers.set_cpsr(next_cpsr);
    if (exception != CpuException::Reset) {
        context.registers.set_spsr(old_cpsr);
        context.registers.write(RegisterIndex::Lr, current_pc + lr_offset);
    }

    const Address vector_base = context.high_vectors ? 0xFFFF0000U : 0x00000000U;
    context.registers.write(RegisterIndex::Pc, vector_base + static_cast<Address>(exception));
    context.halted = false;
}

}  // namespace elmasds::cpu
