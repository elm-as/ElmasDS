#include "cpu/arm_architecture/register_bank.hpp"

#include <cassert>

namespace elmasds::cpu {
namespace {

[[nodiscard]] std::size_t bank_to_spsr_slot(ModeBank bank) noexcept {
    assert(bank != ModeBank::UserSystem);
    return static_cast<std::size_t>(bank) - 1U;
}

}  // namespace

RegisterBank::RegisterBank(ArmArchitecture architecture) noexcept : architecture_(architecture) {
    reset(0U);
}

void RegisterBank::reset(Address reset_pc) noexcept {
    r0_to_r7_.fill(0U);
    r8_to_r12_user_.fill(0U);
    r8_to_r12_fiq_.fill(0U);
    sp_banked_.fill(0U);
    lr_banked_.fill(0U);
    pc_ = reset_pc;
    cpsr_ = StatusRegister{};
    spsr_banked_.fill(StatusRegister{});
}

u32 RegisterBank::read(RegisterIndex reg) const noexcept {
    return read_for_bank(reg, mode_to_bank(current_mode()));
}

void RegisterBank::write(RegisterIndex reg, u32 value) noexcept {
    write_for_bank(reg, mode_to_bank(current_mode()), value);
}

u32 RegisterBank::read_user(RegisterIndex reg) const noexcept {
    return read_for_bank(reg, ModeBank::UserSystem);
}

void RegisterBank::write_user(RegisterIndex reg, u32 value) noexcept {
    write_for_bank(reg, ModeBank::UserSystem, value);
}

StatusRegister RegisterBank::cpsr() const noexcept {
    return cpsr_;
}

void RegisterBank::set_cpsr(StatusRegister next_cpsr) noexcept {
    cpsr_ = next_cpsr;
}

StatusRegister RegisterBank::spsr() const noexcept {
    const CpuMode mode = current_mode();
    if (!mode_has_spsr(mode)) {
        return cpsr_;
    }
    return spsr_banked_[bank_to_spsr_slot(mode_to_bank(mode))];
}

void RegisterBank::set_spsr(StatusRegister next_spsr) noexcept {
    const CpuMode mode = current_mode();
    if (!mode_has_spsr(mode)) {
        return;
    }
    spsr_banked_[bank_to_spsr_slot(mode_to_bank(mode))] = next_spsr;
}

CpuMode RegisterBank::current_mode() const noexcept {
    return cpsr_.mode();
}

void RegisterBank::switch_mode(CpuMode next_mode) noexcept {
    cpsr_ = cpsr_.with_mode(next_mode);
}

ArmArchitecture RegisterBank::architecture() const noexcept {
    return architecture_;
}

u32 RegisterBank::read_for_bank(RegisterIndex reg, ModeBank bank) const noexcept {
    const u8 index = static_cast<u8>(reg);
    if (index <= 7U) {
        return r0_to_r7_[index];
    }
    if (index <= 12U) {
        const std::size_t slot = static_cast<std::size_t>(index - 8U);
        return (bank == ModeBank::Fiq) ? r8_to_r12_fiq_[slot] : r8_to_r12_user_[slot];
    }
    if (reg == RegisterIndex::Sp) {
        return sp_banked_[static_cast<std::size_t>(bank)];
    }
    if (reg == RegisterIndex::Lr) {
        return lr_banked_[static_cast<std::size_t>(bank)];
    }
    return pc_;
}

void RegisterBank::write_for_bank(RegisterIndex reg, ModeBank bank, u32 value) noexcept {
    const u8 index = static_cast<u8>(reg);
    if (index <= 7U) {
        r0_to_r7_[index] = value;
        return;
    }
    if (index <= 12U) {
        const std::size_t slot = static_cast<std::size_t>(index - 8U);
        if (bank == ModeBank::Fiq) {
            r8_to_r12_fiq_[slot] = value;
        } else {
            r8_to_r12_user_[slot] = value;
        }
        return;
    }
    if (reg == RegisterIndex::Sp) {
        sp_banked_[static_cast<std::size_t>(bank)] = value;
        return;
    }
    if (reg == RegisterIndex::Lr) {
        lr_banked_[static_cast<std::size_t>(bank)] = value;
        return;
    }
    pc_ = value;
}

}  // namespace elmasds::cpu
