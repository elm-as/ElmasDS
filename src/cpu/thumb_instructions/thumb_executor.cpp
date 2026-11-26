#include "cpu/thumb_instructions/thumb_executor.hpp"

#include "cpu/arm_architecture/exception_vector.hpp"
#include "cpu/thumb_instructions/thumb_alu_and_branch.hpp"
#include "cpu/thumb_instructions/thumb_decoder.hpp"
#include "cpu/thumb_instructions/thumb_memory_transfer.hpp"

#include <array>

namespace elmasds::cpu {
namespace {

constexpr u8 kThumbLutExact = 0xFFU;

[[nodiscard]] constexpr std::array<u8, 1024U> build_thumb_lut(ArmArchitecture arch) noexcept {
    std::array<u8, 1024U> lut{};
    for (u32 idx = 0U; idx < 1024U; ++idx) {
        if (arch == ArmArchitecture::ArmV5Te && (idx >> 5U) == 0x1DU) {
            lut[idx] = kThumbLutExact;
            continue;
        }
        lut[idx] = static_cast<u8>(decode_thumb_instruction(static_cast<u16>(idx << 6U), arch));
    }
    return lut;
}

const auto kThumbV4Lut = build_thumb_lut(ArmArchitecture::ArmV4T);
const auto kThumbV5Lut = build_thumb_lut(ArmArchitecture::ArmV5Te);

struct ThumbDispatchRequest {
    ThumbInstructionKind kind = ThumbInstructionKind::Undefined;
    u16 opcode = 0;
    Address pc = 0;
};

[[nodiscard]] ArmStepResult dispatch_thumb(ArmCoreContext& context,
                                           const ThumbDispatchRequest& request) {
    switch (request.kind) {
    case ThumbInstructionKind::MoveShiftedRegister:
    case ThumbInstructionKind::AddSubtract:
        return execute_thumb_shift_or_addsub(context, request.opcode);
    case ThumbInstructionKind::ImmediateAlu:
    case ThumbInstructionKind::AluRegister:
        return execute_thumb_alu(context, request.opcode);
    case ThumbInstructionKind::HiRegOrBranchExchange:
        return execute_thumb_hi_reg_or_bx(context, request.opcode, request.pc);
    case ThumbInstructionKind::PcRelativeLoad:
    case ThumbInstructionKind::RegisterOffsetTransfer:
    case ThumbInstructionKind::HalfwordSignTransfer:
    case ThumbInstructionKind::ImmediateTransfer:
    case ThumbInstructionKind::HalfwordTransfer:
    case ThumbInstructionKind::SpRelativeTransfer:
        return execute_thumb_single_transfer(context, request.opcode, request.pc);
    case ThumbInstructionKind::LoadAddress:
    case ThumbInstructionKind::AdjustStackPointer:
    case ThumbInstructionKind::PushPopRegisters:
    case ThumbInstructionKind::MultipleTransfer:
        return execute_thumb_stack_and_block(context, request.opcode, request.pc);
    case ThumbInstructionKind::ConditionalBranch:
    case ThumbInstructionKind::SoftwareInterrupt:
    case ThumbInstructionKind::UnconditionalBranch:
    case ThumbInstructionKind::LongBranchPrefix:
    case ThumbInstructionKind::LongBranchSuffix:
        return execute_thumb_branch_or_swi(context, request.opcode, request.pc);
    case ThumbInstructionKind::Breakpoint:
    case ThumbInstructionKind::Undefined:
        raise_exception(context, CpuException::Undefined, request.pc);
        return ArmStepResult{3U, true};
    }
    return ArmStepResult{1U, false};
}

}  // namespace

ArmStepResult step_thumb_instruction(ArmCoreContext& context) {
    const Address pc = context.registers.read(RegisterIndex::Pc) & ~1U;
    const u16 opcode = context.bus.fetch16(pc);
    const ArmArchitecture arch = context.registers.architecture();
    const u8 lut_val = (arch == ArmArchitecture::ArmV5Te) ? kThumbV5Lut[opcode >> 6U]
                                                          : kThumbV4Lut[opcode >> 6U];
    const ThumbInstructionKind kind = (lut_val != kThumbLutExact)
                                          ? static_cast<ThumbInstructionKind>(lut_val)
                                          : decode_thumb_instruction(opcode, arch);
    const ArmStepResult result = dispatch_thumb(context, ThumbDispatchRequest{kind, opcode, pc});
    if (!result.pc_written) {
        context.registers.write(RegisterIndex::Pc, pc + 2U);
    }
    return result;
}

}  // namespace elmasds::cpu
