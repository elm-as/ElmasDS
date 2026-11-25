#pragma once

#include "primitives/fixed_width_types.hpp"

#include <cstddef>
#include <vector>

namespace elmasds::e2e {

enum class ArmReg : u8 {
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

enum class ArmCond : u8 {
    Eq = 0,
    Ne = 1,
    Cs = 2,
    Cc = 3,
    Mi = 4,
    Pl = 5,
    Vs = 6,
    Vc = 7,
    Hi = 8,
    Ls = 9,
    Ge = 10,
    Lt = 11,
    Gt = 12,
    Le = 13,
    Al = 14,
};

struct RegImmOp {
    ArmReg rd = ArmReg::R0;
    ArmReg rn = ArmReg::R0;
    u32 imm = 0U;
    bool set_flags = false;
    ArmCond cond = ArmCond::Al;
};

struct MemOffsetOp {
    ArmReg rd = ArmReg::R0;
    ArmReg rn = ArmReg::R0;
    i32 offset = 0;
    ArmCond cond = ArmCond::Al;
};

struct ScratchRegs {
    ArmReg addr = ArmReg::R0;
    ArmReg value = ArmReg::R1;
};

struct LiteralPatch {
    std::size_t instr_index = 0U;
    u32 value = 0U;
    ArmReg rd = ArmReg::R0;
    ArmCond cond = ArmCond::Al;
};

/**
 * @brief Little-endian ARM32 machine-code emitter for synthetic E2E guest payloads.
 */
class ArmProgramEmitter {
public:
    explicit ArmProgramEmitter(Address base_address = 0U) noexcept;

    void clear() noexcept;

    [[nodiscard]] Address base_address() const noexcept {
        return base_address_;
    }
    [[nodiscard]] Address current_pc() const noexcept {
        return base_address_ + static_cast<Address>(words_.size() * 4U);
    }
    [[nodiscard]] std::size_t word_count() const noexcept {
        return words_.size();
    }
    [[nodiscard]] const std::vector<u32>& words() const noexcept {
        return words_;
    }
    [[nodiscard]] std::vector<u8> to_bytes() const;

    void emit_word(u32 word);
    void emit_nop();

    void emit_mov_imm(ArmReg rd, u32 imm32, ArmCond cond = ArmCond::Al);
    void emit_add_imm(const RegImmOp& op);
    void emit_sub_imm(const RegImmOp& op);
    void emit_orr_imm(const RegImmOp& op);
    void emit_cmp_imm(ArmReg rn, u32 imm, ArmCond cond = ArmCond::Al);

    void emit_ldr_literal(ArmReg rd, u32 literal_value, ArmCond cond = ArmCond::Al);
    void finalize_literal_pool();

    void emit_ldr32(const MemOffsetOp& op);
    void emit_str32(const MemOffsetOp& op);
    void emit_ldr16(const MemOffsetOp& op);
    void emit_str16(const MemOffsetOp& op);
    void emit_ldr8(const MemOffsetOp& op);
    void emit_str8(const MemOffsetOp& op);

    void emit_b(Address target, ArmCond cond = ArmCond::Al);
    void emit_bl(Address target, ArmCond cond = ArmCond::Al);
    void emit_bx(ArmReg rm, ArmCond cond = ArmCond::Al);
    void emit_branch_self(ArmCond cond = ArmCond::Al);

    void emit_mrs_cpsr(ArmReg rd, ArmCond cond = ArmCond::Al);
    void emit_msr_cpsr_c_imm(u8 imm8, ArmCond cond = ArmCond::Al);
    void emit_msr_cpsr_c_reg(ArmReg rm, ArmCond cond = ArmCond::Al);
    void emit_enable_irq(ArmReg scratch = ArmReg::R0);
    void emit_set_mode_sp(u8 cpu_mode, Address sp, ArmReg scratch = ArmReg::R0);

    void emit_mcr_wfi(ArmReg rd = ArmReg::R0, ArmCond cond = ArmCond::Al);
    void emit_irq_return(ArmCond cond = ArmCond::Al);
    void emit_swi(u32 comment24, ArmCond cond = ArmCond::Al);

    void emit_irq_handler_prologue();
    void emit_irq_handler_epilogue();
    void emit_irq_trampoline_install(Address vector_slot_addr,
                                     Address handler_addr,
                                     ScratchRegs scratch = {});

    void emit_write32_abs(Address addr, u32 value, ScratchRegs scratch = {});
    void emit_write16_abs(Address addr, u16 value, ScratchRegs scratch = {});
    void emit_write8_abs(Address addr, u8 value, ScratchRegs scratch = {});
    void emit_read32_abs(ArmReg rd, Address addr);

private:
    Address base_address_ = 0U;
    std::vector<u32> words_;
    std::vector<LiteralPatch> pending_literals_;
};

}  // namespace elmasds::e2e
