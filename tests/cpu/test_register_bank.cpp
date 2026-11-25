#include "cpu/arm_architecture/register_bank.hpp"

#include <gtest/gtest.h>

namespace elmasds::cpu {
namespace {

TEST(RegisterBankTest, StartsInSupervisorWithIrqAndFiqDisabled) {
    RegisterBank bank(ArmArchitecture::ArmV5Te);
    EXPECT_EQ(bank.current_mode(), CpuMode::Supervisor);
    EXPECT_TRUE(bank.cpsr().irq_disabled());
    EXPECT_TRUE(bank.cpsr().fiq_disabled());
    EXPECT_FALSE(bank.cpsr().thumb());
    EXPECT_EQ(bank.read(RegisterIndex::Pc), 0U);

    bank.reset(0xFFFF0000U);
    EXPECT_EQ(bank.read(RegisterIndex::Pc), 0xFFFF0000U);
}

TEST(RegisterBankTest, BanksSpLrAndFiqRegistersAcrossModes) {
    RegisterBank bank;
    bank.switch_mode(CpuMode::User);
    bank.write(RegisterIndex::R8, 0x11111111U);
    bank.write(RegisterIndex::Sp, 0x03007F00U);
    bank.write(RegisterIndex::Lr, 0x08001234U);

    // System mode shares User bank.
    bank.switch_mode(CpuMode::System);
    EXPECT_EQ(bank.read(RegisterIndex::R8), 0x11111111U);
    EXPECT_EQ(bank.read(RegisterIndex::Sp), 0x03007F00U);
    EXPECT_EQ(bank.read(RegisterIndex::Lr), 0x08001234U);

    // IRQ mode banks SP and LR, shares R8.
    bank.switch_mode(CpuMode::Irq);
    bank.write(RegisterIndex::Sp, 0x03007FA0U);
    bank.write(RegisterIndex::Lr, 0x00000100U);
    EXPECT_EQ(bank.read(RegisterIndex::R8), 0x11111111U);

    // FIQ mode banks R8-R12, SP, and LR independently.
    bank.switch_mode(CpuMode::Fiq);
    bank.write(RegisterIndex::R8, 0xAAAAAAAAU);
    bank.write(RegisterIndex::Sp, 0x03007FC0U);
    EXPECT_EQ(bank.read_user(RegisterIndex::R8), 0x11111111U);
    EXPECT_EQ(bank.read_user(RegisterIndex::Sp), 0x03007F00U);

    // Switching back to IRQ preserves IRQ bank.
    bank.switch_mode(CpuMode::Irq);
    EXPECT_EQ(bank.read(RegisterIndex::Sp), 0x03007FA0U);
    EXPECT_EQ(bank.read(RegisterIndex::Lr), 0x00000100U);
    EXPECT_EQ(bank.read(RegisterIndex::R8), 0x11111111U);
}

TEST(RegisterBankTest, PreservesPerExceptionModeSpsrAndEnforcesMsrPrivilege) {
    RegisterBank bank(ArmArchitecture::ArmV5Te);
    bank.switch_mode(CpuMode::Irq);
    bank.set_spsr(StatusRegister(0xF8000010U));

    bank.switch_mode(CpuMode::Supervisor);
    bank.set_spsr(StatusRegister(0x1000001FU));

    bank.switch_mode(CpuMode::Irq);
    EXPECT_EQ(bank.spsr().raw(), 0xF8000010U);

    // User mode MSR cannot alter control bits or Q flag on ARMv4T.
    const StatusRegister user_cpsr = StatusRegister(0x00000010U);
    const StatusRegister after_user_msr = user_cpsr.with_msr_write(
        PsrWriteRequest{0xFFFFFFFFU, 0x0FU, ArmArchitecture::ArmV4T}, false);
    EXPECT_EQ(after_user_msr.raw(), 0xF0000010U);

    // ARMv5TE flags mask includes Q bit (bit 27).
    const StatusRegister after_v5_msr = user_cpsr.with_msr_write(
        PsrWriteRequest{0xFFFFFFFFU, 0x0FU, ArmArchitecture::ArmV5Te}, false);
    EXPECT_EQ(after_v5_msr.raw(), 0xF8000010U);
}

}  // namespace
}  // namespace elmasds::cpu
