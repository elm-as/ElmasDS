#include "cpu/arm_instructions/alu_operations.hpp"
#include "cpu/arm_instructions/arm_executor.hpp"
#include "mock_memory_bus.hpp"

#include <gtest/gtest.h>

namespace elmasds::cpu {
namespace {

TEST(ArmAluTest, ComputesAddSubCarryAndOverflowCorrectly) {
    // 0x7FFFFFFF + 1 => 0x80000000, N=1, Z=0, C=0, V=1.
    const AluResult signed_overflow = evaluate_alu_operation(AluInputs{
        AluOpcode::Add,
        0x7FFFFFFFU,
        1U,
        false,
        false,
        false,
    });
    EXPECT_EQ(signed_overflow.value, 0x80000000U);
    EXPECT_TRUE(signed_overflow.negative);
    EXPECT_FALSE(signed_overflow.zero);
    EXPECT_FALSE(signed_overflow.carry);
    EXPECT_TRUE(signed_overflow.overflow);

    // 5 - 5 => 0, N=0, Z=1, C=1 (no borrow), V=0.
    const AluResult cmp_equal = evaluate_alu_operation(AluInputs{
        AluOpcode::Cmp,
        5U,
        5U,
        false,
        false,
        false,
    });
    EXPECT_FALSE(cmp_equal.write_rd);
    EXPECT_TRUE(cmp_equal.zero);
    EXPECT_TRUE(cmp_equal.carry);
    EXPECT_FALSE(cmp_equal.overflow);

    // 3 - 5 => -2, C=0 (borrow occurred).
    const AluResult sub_borrow = evaluate_alu_operation(AluInputs{
        AluOpcode::Sub,
        3U,
        5U,
        false,
        false,
        false,
    });
    EXPECT_EQ(sub_borrow.value, 0xFFFFFFFEU);
    EXPECT_TRUE(sub_borrow.negative);
    EXPECT_FALSE(sub_borrow.carry);
}

TEST(ArmDataProcessingTest, ExecutesMovAddSubAndUpdatesFlagsAndPc) {
    RegisterBank regs(ArmArchitecture::ArmV5Te);
    test::MockMemoryBus bus;
    ArmCoreContext ctx{regs, bus, false, false};
    regs.reset(0x02000000U);

    // 0x02000000: MOV R0, #0x10          (E3A00010)
    // 0x02000004: ADDS R1, R0, #0x20     (E2901020)
    // 0x02000008: SUBEQ R2, R1, #0x08    (02412008 -> skipped because Z==0)
    // 0x0200000C: SUBNE R2, R1, #0x08    (12412008 -> executed)
    // 0x02000010: MOV PC, LR             (E1A0F00E)
    bus.write32(0x02000000U, 0xE3A00010U);
    bus.write32(0x02000004U, 0xE2901020U);
    bus.write32(0x02000008U, 0x02412008U);
    bus.write32(0x0200000CU, 0x12412008U);
    bus.write32(0x02000010U, 0xE1A0F00EU);
    regs.write(RegisterIndex::Lr, 0x02001000U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R0), 0x10U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R1), 0x30U);
    EXPECT_FALSE(regs.cpsr().zero());

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R2), 0U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R2), 0x28U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02001000U);
}

TEST(ArmDataProcessingTest, ExecutesMultiplyClzAndSaturatingInstructions) {
    RegisterBank regs(ArmArchitecture::ArmV5Te);
    test::MockMemoryBus bus;
    ArmCoreContext ctx{regs, bus, false, false};
    regs.reset(0x02000000U);
    regs.write(RegisterIndex::R1, 0x00010000U);
    regs.write(RegisterIndex::R2, 0x00020000U);
    regs.write(RegisterIndex::R3, 0x00000010U);
    regs.write(RegisterIndex::R4, 0x7FFFFFF0U);
    regs.write(RegisterIndex::R5, 0x00000020U);

    // UMULL R6, R7, R1, R2  -> R7:R6 = 0x00000002_00000000 (E0876291)
    // CLZ R8, R3            -> 27                            (E16F8F13)
    // QADD R9, R4, R5       -> 0x7FFFFFFF, Q=1               (E1059054)
    bus.write32(0x02000000U, 0xE0876291U);
    bus.write32(0x02000004U, 0xE16F8F13U);
    bus.write32(0x02000008U, 0xE1059054U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R6), 0x00000000U);
    EXPECT_EQ(regs.read(RegisterIndex::R7), 0x00000002U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R8), 27U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R9), 0x7FFFFFFFU);
    EXPECT_TRUE(regs.cpsr().saturation());
}

}  // namespace
}  // namespace elmasds::cpu
