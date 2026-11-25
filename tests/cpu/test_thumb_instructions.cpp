#include "cpu/thumb_instructions/thumb_executor.hpp"
#include "mock_memory_bus.hpp"

#include <gtest/gtest.h>

namespace elmasds::cpu {
namespace {

TEST(ThumbAluTest, ExecutesShiftsImmediateAndRegisterAluOperations) {
    RegisterBank regs(ArmArchitecture::ArmV4T);
    test::MockMemoryBus bus;
    ArmCoreContext ctx{regs, bus, false, false};
    regs.reset(0x02000000U);
    regs.set_cpsr(regs.cpsr().with_thumb(true));

    // 0x02000000: MOV R0, #0x15         (0x2015)
    // 0x02000002: LSL R1, R0, #2        (0x0081 -> R1 = 0x54)
    // 0x02000004: ADD R2, R1, #5        (0x1D4A -> R2 = 0x59)
    // 0x02000006: NEG R3, R2            (0x4253 -> R3 = -0x59)
    // 0x02000008: MUL R1, R0            (0x4341 -> R1 = 0x54 * 0x15 = 0x06E4)
    bus.write16(0x02000000U, 0x2015U);
    bus.write16(0x02000002U, 0x0081U);
    bus.write16(0x02000004U, 0x1D4AU);
    bus.write16(0x02000006U, 0x4253U);
    bus.write16(0x02000008U, 0x4341U);

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R0), 0x15U);

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R1), 0x54U);

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R2), 0x59U);

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R3), static_cast<u32>(-0x59));
    EXPECT_TRUE(regs.cpsr().negative());

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R1), 0x06E4U);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x0200000AU);
}

TEST(ThumbBranchTest, ExecutesConditionalBranchLongBlAndArm9Blx) {
    RegisterBank regs(ArmArchitecture::ArmV5Te);
    test::MockMemoryBus bus;
    ArmCoreContext ctx{regs, bus, false, false};
    regs.reset(0x02000000U);
    regs.set_cpsr(regs.cpsr().with_thumb(true).with_nzcv(false, true, false, false));

    // 0x02000000: BEQ +4 (PC+4 + 2*2 = 0x02000008)        (0xD002)
    // 0x02000008: BL prefix (+0x0000)                     (0xF000)
    // 0x0200000A: BLX suffix (+0x0100 -> target 0x0200010C, T=0) (0xE880)
    bus.write16(0x02000000U, 0xD002U);
    bus.write16(0x02000008U, 0xF000U);
    bus.write16(0x0200000AU, 0xE880U);

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02000008U);

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::Lr), 0x0200000CU);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x0200000AU);

    step_thumb_instruction(ctx);
    EXPECT_FALSE(regs.cpsr().thumb());
    EXPECT_EQ(regs.read(RegisterIndex::Lr), 0x0200000DU);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x0200010CU);
}

TEST(ThumbMemoryTest, HandlesPcRelativeLoadPushPopAndArm7VsArm9PopPc) {
    // On ARMv4T, POP {pc} with even address stays in Thumb; on ARMv5TE it switches to ARM.
    for (const auto arch : {ArmArchitecture::ArmV4T, ArmArchitecture::ArmV5Te}) {
        RegisterBank regs(arch);
        test::MockMemoryBus bus;
        ArmCoreContext ctx{regs, bus, false, false};
        regs.reset(0x02000002U);
        regs.set_cpsr(regs.cpsr().with_thumb(true));
        regs.write(RegisterIndex::Sp, 0x03001000U);
        regs.write(RegisterIndex::R0, 0xCAFEBABEU);
        regs.write(RegisterIndex::Lr, 0x02000100U);  // Bit 0 == 0

        // 0x02000002: LDR R1, [PC, #4] -> base = ((0x02000002 + 4) & ~3) = 0x02000004, +4 = 0x02000008 (0x4901)
        // 0x02000004: PUSH {R0, LR}    (0xB501)
        // 0x02000006: POP  {R2, PC}    (0xBD04)
        bus.write32(0x02000008U, 0xDEAD1234U);
        bus.write16(0x02000002U, 0x4901U);
        bus.write16(0x02000004U, 0xB501U);
        bus.write16(0x02000006U, 0xBD04U);

        step_thumb_instruction(ctx);
        EXPECT_EQ(regs.read(RegisterIndex::R1), 0xDEAD1234U);

        step_thumb_instruction(ctx);
        EXPECT_EQ(regs.read(RegisterIndex::Sp), 0x03000FF8U);

        step_thumb_instruction(ctx);
        EXPECT_EQ(regs.read(RegisterIndex::Sp), 0x03001000U);
        EXPECT_EQ(regs.read(RegisterIndex::R2), 0xCAFEBABEU);
        EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02000100U);
        EXPECT_EQ(regs.cpsr().thumb(), arch == ArmArchitecture::ArmV4T);
    }
}

TEST(ThumbMemoryTest, ExecutesHiRegOpsAndThumbSwi) {
    RegisterBank regs(ArmArchitecture::ArmV4T);
    test::MockMemoryBus bus;
    ArmCoreContext ctx{regs, bus, false, false};
    regs.reset(0x02000000U);
    regs.set_cpsr(regs.cpsr().with_thumb(true).with_nzcv(false, true, false, false));
    regs.write(RegisterIndex::R8, 0x10U);

    // 0x02000000: ADD R0, R8 (R0=0 -> 0x10, must preserve Z=1!) (0x4440)
    // 0x02000002: SWI #0     (0xDF00 -> SVC mode, ARM state, PC=0x08, LR=0x02000004)
    bus.write16(0x02000000U, 0x4440U);
    bus.write16(0x02000002U, 0xDF00U);

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R0), 0x10U);
    EXPECT_TRUE(regs.cpsr().zero());

    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.current_mode(), CpuMode::Supervisor);
    EXPECT_FALSE(regs.cpsr().thumb());
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x00000008U);
    EXPECT_EQ(regs.read(RegisterIndex::Lr), 0x02000004U);
}

}  // namespace
}  // namespace elmasds::cpu
