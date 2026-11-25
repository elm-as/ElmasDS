#pragma once

#include "cpu/arm_architecture/cpu_mode.hpp"
#include "cpu/arm_architecture/status_register.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>

namespace elmasds::cpu {

/**
 * @brief Named general-purpose ARM registers R0-R15.
 */
enum class RegisterIndex : u8 {
    R0 = 0,
    R1 = 1,
    R2 = 2,
    R3 = 3,
    R4 = 4,
    R5 = 5,
    R6 = 6,
    R7 = 7,
    R8 = 8,
    R9 = 9,
    R10 = 10,
    R11 = 11,
    R12 = 12,
    Sp = 13,
    Lr = 14,
    Pc = 15,
};

/**
 * @brief Complete banked register file for ARM7TDMI and ARM946E-S cores.
 *
 * Responsibility:
 *   Stores unbanked R0-R7, FIQ-banked R8-R12, per-mode SP/LR (R13/R14), R15 (PC),
 *   CPSR, and per-exception-mode SPSR registers.
 * Dependencies:
 *   `cpu_mode.hpp`, `status_register.hpp`, `fixed_width_types.hpp`.
 * Hardware notes:
 *   - User and System modes share the same R0-R14 registers and have no SPSR.
 *   - Reading SPSR in User/System returns CPSR (safe hardware fallback); writing is a no-op.
 */
class RegisterBank {
public:
    static constexpr std::size_t kBankCount = 6;
    static constexpr std::size_t kSpsrCount = 5;

    explicit RegisterBank(ArmArchitecture architecture = ArmArchitecture::ArmV4T) noexcept;

    /**
     * @brief Resets all registers to Supervisor mode with IRQ and FIQ disabled.
     * @param reset_pc Initial PC value (0x00000000 or 0xFFFF0000 for ARM9 HIVECS).
     */
    void reset(Address reset_pc = 0U) noexcept;

    [[nodiscard]] u32 read(RegisterIndex reg) const noexcept;
    void write(RegisterIndex reg, u32 value) noexcept;

    /**
     * @brief Reads a register from the User/System bank regardless of current CPSR mode.
     */
    [[nodiscard]] u32 read_user(RegisterIndex reg) const noexcept;

    /**
     * @brief Writes a register in the User/System bank regardless of current CPSR mode.
     */
    void write_user(RegisterIndex reg, u32 value) noexcept;

    [[nodiscard]] StatusRegister cpsr() const noexcept;
    void set_cpsr(StatusRegister next_cpsr) noexcept;

    [[nodiscard]] StatusRegister spsr() const noexcept;
    void set_spsr(StatusRegister next_spsr) noexcept;

    [[nodiscard]] CpuMode current_mode() const noexcept;
    void switch_mode(CpuMode next_mode) noexcept;

    [[nodiscard]] ArmArchitecture architecture() const noexcept;

    [[nodiscard]] u32 read_bank_register(RegisterIndex reg, ModeBank bank) const noexcept {
        return read_for_bank(reg, bank);
    }
    void write_bank_register(RegisterIndex reg, ModeBank bank, u32 value) noexcept {
        write_for_bank(reg, bank, value);
    }
    [[nodiscard]] StatusRegister read_spsr_bank(std::size_t index) const noexcept {
        return spsr_banked_[index % kSpsrCount];
    }
    void write_spsr_bank(std::size_t index, StatusRegister value) noexcept {
        spsr_banked_[index % kSpsrCount] = value;
    }

private:
    [[nodiscard]] u32 read_for_bank(RegisterIndex reg, ModeBank bank) const noexcept;
    void write_for_bank(RegisterIndex reg, ModeBank bank, u32 value) noexcept;

    ArmArchitecture architecture_;
    std::array<u32, 8> r0_to_r7_{};
    std::array<u32, 5> r8_to_r12_user_{};
    std::array<u32, 5> r8_to_r12_fiq_{};
    std::array<u32, kBankCount> sp_banked_{};
    std::array<u32, kBankCount> lr_banked_{};
    u32 pc_ = 0;
    StatusRegister cpsr_{};
    std::array<StatusRegister, kSpsrCount> spsr_banked_{};
};

}  // namespace elmasds::cpu
