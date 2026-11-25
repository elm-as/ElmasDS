#include "cpu/arm9/arm946_es.hpp"

namespace elmasds::cpu {

Arm946Es::Arm946Es(CpuBusInterface& external_bus) noexcept
    : registers_(ArmArchitecture::ArmV5Te),
      tcm_bus_(external_bus, cp15_),
      context_{registers_, tcm_bus_, false, false, &cp15_} {}

void Arm946Es::reset(Address reset_pc, bool high_vectors) noexcept {
    registers_.reset(reset_pc);
    cp15_.reset(high_vectors);
    tcm_bus_.reset();
    context_.high_vectors = cp15_.high_vectors();
    context_.halted = false;
    context_.wake_pending = false;
    interrupts_ = InterruptLines{};
    total_cycles_ = 0U;
}

u32 Arm946Es::step() {
    context_.high_vectors = cp15_.high_vectors();
    const u32 cycles = step_arm_core(context_, interrupts_);
    context_.high_vectors = cp15_.high_vectors();
    // Real ARM946E-S does not remain halted if an interrupt or wake condition is already pending.
    if (cp15_.consume_wfi_request() && !interrupts_.irq && !interrupts_.fiq &&
        !context_.wake_pending) {
        context_.halted = true;
    }
    total_cycles_ += cycles;
    return cycles;
}

u64 Arm946Es::run_cycles(u32 cycle_budget) {
    u64 elapsed = 0U;
    while (elapsed < cycle_budget) {
        elapsed += step();
    }
    return elapsed;
}

void Arm946Es::set_irq_line(bool asserted) noexcept {
    interrupts_.irq = asserted;
}

void Arm946Es::set_fiq_line(bool asserted) noexcept {
    interrupts_.fiq = asserted;
}

void Arm946Es::set_wake_pending(bool pending) noexcept {
    context_.wake_pending = pending;
}

RegisterBank& Arm946Es::registers() noexcept {
    return registers_;
}

const RegisterBank& Arm946Es::registers() const noexcept {
    return registers_;
}

Cp15SystemControl& Arm946Es::cp15() noexcept {
    return cp15_;
}

const Cp15SystemControl& Arm946Es::cp15() const noexcept {
    return cp15_;
}

ArmCoreContext& Arm946Es::context() noexcept {
    return context_;
}

const ArmCoreContext& Arm946Es::context() const noexcept {
    return context_;
}

bool Arm946Es::is_halted() const noexcept {
    return context_.halted;
}

TickCount Arm946Es::total_cycles() const noexcept {
    return total_cycles_;
}

}  // namespace elmasds::cpu
