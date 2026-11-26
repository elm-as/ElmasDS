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
    active_regs_.fill(0U);
    active_regs_[15U] = reset_pc;
    r0_to_r7_.fill(0U);
    r8_to_r12_user_.fill(0U);
    r8_to_r12_fiq_.fill(0U);
    sp_banked_.fill(0U);
    lr_banked_.fill(0U);
    cpsr_ = StatusRegister{};
    active_bank_ = mode_to_bank(cpsr_.mode());
    spsr_banked_.fill(StatusRegister{});
}

u32 RegisterBank::read_user(RegisterIndex reg) const noexcept {
    return read_for_bank(reg, ModeBank::UserSystem);
}

void RegisterBank::write_user(RegisterIndex reg, u32 value) noexcept {
    write_for_bank(reg, ModeBank::UserSystem, value);
}

void RegisterBank::sync_bank(ModeBank next_bank) noexcept {
    if (next_bank == active_bank_) {
        return;
    }
    const std::size_t cur_slot = static_cast<std::size_t>(active_bank_);
    sp_banked_[cur_slot] = active_regs_[13U];
    lr_banked_[cur_slot] = active_regs_[14U];
    const bool cur_fiq = (active_bank_ == ModeBank::Fiq);
    const bool next_fiq = (next_bank == ModeBank::Fiq);
    if (cur_fiq != next_fiq) {
        auto& save_dst = cur_fiq ? r8_to_r12_fiq_ : r8_to_r12_user_;
        const auto& load_src = next_fiq ? r8_to_r12_fiq_ : r8_to_r12_user_;
        for (std::size_t i = 0U; i < 5U; ++i) {
            save_dst[i] = active_regs_[8U + i];
            active_regs_[8U + i] = load_src[i];
        }
    }
    const std::size_t next_slot = static_cast<std::size_t>(next_bank);
    active_regs_[13U] = sp_banked_[next_slot];
    active_regs_[14U] = lr_banked_[next_slot];
    active_bank_ = next_bank;
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

u32 RegisterBank::read_for_bank(RegisterIndex reg, ModeBank bank) const noexcept {
    const u8 index = static_cast<u8>(reg);
    if (index <= 7U || index == 15U || bank == active_bank_) {
        return active_regs_[index];
    }
    if (index <= 12U) {
        const bool same_fiq = (bank == ModeBank::Fiq) == (active_bank_ == ModeBank::Fiq);
        if (same_fiq) {
            return active_regs_[index];
        }
        const std::size_t slot = static_cast<std::size_t>(index - 8U);
        return (bank == ModeBank::Fiq) ? r8_to_r12_fiq_[slot] : r8_to_r12_user_[slot];
    }
    if (reg == RegisterIndex::Sp) {
        return sp_banked_[static_cast<std::size_t>(bank)];
    }
    return lr_banked_[static_cast<std::size_t>(bank)];
}

void RegisterBank::write_for_bank(RegisterIndex reg, ModeBank bank, u32 value) noexcept {
    const u8 index = static_cast<u8>(reg);
    if (index <= 7U || index == 15U || bank == active_bank_) {
        active_regs_[index] = value;
        return;
    }
    if (index <= 12U) {
        const bool same_fiq = (bank == ModeBank::Fiq) == (active_bank_ == ModeBank::Fiq);
        if (same_fiq) {
            active_regs_[index] = value;
            return;
        }
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
    lr_banked_[static_cast<std::size_t>(bank)] = value;
}

}  // namespace elmasds::cpu
