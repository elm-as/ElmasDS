#pragma once

#include "cpu/arm_architecture/bus_interface.hpp"
#include "cpu/arm_architecture/coprocessor_interface.hpp"
#include "cpu/arm_architecture/register_bank.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Shared execution state passed to ARM/Thumb instruction executors.
 *
 * Responsibility:
 *   Provides controlled access to RegisterBank, CpuBusInterface, optional CoprocessorInterface,
 *   ARM9 HIVECS setting, halted state, and architecture-accurate PC pipeline offsets/writes.
 */
struct ArmCoreContext;

/**
 * @brief Non-owning callback invoked on `SWI` before raising a hardware exception vector.
 *
 * Returns `true` when the software interrupt was handled in HLE mode (in which case the caller
 * advances `PC` past the `SWI` instruction without jumping to the exception vector).
 */
struct SwiHandler {
    void* context = nullptr;
    bool (*invoke)(void* context, ArmCoreContext& core, u8 swi_number) = nullptr;

    [[nodiscard]] bool try_handle(ArmCoreContext& core, u8 swi_number) const {
        return (invoke != nullptr) && invoke(context, core, swi_number);
    }
};

struct ArmCoreContext {
    RegisterBank& registers;
    CpuBusInterface& bus;
    bool high_vectors = false;
    bool halted = false;
    CoprocessorInterface* coprocessor = nullptr;
    bool wake_pending = false;
    SwiHandler swi_handler{};

    /**
     * @brief Reads register `reg`, returning `PC + 8` in ARM state when `reg == R15`.
     */
    [[nodiscard]] u32 read_arm_operand(RegisterIndex reg, u32 instruction_pc) const noexcept {
        if (reg == RegisterIndex::Pc) {
            return instruction_pc + 8U;
        }
        return registers.read(reg);
    }

    /**
     * @brief Writes PC via standard branch/ALU target (aligns PC; never toggles Thumb).
     */
    void branch_arm(Address target) noexcept {
        registers.write(RegisterIndex::Pc, target & ~3U);
    }

    /**
     * @brief Writes PC via BX/BLX semantics (bit 0 selects Thumb state).
     */
    void branch_exchange(Address target) noexcept {
        const bool thumb = (target & 1U) != 0U;
        registers.set_cpsr(registers.cpsr().with_thumb(thumb));
        registers.write(RegisterIndex::Pc, target & (thumb ? ~1U : ~3U));
    }

    /**
     * @brief Writes PC from LDR/LDM/POP (ARMv5TE switches Thumb on bit 0; ARMv4T keeps state).
     */
    void load_pc(Address target) noexcept {
        if (registers.architecture() == ArmArchitecture::ArmV5Te) {
            branch_exchange(target);
            return;
        }
        const bool thumb = registers.cpsr().thumb();
        registers.write(RegisterIndex::Pc, target & (thumb ? ~1U : ~3U));
    }
};

}  // namespace elmasds::cpu
