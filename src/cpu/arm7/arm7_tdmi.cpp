#include "cpu/arm7/arm7_tdmi.hpp"

namespace elmasds::cpu {

Arm7Tdmi::Arm7Tdmi(CpuBusInterface& bus) noexcept
    : registers_(ArmArchitecture::ArmV4T),
      context_{registers_, bus, false, false, nullptr} {}

void Arm7Tdmi::reset(Address reset_pc) noexcept {
    registers_.reset(reset_pc);
    context_.high_vectors = false;
    context_.halted = false;
    context_.wake_pending = false;
    interrupts_ = InterruptLines{};
    total_cycles_ = 0U;
}

u32 Arm7Tdmi::step() {
    const u32 cycles = step_arm_core(context_, interrupts_);
    total_cycles_ += cycles;
    return cycles;
}

u64 Arm7Tdmi::run_cycles(u32 cycle_budget) {
    u64 elapsed = 0U;
    while (elapsed < cycle_budget) {
        elapsed += step();
    }
    return elapsed;
}

TickCount Arm7Tdmi::run_slice_ticks(TickCount tick_budget) {
    while (tick_budget >= elmasds::kArm7TicksPerCycle) {
        if (can_skip_halt()) {
            return 0ULL;
        }
        const TickCount used = static_cast<TickCount>(step()) * elmasds::kArm7TicksPerCycle;
        tick_budget = (used >= tick_budget) ? 0ULL : (tick_budget - used);
    }
    return tick_budget;
}

void Arm7Tdmi::set_irq_line(bool asserted) noexcept {
    interrupts_.irq = asserted;
}

void Arm7Tdmi::set_fiq_line(bool asserted) noexcept {
    interrupts_.fiq = asserted;
}

void Arm7Tdmi::set_wake_pending(bool pending) noexcept {
    context_.wake_pending = pending;
}

void Arm7Tdmi::set_halted(bool halted) noexcept {
    if (halted && (interrupts_.irq || interrupts_.fiq || context_.wake_pending)) {
        context_.halted = false;
        return;
    }
    context_.halted = halted;
}

RegisterBank& Arm7Tdmi::registers() noexcept {
    return registers_;
}

const RegisterBank& Arm7Tdmi::registers() const noexcept {
    return registers_;
}

ArmCoreContext& Arm7Tdmi::context() noexcept {
    return context_;
}

const ArmCoreContext& Arm7Tdmi::context() const noexcept {
    return context_;
}

bool Arm7Tdmi::is_halted() const noexcept {
    return context_.halted;
}

TickCount Arm7Tdmi::total_cycles() const noexcept {
    return total_cycles_;
}

}  // namespace elmasds::cpu
