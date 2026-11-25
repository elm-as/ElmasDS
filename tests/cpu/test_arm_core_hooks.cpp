#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_architecture/coprocessor_interface.hpp"
#include "cpu/arm_architecture/cpu_step.hpp"
#include "cpu/arm_instructions/arm_executor.hpp"
#include "cpu/thumb_instructions/thumb_executor.hpp"
#include "mock_memory_bus.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace elmasds::cpu {
namespace {

class SpyFetchBus final : public CpuBusInterface {
public:
    u8 read8(Address address) override { return inner_.read8(address); }

    u16 read16(Address address) override {
        ++data_read16_count;
        return inner_.read16(address);
    }

    u32 read32(Address address) override {
        ++data_read32_count;
        return inner_.read32(address);
    }

    u16 fetch16(Address address) override {
        ++fetch16_count;
        return inner_.read16(address);
    }

    u32 fetch32(Address address) override {
        ++fetch32_count;
        return inner_.read32(address);
    }

    void write8(Address address, u8 value) override { inner_.write8(address, value); }
    void write16(Address address, u16 value) override { inner_.write16(address, value); }
    void write32(Address address, u32 value) override { inner_.write32(address, value); }

    u32 data_read16_count = 0;
    u32 data_read32_count = 0;
    u32 fetch16_count = 0;
    u32 fetch32_count = 0;

private:
    test::MockMemoryBus inner_;
};

class MockCoprocessor final : public CoprocessorInterface {
public:
    bool write_register(const CoprocessorTransfer& transfer, u32 value) override {
        last_transfer = transfer;
        last_written_value = value;
        ++write_calls;
        return accept_write;
    }

    std::optional<u32> read_register(const CoprocessorTransfer& transfer) override {
        last_transfer = transfer;
        ++read_calls;
        return read_response;
    }

    CoprocessorTransfer last_transfer{};
    u32 last_written_value = 0;
    u32 write_calls = 0;
    u32 read_calls = 0;
    bool accept_write = true;
    std::optional<u32> read_response = 0U;
};

TEST(ArmCoreHooksTest, BusFetch16AndFetch32SeparateInstructionFetchesFromDataReads) {
    SpyFetchBus bus;
    RegisterBank regs(ArmArchitecture::ArmV5Te);
    ArmCoreContext ctx{regs, bus, false, false, nullptr};
    regs.reset(0x02000000U);

    // ARM: LDR R0, [R1] (0xE5910000) -> 1 fetch32, 1 data read32
    // Thumb: LDRH R0, [R1, #0] (0x8808) -> 1 fetch16, 1 data read16
    bus.write32(0x02000000U, 0xE5910000U);
    bus.write16(0x02000100U, 0x8808U);
    bus.write32(0x02000200U, 0x12345678U);
    regs.write(RegisterIndex::R1, 0x02000200U);

    step_arm_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R0), 0x12345678U);
    EXPECT_EQ(bus.fetch32_count, 1U);
    EXPECT_EQ(bus.data_read32_count, 1U);
    EXPECT_EQ(bus.fetch16_count, 0U);

    regs.set_cpsr(regs.cpsr().with_thumb(true));
    regs.write(RegisterIndex::Pc, 0x02000100U);
    step_thumb_instruction(ctx);
    EXPECT_EQ(regs.read(RegisterIndex::R0), 0x5678U);
    EXPECT_EQ(bus.fetch16_count, 1U);
    EXPECT_EQ(bus.data_read16_count, 1U);
    EXPECT_EQ(bus.fetch32_count, 1U);
}

