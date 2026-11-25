#include "cpu/arm_instructions/arm_executor.hpp"
#include "mock_memory_bus.hpp"

#include <gtest/gtest.h>

namespace elmasds::cpu {
namespace {

TEST(ArmBranchAndExceptionTest, HandlesBranchLinkBxBlxAndSwiReturn) {
    RegisterBank regs(ArmArchitecture::ArmV5Te);
    test::MockMemoryBus bus;
    ArmCoreContext ctx{regs, bus, true, false};  // HIVECS enabled -> 0xFFFF0000
    regs.reset(0x02000000U);

    // 0x02000000: BL +0x08 -> target 0x02000010, LR=0x02000004 (EB000002)
    // 0x02000010: SWI #0   -> jumps to 0xFFFF0008, SVC LR=0x02000014 (EF000000)
    // 0xFFFF0008: MOVS PC, LR (E1B0F00E) -> returns to 0x02000014 and restores CPSR
    // 0x02000014: BX R0 (R0 = 0x02000101 -> Thumb mode at 0x02000100) (E12FFF10)
    bus.write32(0x02000000U, 0xEB000002U);
    bus.write32(0x02000010U, 0xEF000000U);
    bus.write32(0xFFFF0008U, 0xE1B0F00EU);
    bus.write32(0x02000014U, 0xE12FFF10U);
    regs.switch_mode(CpuMode::User);
    regs.write(RegisterIndex::R0, 0x02000101U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02000010U);
    EXPECT_EQ(regs.read(RegisterIndex::Lr), 0x02000004U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.current_mode(), CpuMode::Supervisor);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0xFFFF0008U);
    EXPECT_EQ(regs.read(RegisterIndex::Lr), 0x02000014U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.current_mode(), CpuMode::User);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02000014U);

    step_arm_instruction(ctx);
    EXPECT_TRUE(regs.cpsr().thumb());
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02000100U);
}

TEST(ArmBranchAndExceptionTest, SelfBranchDoesNotFallThrough) {
    RegisterBank regs(ArmArchitecture::ArmV4T);
    test::MockMemoryBus bus;
    ArmCoreContext ctx{regs, bus, false, false};
    regs.reset(0x08000000U);

    // B . (offset = -8 => imm24 = 0xFFFFFE => EAFFFFFE)
    bus.write32(0x08000000U, 0xEAFFFFFEU);
    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x08000000U);
}

TEST(ArmMemoryTransferTest, ExecutesLdrStrUnalignedRotationAndLdrshArm7Quirk) {
    RegisterBank regs(ArmArchitecture::ArmV4T);
    test::MockMemoryBus bus;
    ArmCoreContext ctx{regs, bus, false, false};
    regs.reset(0x03000000U);
    regs.write(RegisterIndex::R0, 0x02000000U);
    bus.write32(0x02000000U, 0x11223380U);

    // LDR R1, [R0, #1] -> rotated right by 8 => 0x80112233 (E5901001)
    // LDRSH R2, [R0, #0] -> 0x3380 (sign-extended = 0x00003380) (E1D020F0)
    // LDRSH R3, [R0, #1] -> ARM7 odd-addr sign-extends byte 0x33 => 0x33, or at offset 0: 0x80 -> -128!
    bus.write32(0x03000000U, 0xE5901001U);
    bus.write32(0x03000004U, 0xE1D020F0U);
    bus.write32(0x03000008U, 0xE1D030F1U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R1), 0x80112233U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R2), 0x00003380U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R3), 0x00000033U);
}

TEST(ArmMemoryTransferTest, EnforcesArm7VsArm9StmAndLdmBaseWritebackRules) {
    // STMDB SP!, {R0, SP} where SP is NOT first in rlist:
    // ARMv4T (ARM7) stores the NEW SP; ARMv5TE (ARM9) stores the OLD SP.
    for (const auto arch : {ArmArchitecture::ArmV4T, ArmArchitecture::ArmV5Te}) {
        RegisterBank regs(arch);
        test::MockMemoryBus bus;
        ArmCoreContext ctx{regs, bus, false, false};
        regs.reset(0x02000000U);
        regs.write(RegisterIndex::R0, 0xAAAAAAAAU);
        regs.write(RegisterIndex::Sp, 0x03001000U);

        // STMDB SP!, {R0, R13} -> E92D2001 (SP becomes 0x03000FF8; R0 at 0x03000FF8, SP at 0x03000FFC)
        bus.write32(0x02000000U, 0xE92D2001U);
        step_arm_instruction(ctx);

        EXPECT_EQ(regs.read(RegisterIndex::Sp), 0x03000FF8U);
        EXPECT_EQ(bus.read32(0x03000FF8U), 0xAAAAAAAAU);
        const u32 stored_sp = bus.read32(0x03000FFCU);
        if (arch == ArmArchitecture::ArmV4T) {
            EXPECT_EQ(stored_sp, 0x03000FF8U);
        } else {
            EXPECT_EQ(stored_sp, 0x03001000U);
        }
    }

    // LDMIA R0!, {R0, R1}: R0 is NOT the last register in rlist.
    // ARMv4T suppresses writeback (R0 = loaded value); ARMv5TE writes back (R0 = final_base).
    for (const auto arch : {ArmArchitecture::ArmV4T, ArmArchitecture::ArmV5Te}) {
        RegisterBank regs(arch);
        test::MockMemoryBus bus;
        ArmCoreContext ctx{regs, bus, false, false};
        regs.reset(0x02000000U);
        regs.write(RegisterIndex::R0, 0x02000100U);
        bus.write32(0x02000100U, 0x12345678U);
        bus.write32(0x02000104U, 0x9ABCDEF0U);

        // LDMIA R0!, {R0, R1} -> E8B00003
        bus.write32(0x02000000U, 0xE8B00003U);
        step_arm_instruction(ctx);

        EXPECT_EQ(regs.read(RegisterIndex::R1), 0x9ABCDEF0U);
        if (arch == ArmArchitecture::ArmV4T) {
            EXPECT_EQ(regs.read(RegisterIndex::R0), 0x12345678U);
        } else {
            EXPECT_EQ(regs.read(RegisterIndex::R0), 0x02000108U);
        }
    }
}

}  // namespace
}  // namespace elmasds::cpu
