#include "e2e/arm_program_emitter.hpp"

#include "primitives/bit_operations.hpp"

#include <cstdlib>
#include <optional>
#include <stdexcept>

namespace elmasds::e2e {
namespace {

constexpr u8 kAluOrr = 0xCU;
constexpr u8 kAluMov = 0xDU;
constexpr u8 kAluBic = 0xEU;
constexpr u8 kAluAdd = 0x4U;
constexpr u8 kAluSub = 0x2U;
constexpr u8 kAluCmp = 0xAU;

struct EncodedImm8 {
    u8 rot4 = 0U;
    u8 imm8 = 0U;
};

struct SingleTransferSpec {
    MemOffsetOp op{};
    bool is_load = true;
    bool is_byte = false;
};

struct BranchSpec {
    Address instr_pc = 0U;
    Address target = 0U;
    bool link = false;
    ArmCond cond = ArmCond::Al;
};

[[nodiscard]] constexpr u32 cond_bits(ArmCond cond) noexcept {
    return static_cast<u32>(cond) << 28U;
}

[[nodiscard]] constexpr u32 reg_bits(ArmReg reg, u32 shift) noexcept {
    return static_cast<u32>(reg) << shift;
}

[[nodiscard]] std::optional<EncodedImm8> try_encode_imm8(u32 value) noexcept {
    for (u8 rot4 = 0U; rot4 < 16U; ++rot4) {
        const u32 unrotated = bits::rotate_right_32(value, (16U - rot4) * 2U);
        if (unrotated <= 0xFFU) {
            return EncodedImm8{rot4, static_cast<u8>(unrotated)};
        }
    }
    return std::nullopt;
}

[[nodiscard]] u32 encode_dp_imm(u8 alu_op, const RegImmOp& op, EncodedImm8 encoded) noexcept {
    const u32 s_bit = op.set_flags ? (1U << 20U) : 0U;
    return cond_bits(op.cond) | (1U << 25U) | (static_cast<u32>(alu_op) << 21U) | s_bit |
           reg_bits(op.rn, 16U) | reg_bits(op.rd, 12U) | (static_cast<u32>(encoded.rot4) << 8U) |
           static_cast<u32>(encoded.imm8);
}

[[nodiscard]] u32 encode_single_transfer(const SingleTransferSpec& spec) {
    if (spec.op.offset < -4095 || spec.op.offset > 4095) {
        throw std::out_of_range("ARM single data transfer offset out of range [-4095, 4095]");
    }
    const u32 u_bit = (spec.op.offset >= 0) ? (1U << 23U) : 0U;
    const u32 b_bit = spec.is_byte ? (1U << 22U) : 0U;
    const u32 l_bit = spec.is_load ? (1U << 20U) : 0U;
    const u32 abs_off = static_cast<u32>(std::abs(spec.op.offset));
    return cond_bits(spec.op.cond) | (1U << 26U) | (1U << 24U) | u_bit | b_bit | l_bit |
           reg_bits(spec.op.rn, 16U) | reg_bits(spec.op.rd, 12U) | (abs_off & 0xFFFU);
}

[[nodiscard]] u32 encode_halfword_transfer(const MemOffsetOp& op, bool is_load) {
    if (op.offset < -255 || op.offset > 255) {
        throw std::out_of_range("ARM halfword transfer offset out of range [-255, 255]");
    }
    const u32 u_bit = (op.offset >= 0) ? (1U << 23U) : 0U;
    const u32 l_bit = is_load ? (1U << 20U) : 0U;
    const u32 abs_off = static_cast<u32>(std::abs(op.offset));
    const u32 imm_hi = (abs_off >> 4U) & 0xFU;
    const u32 imm_lo = abs_off & 0xFU;
    return cond_bits(op.cond) | (1U << 24U) | u_bit | (1U << 22U) | l_bit | reg_bits(op.rn, 16U) |
           reg_bits(op.rd, 12U) | (imm_hi << 8U) | 0xB0U | imm_lo;
}

[[nodiscard]] u32 encode_branch(const BranchSpec& spec) {
    const i64 delta = static_cast<i64>(spec.target) - static_cast<i64>(spec.instr_pc + 8U);
    if ((delta & 3LL) != 0LL) {
        throw std::invalid_argument("ARM branch target must be 4-byte aligned");
    }
    const i64 word_delta = delta >> 2;
    if (word_delta < -0x800000LL || word_delta > 0x7FFFFFLL) {
        throw std::out_of_range("ARM branch target out of +-32 MiB range");
    }
    const u32 l_bit = spec.link ? (1U << 24U) : 0U;
    return cond_bits(spec.cond) | (0x5U << 25U) | l_bit |
           (static_cast<u32>(word_delta) & 0x00FFFFFFU);
}

void emit_alu_imm_chunks(ArmProgramEmitter& emitter, u8 alu_op, const RegImmOp& op) {
    if (const auto single = try_encode_imm8(op.imm); single.has_value()) {
        emitter.emit_word(encode_dp_imm(alu_op, op, *single));
        return;
    }
    if (op.set_flags && (alu_op == kAluAdd || alu_op == kAluSub)) {
        throw std::invalid_argument("Multi-chunk ADD/SUB cannot accurately set CPSR flags");
    }
    if (alu_op == kAluOrr && op.rd != op.rn) {
        emitter.emit_word(cond_bits(op.cond) | (static_cast<u32>(kAluMov) << 21U) |
                          reg_bits(op.rd, 12U) | static_cast<u32>(op.rn));
    }
    bool first = (alu_op != kAluOrr || op.rd == op.rn);
    for (int byte_idx = 0; byte_idx < 4; ++byte_idx) {
        const u32 shift = static_cast<u32>(byte_idx * 8);
        const u32 chunk = op.imm & (0xFFU << shift);
        if (chunk == 0U) {
            continue;
        }
        const bool is_last = (byte_idx == 3) || ((op.imm >> (shift + 8U)) == 0U);
        const RegImmOp step{op.rd, first ? op.rn : op.rd, chunk, op.set_flags && is_last, op.cond};
        emitter.emit_word(encode_dp_imm(alu_op, step, *try_encode_imm8(chunk)));
        first = false;
    }
}

}  // namespace

ArmProgramEmitter::ArmProgramEmitter(Address base_address) noexcept
    : base_address_(base_address) {}

void ArmProgramEmitter::clear() noexcept {
    words_.clear();
    pending_literals_.clear();
}

std::vector<u8> ArmProgramEmitter::to_bytes() const {
    std::vector<u8> bytes(words_.size() * 4U);
    for (std::size_t i = 0; i < words_.size(); ++i) {
        const u32 w = words_[i];
        bytes[(i * 4U) + 0U] = static_cast<u8>(w & 0xFFU);
        bytes[(i * 4U) + 1U] = static_cast<u8>((w >> 8U) & 0xFFU);
        bytes[(i * 4U) + 2U] = static_cast<u8>((w >> 16U) & 0xFFU);
        bytes[(i * 4U) + 3U] = static_cast<u8>((w >> 24U) & 0xFFU);
    }
    return bytes;
}

void ArmProgramEmitter::emit_word(u32 word) {
    words_.push_back(word);
}

void ArmProgramEmitter::emit_nop() {
    words_.push_back(0xE1A00000U);
}

void ArmProgramEmitter::emit_mov_imm(ArmReg rd, u32 imm32, ArmCond cond) {
    if (const auto single = try_encode_imm8(imm32); single.has_value()) {
        emit_word(encode_dp_imm(kAluMov, RegImmOp{rd, ArmReg::R0, imm32, false, cond}, *single));
        return;
    }
    bool first = true;
    for (int byte_idx = 0; byte_idx < 4; ++byte_idx) {
        const u32 chunk = imm32 & (0xFFU << static_cast<u32>(byte_idx * 8));
        if (chunk == 0U) {
            continue;
        }
        const RegImmOp step{rd, first ? ArmReg::R0 : rd, chunk, false, cond};
        emit_word(encode_dp_imm(first ? kAluMov : kAluOrr, step, *try_encode_imm8(chunk)));
        first = false;
    }
}

void ArmProgramEmitter::emit_add_imm(const RegImmOp& op) {
    emit_alu_imm_chunks(*this, kAluAdd, op);
}

void ArmProgramEmitter::emit_sub_imm(const RegImmOp& op) {
    emit_alu_imm_chunks(*this, kAluSub, op);
}

void ArmProgramEmitter::emit_orr_imm(const RegImmOp& op) {
    emit_alu_imm_chunks(*this, kAluOrr, op);
}

void ArmProgramEmitter::emit_cmp_imm(ArmReg rn, u32 imm, ArmCond cond) {
    const auto encoded = try_encode_imm8(imm);
    if (!encoded.has_value()) {
        throw std::invalid_argument("CMP immediate cannot be encoded as rotated 8-bit immediate");
    }
    emit_word(encode_dp_imm(kAluCmp, RegImmOp{ArmReg::R0, rn, imm, true, cond}, *encoded));
}

void ArmProgramEmitter::emit_ldr_literal(ArmReg rd, u32 literal_value, ArmCond cond) {
    pending_literals_.push_back(LiteralPatch{words_.size(), literal_value, rd, cond});
    emit_word(0U);
}

void ArmProgramEmitter::finalize_literal_pool() {
    for (const LiteralPatch& patch : pending_literals_) {
        const Address literal_pc = current_pc();
        words_.push_back(patch.value);
        const Address instr_pc = base_address_ + static_cast<Address>(patch.instr_index * 4U);
        const i32 offset = static_cast<i32>(literal_pc) - static_cast<i32>(instr_pc + 8U);
        const MemOffsetOp op{patch.rd, ArmReg::Pc, offset, patch.cond};
        words_[patch.instr_index] = encode_single_transfer(SingleTransferSpec{op, true, false});
    }
    pending_literals_.clear();
}

void ArmProgramEmitter::emit_ldr32(const MemOffsetOp& op) {
    emit_word(encode_single_transfer(SingleTransferSpec{op, true, false}));
}

void ArmProgramEmitter::emit_str32(const MemOffsetOp& op) {
    emit_word(encode_single_transfer(SingleTransferSpec{op, false, false}));
}

void ArmProgramEmitter::emit_ldr16(const MemOffsetOp& op) {
    emit_word(encode_halfword_transfer(op, true));
}

void ArmProgramEmitter::emit_str16(const MemOffsetOp& op) {
    emit_word(encode_halfword_transfer(op, false));
}

void ArmProgramEmitter::emit_ldr8(const MemOffsetOp& op) {
    emit_word(encode_single_transfer(SingleTransferSpec{op, true, true}));
}

void ArmProgramEmitter::emit_str8(const MemOffsetOp& op) {
    emit_word(encode_single_transfer(SingleTransferSpec{op, false, true}));
}

void ArmProgramEmitter::emit_b(Address target, ArmCond cond) {
    emit_word(encode_branch(BranchSpec{current_pc(), target, false, cond}));
}

void ArmProgramEmitter::emit_bl(Address target, ArmCond cond) {
    emit_word(encode_branch(BranchSpec{current_pc(), target, true, cond}));
}

void ArmProgramEmitter::emit_bx(ArmReg rm, ArmCond cond) {
    emit_word(cond_bits(cond) | 0x012FFF10U | static_cast<u32>(rm));
}

void ArmProgramEmitter::emit_branch_self(ArmCond cond) {
    emit_word(cond_bits(cond) | 0x0AFFFFFEU);
}

void ArmProgramEmitter::emit_mrs_cpsr(ArmReg rd, ArmCond cond) {
    emit_word(cond_bits(cond) | 0x010F0000U | reg_bits(rd, 12U));
}

void ArmProgramEmitter::emit_msr_cpsr_c_imm(u8 imm8, ArmCond cond) {
    emit_word(cond_bits(cond) | 0x0321F000U | static_cast<u32>(imm8));
}

void ArmProgramEmitter::emit_msr_cpsr_c_reg(ArmReg rm, ArmCond cond) {
    emit_word(cond_bits(cond) | 0x0121F000U | static_cast<u32>(rm));
}

void ArmProgramEmitter::emit_enable_irq(ArmReg scratch) {
    emit_mrs_cpsr(scratch);
    const RegImmOp bic_op{scratch, scratch, 0x80U, false, ArmCond::Al};
    emit_word(encode_dp_imm(kAluBic, bic_op, EncodedImm8{0U, 0x80U}));
    emit_msr_cpsr_c_reg(scratch);
}

void ArmProgramEmitter::emit_set_mode_sp(u8 cpu_mode, Address sp, ArmReg scratch) {
    const u8 mode_bits = static_cast<u8>(cpu_mode & 0x1FU);
    const ArmReg max_scratch = (mode_bits == 0x11U) ? ArmReg::R7 : ArmReg::R12;
    if (static_cast<u8>(scratch) > static_cast<u8>(max_scratch)) {
        throw std::invalid_argument("Scratch register is banked or invalid for target mode");
    }
    emit_mrs_cpsr(scratch);
    emit_msr_cpsr_c_imm(static_cast<u8>(mode_bits | 0xC0U));
    emit_mov_imm(ArmReg::Sp, sp);
    emit_msr_cpsr_c_reg(scratch);
}

void ArmProgramEmitter::emit_mcr_wfi(ArmReg rd, ArmCond cond) {
    emit_word(cond_bits(cond) | 0x0E070F90U | reg_bits(rd, 12U));
}

void ArmProgramEmitter::emit_irq_return(ArmCond cond) {
    emit_word(cond_bits(cond) | 0x025EF004U);
}

void ArmProgramEmitter::emit_swi(u32 comment24, ArmCond cond) {
    emit_word(cond_bits(cond) | 0x0F000000U | (comment24 & 0x00FFFFFFU));
}

void ArmProgramEmitter::emit_irq_handler_prologue() {
    emit_word(0xE92D500FU);
}

void ArmProgramEmitter::emit_irq_handler_epilogue() {
    emit_word(0xE8BD500FU);
    emit_irq_return();
}

void ArmProgramEmitter::emit_irq_trampoline_install(Address vector_slot_addr,
                                                    Address handler_addr,
                                                    ScratchRegs scratch) {
    emit_write32_abs(vector_slot_addr, handler_addr, scratch);
}

void ArmProgramEmitter::emit_write32_abs(Address addr, u32 value, ScratchRegs scratch) {
    emit_mov_imm(scratch.addr, addr);
    emit_mov_imm(scratch.value, value);
    emit_str32(MemOffsetOp{scratch.value, scratch.addr, 0});
}

void ArmProgramEmitter::emit_write16_abs(Address addr, u16 value, ScratchRegs scratch) {
    emit_mov_imm(scratch.addr, addr);
    emit_mov_imm(scratch.value, value);
    emit_str16(MemOffsetOp{scratch.value, scratch.addr, 0});
}

void ArmProgramEmitter::emit_write8_abs(Address addr, u8 value, ScratchRegs scratch) {
    emit_mov_imm(scratch.addr, addr);
    emit_mov_imm(scratch.value, value);
    emit_str8(MemOffsetOp{scratch.value, scratch.addr, 0});
}

void ArmProgramEmitter::emit_read32_abs(ArmReg rd, Address addr) {
    emit_mov_imm(rd, addr);
    emit_ldr32(MemOffsetOp{rd, rd, 0});
}

}  // namespace elmasds::e2e