TEST(ArmCoreHooksTest, CoprocessorInterfaceHandlesMcrMrcAndMrcPcFlags) {
    test::MockMemoryBus bus;
    MockCoprocessor coproc;
    RegisterBank regs(ArmArchitecture::ArmV5Te);
    ArmCoreContext ctx{regs, bus, false, false, &coproc};
    regs.reset(0x02000000U);

    // MCR p15, 2, R3, c1, c7, 5 (0xEE413FB7)
    regs.write(RegisterIndex::R3, 0xA5A55A5AU);
    bus.write32(0x02000000U, 0xEE413FB7U);
    // MCR p15, 0, PC, c0, c0, 0 (0xEE00FF10) -> passes PC + 8
    bus.write32(0x02000004U, 0xEE00FF10U);
    // MRC p15, 1, R4, c2, c5, 3 (0xEE324F75)
    bus.write32(0x02000008U, 0xEE324F75U);
    // MRC p15, 0, PC, c0, c0, 0 (0xEE10FF10) -> updates NZCV only
    bus.write32(0x0200000CU, 0xEE10FF10U);

    step_arm_instruction(ctx);
    EXPECT_EQ(coproc.write_calls, 1U);
    EXPECT_EQ(coproc.last_written_value, 0xA5A55A5AU);
    EXPECT_EQ(coproc.last_transfer.coprocessor, 15U);
    EXPECT_EQ(coproc.last_transfer.opcode_1, 2U);
    EXPECT_EQ(coproc.last_transfer.crn, 1U);
    EXPECT_EQ(coproc.last_transfer.crm, 7U);
    EXPECT_EQ(coproc.last_transfer.opcode_2, 5U);
    EXPECT_TRUE(coproc.last_transfer.privileged);

    step_arm_instruction(ctx);
    EXPECT_EQ(coproc.last_written_value, 0x02000004U + 8U);

    coproc.read_response = 0x13572468U;
    step_arm_instruction(ctx);
    EXPECT_EQ(coproc.read_calls, 1U);
    EXPECT_EQ(regs.read(RegisterIndex::R4), 0x13572468U);
    EXPECT_EQ(coproc.last_transfer.opcode_1, 1U);
    EXPECT_EQ(coproc.last_transfer.crn, 2U);
    EXPECT_EQ(coproc.last_transfer.crm, 5U);
    EXPECT_EQ(coproc.last_transfer.opcode_2, 3U);

    // Switch to User mode and execute MRC Rd=PC: privileged=false, updates NZCV only.
    regs.switch_mode(CpuMode::User);
    coproc.read_response = 0xA0000013U;  // N=1, Z=0, C=1, V=0, low bits ignored
    step_arm_instruction(ctx);
    EXPECT_FALSE(coproc.last_transfer.privileged);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02000010U);
    EXPECT_EQ(regs.current_mode(), CpuMode::User);
    EXPECT_TRUE(regs.cpsr().negative());
    EXPECT_FALSE(regs.cpsr().zero());
    EXPECT_TRUE(regs.cpsr().carry());
    EXPECT_FALSE(regs.cpsr().overflow());
}

TEST(ArmCoreHooksTest, CoprocessorTrapsWhenMissingRejectedOrNonMcrMrc) {
    const std::vector<u32> opcodes = {0xEE413FB7U, 0xEE324F75U, 0xEE000F00U, 0xED900F00U,
                                      0xED800F00U};
    for (const u32 opcode : opcodes) {
        test::MockMemoryBus bus;
        MockCoprocessor coproc;
        coproc.accept_write = false;
        coproc.read_response = std::nullopt;
        RegisterBank regs(ArmArchitecture::ArmV5Te);
        ArmCoreContext ctx{regs, bus, false, false, &coproc};
        regs.reset(0x02000000U);
        bus.write32(0x02000000U, opcode);

        const ArmStepResult res = step_arm_instruction(ctx);
        EXPECT_EQ(res.cycles, 3U);
        EXPECT_EQ(regs.current_mode(), CpuMode::Undefined);
        EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x00000004U);
        EXPECT_EQ(regs.read(RegisterIndex::Lr), 0x02000004U);
    }

    // Also verify missing coprocessor (nullptr) traps MCR/MRC to Undefined.
    test::MockMemoryBus bus;
    RegisterBank regs(ArmArchitecture::ArmV5Te);
    ArmCoreContext null_ctx{regs, bus, false, false, nullptr};
    regs.reset(0x02000000U);
    bus.write32(0x02000000U, 0xEE413FB7U);
    EXPECT_EQ(step_arm_instruction(null_ctx).cycles, 3U);
    EXPECT_EQ(regs.current_mode(), CpuMode::Undefined);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x00000004U);
}

