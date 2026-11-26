#include "cpu/arm_instructions/arm_executor.hpp"

#include "cpu/arm_architecture/condition_codes.hpp"
#include "cpu/arm_architecture/coprocessor_interface.hpp"
#include "cpu/arm_architecture/cpu_mode.hpp"
#include "cpu/arm_architecture/exception_vector.hpp"
#include "cpu/arm_instructions/arm_data_processing.hpp"
#include "cpu/arm_instructions/arm_decoder.hpp"
#include "cpu/arm_instructions/arm_memory_transfer.hpp"
#include "primitives/bit_operations.hpp"

#include <array>
#include <optional>

namespace elmasds::cpu {
namespace {

constexpr u8 kArmLutExact = 0xFFU;

[[nodiscard]] constexpr std::array<u8, 4096U> build_arm_lut(ArmArchitecture arch) noexcept {
    std::array<u8, 4096U> lut{};
    for (u32 key = 0U; key < 4096U; ++key) {
        const u32 b27_20 = (key >> 4U) & 0xFFU;
        if ((b27_20 & 0xD9U) == 0x10U || (b27_20 & 0xFBU) == 0x32U) {
            lut[key] = kArmLutExact;
            continue;
        }
        const u32 synthetic_opcode = (b27_20 << 20U) | ((key & 0x0FU) << 4U);
        lut[key] = static_cast<u8>(decode_arm_instruction(synthetic_opcode, arch));
    }
    return lut;
}

const auto kArmV4Lut = build_arm_lut(ArmArchitecture::ArmV4T);
const auto kArmV5Lut = build_arm_lut(ArmArchitecture::ArmV5Te);

struct ArmDispatchRequest {
    ArmInstructionKind kind = ArmInstructionKind::Undefined;
    u32 opcode = 0;
    Address pc = 0;
};

[[nodiscard]] ArmStepResult execute_arm_coprocessor(ArmCoreContext& context,
                                                    u32 opcode,
                                                    Address pc) {
    if ((opcode & 0x0F000010U) != 0x0E000010U || context.coprocessor == nullptr) {
        raise_exception(context, CpuException::Undefined, pc);
        return ArmStepResult{3U, true};
    }

    const CoprocessorTransfer transfer{
        static_cast<u8>(bits::extract_bits(opcode, 8U, 4U)),
        static_cast<u8>(bits::extract_bits(opcode, 21U, 3U)),
        static_cast<u8>(bits::extract_bits(opcode, 16U, 4U)),
        static_cast<u8>(bits::extract_bits(opcode, 0U, 4U)),
        static_cast<u8>(bits::extract_bits(opcode, 5U, 3U)),
        is_privileged_mode(context.registers.current_mode()),
    };
    const auto rd = static_cast<RegisterIndex>(bits::extract_bits(opcode, 12U, 4U));

    if (!bits::test_bit(opcode, 20U)) {
        const u32 value = context.read_arm_operand(rd, pc);
        if (!context.coprocessor->write_register(transfer, value)) {
            raise_exception(context, CpuException::Undefined, pc);
            return ArmStepResult{3U, true};
        }
        return ArmStepResult{1U, false};
    }

    const std::optional<u32> value = context.coprocessor->read_register(transfer);
    if (!value.has_value()) {
        raise_exception(context, CpuException::Undefined, pc);
        return ArmStepResult{3U, true};
    }

    if (rd == RegisterIndex::Pc) {
        const StatusRegister updated = context.registers.cpsr().with_nzcv(
            bits::test_bit(*value, 31U),
            bits::test_bit(*value, 30U),
            bits::test_bit(*value, 29U),
            bits::test_bit(*value, 28U));
        context.registers.set_cpsr(updated);
        return ArmStepResult{1U, false};
    }

    context.registers.write(rd, *value);
    return ArmStepResult{1U, false};
}

[[nodiscard]] ArmStepResult dispatch_decoded(ArmCoreContext& context,
                                             const ArmDispatchRequest& request) {
    const RegisterIndex rd = static_cast<RegisterIndex>(bits::extract_bits(request.opcode, 12U, 4U));
    switch (request.kind) {
    case ArmInstructionKind::DataProcessing: {
        const u32 alu_op = bits::extract_bits(request.opcode, 21U, 4U);
        const bool writes_rd = (alu_op < 0x8U) || (alu_op > 0xBU);
        return ArmStepResult{execute_arm_data_processing(context, request.opcode, request.pc),
                             writes_rd && rd == RegisterIndex::Pc};
    }
    case ArmInstructionKind::Mrs:
        return ArmStepResult{execute_arm_mrs(context, request.opcode), false};
    case ArmInstructionKind::MsrRegister:
        return ArmStepResult{execute_arm_msr(context, request.opcode, false), false};
    case ArmInstructionKind::MsrImmediate:
        return ArmStepResult{execute_arm_msr(context, request.opcode, true), false};
    case ArmInstructionKind::Multiply:
        return ArmStepResult{execute_arm_multiply(context, request.opcode), false};
    case ArmInstructionKind::MultiplyLong:
        return ArmStepResult{execute_arm_multiply_long(context, request.opcode), false};
    case ArmInstructionKind::ArmV5DspMultiply:
        return ArmStepResult{execute_arm_dsp_multiply(context, request.opcode), false};
    case ArmInstructionKind::ArmV5Saturating:
        return ArmStepResult{execute_arm_saturating(context, request.opcode), false};
    case ArmInstructionKind::CountLeadingZeros:
        return ArmStepResult{execute_arm_clz(context, request.opcode), false};
    case ArmInstructionKind::SingleDataSwap:
        return execute_arm_swap(context, request.opcode);
    case ArmInstructionKind::BranchAndExchange:
    case ArmInstructionKind::BranchLinkExchangeReg:
        return execute_arm_branch_exchange_reg(context, request.opcode, request.pc);
    case ArmInstructionKind::BranchLinkExchangeImm:
        return execute_arm_blx_immediate(context, request.opcode, request.pc);
    case ArmInstructionKind::HalfwordOrDoubleTransfer:
        return execute_arm_halfword_or_double(context, request.opcode, request.pc);
    case ArmInstructionKind::SingleDataTransfer:
        return execute_arm_single_transfer(context, request.opcode, request.pc);
    case ArmInstructionKind::BlockDataTransfer:
        return execute_arm_block_transfer(context, request.opcode, request.pc);
    case ArmInstructionKind::Branch:
        return execute_arm_branch(context, request.opcode, request.pc);
    case ArmInstructionKind::SoftwareInterrupt:
        return execute_arm_swi(context, request.opcode, request.pc);
    case ArmInstructionKind::PreloadData:
        return ArmStepResult{1U, false};
    case ArmInstructionKind::CoprocessorTransfer:
        return execute_arm_coprocessor(context, request.opcode, request.pc);
    case ArmInstructionKind::Breakpoint:
    case ArmInstructionKind::Undefined:
        raise_exception(context, CpuException::Undefined, request.pc);
        return ArmStepResult{3U, true};
    }
    return ArmStepResult{1U, false};
}

}  // namespace

ArmStepResult step_arm_instruction(ArmCoreContext& context) {
    const Address pc = context.registers.read(RegisterIndex::Pc) & ~3U;
    const u32 opcode = context.bus.fetch32(pc);
    const u32 cond_raw = opcode >> 28U;
    const ArmArchitecture arch = context.registers.architecture();

    if (cond_raw != 0xEU) {
        if (cond_raw == 0xFU) {
            if (arch != ArmArchitecture::ArmV5Te) {
                context.registers.write(RegisterIndex::Pc, pc + 4U);
                return ArmStepResult{1U, false};
            }
            const ArmInstructionKind kind = detail::decode_unconditional_v5(opcode);
            const ArmStepResult res = dispatch_decoded(context, ArmDispatchRequest{kind, opcode, pc});
            if (!res.pc_written) { context.registers.write(RegisterIndex::Pc, pc + 4U); }
            return res;
        }
        if (!evaluate_condition(static_cast<ConditionCode>(cond_raw), context.registers.cpsr())) {
            context.registers.write(RegisterIndex::Pc, pc + 4U);
            return ArmStepResult{1U, false};
        }
    }

    const u32 key = ((opcode >> 16U) & 0xFF0U) | ((opcode >> 4U) & 0x00FU);
    const u8 lut_val = (arch == ArmArchitecture::ArmV5Te) ? kArmV5Lut[key] : kArmV4Lut[key];
    const ArmInstructionKind kind = (lut_val != kArmLutExact)
                                        ? static_cast<ArmInstructionKind>(lut_val)
                                        : decode_arm_instruction(opcode, arch);
    const ArmStepResult result = dispatch_decoded(context, ArmDispatchRequest{kind, opcode, pc});
    if (!result.pc_written) {
        context.registers.write(RegisterIndex::Pc, pc + 4U);
    }
    return result;
}

}  // namespace elmasds::cpu