TEST(ArmCoreHooksTest, LdmPcCaretRestoresCpsrBeforePcAndUpdatesBankedSp) {
    for (const auto arch : {ArmArchitecture::ArmV4T, ArmArchitecture::ArmV5Te}) {
        for (const bool return_thumb : {true, false}) {
            test::MockMemoryBus bus;
            RegisterBank regs(arch);
            ArmCoreContext ctx{regs, bus, false, false, nullptr};
            regs.reset(0x00000018U);
            regs.switch_mode(CpuMode::User);
            regs.write(RegisterIndex::Sp, 0x03000200U);

            const StatusRegister saved_user = regs.cpsr().with_thumb(return_thumb);
            const Address expected_pc = return_thumb ? 0x02000102U : 0x02000200U;
            regs.switch_mode(CpuMode::Irq);
            regs.set_spsr(saved_user);
            regs.write(RegisterIndex::Sp, 0x03000FF8U);
            bus.write32(0x03000FF8U, 0xCAFEBABEU);
            bus.write32(0x03000FFCU, expected_pc);
            bus.write32(0x00000018U, 0xE8FD8001U);  // LDMFD SP!, {R0, PC}^

            step_arm_instruction(ctx);
            EXPECT_EQ(regs.current_mode(), CpuMode::User);
            EXPECT_EQ(regs.cpsr().thumb(), return_thumb);
            EXPECT_EQ(regs.read(RegisterIndex::Pc), expected_pc);
            EXPECT_EQ(regs.read(RegisterIndex::R0), 0xCAFEBABEU);
            EXPECT_EQ(regs.read(RegisterIndex::Sp), 0x03000200U);

            regs.switch_mode(CpuMode::Irq);
            EXPECT_EQ(regs.read(RegisterIndex::Sp), 0x03001000U);
        }
    }
}

TEST(ArmCoreHooksTest, StepArmCoreHandlesWfiWakeAndInterruptPriority) {
    test::MockMemoryBus bus;
    RegisterBank regs(ArmArchitecture::ArmV4T);
    ArmCoreContext ctx{regs, bus, false, true, nullptr};
    regs.reset(0x02000000U);
    regs.switch_mode(CpuMode::User);  // Supervisor reset sets I=1,F=1; User keeps I=1,F=1
    ctx.halted = true;
    bus.write32(0x02000000U, 0xE2800001U);  // ADD R0, R0, #1

    EXPECT_EQ(step_arm_core(ctx, InterruptLines{false, false}), 1U);
    EXPECT_TRUE(ctx.halted);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02000000U);

    // Masked IRQ wakes halted core without branching to 0x18, executing instruction at PC.
    EXPECT_EQ(step_arm_core(ctx, InterruptLines{true, false}), 1U);
    EXPECT_FALSE(ctx.halted);
    EXPECT_EQ(regs.read(RegisterIndex::R0), 1U);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x02000004U);

    // Unmasked FIQ + IRQ together: FIQ takes priority over IRQ.
    regs.set_cpsr(regs.cpsr().with_irq_disabled(false).with_fiq_disabled(false));
    EXPECT_EQ(step_arm_core(ctx, InterruptLines{true, true}), 3U);
    EXPECT_EQ(regs.current_mode(), CpuMode::Fiq);
    EXPECT_EQ(regs.read(RegisterIndex::Pc), 0x0000001CU);
    EXPECT_EQ(regs.read(RegisterIndex::Lr), 0x02000008U);
}

}  // namespace
}  // namespace elmasds::cpu
