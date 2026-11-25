#include "cpu/arm7/arm7_tdmi.hpp"
#include "mock_memory_bus.hpp"

#include <gtest/gtest.h>

#include <initializer_list>

namespace elmasds::cpu {
namespace {

class FetchCountingBus final : public CpuBusInterface {
public:
    u8 read8(Address address) override { return inner_.read8(address); }
    u16 read16(Address address) override { return inner_.read16(address); }
    u32 read32(Address address) override { return inner_.read32(address); }

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

    u32 fetch16_count = 0;
    u32 fetch32_count = 0;

private:
    test::MockMemoryBus inner_;
};

void load_arm(CpuBusInterface& bus, Address base, std::initializer_list<u32> words) {
    Address addr = base;
    for (const u32 word : words) {
        bus.write32(addr, word);
        addr += 4U;
    }
}

void load_thumb(CpuBusInterface& bus, Address base, std::initializer_list<u16> halfwords) {
    Address addr = base;
    for (const u16 halfword : halfwords) {
        bus.write16(addr, halfword);
        addr += 2U;
    }
}

void enter_user_unmasked(Arm7Tdmi& cpu, Address pc, bool thumb) {
    cpu.registers().switch_mode(CpuMode::User);
    cpu.registers().set_cpsr(
        cpu.registers().cpsr().with_irq_disabled(false).with_fiq_disabled(false).with_thumb(thumb));
    cpu.registers().write(RegisterIndex::Pc, pc);
}

TEST(Arm7TdmiTest, ExecutesArmAndThumbSequencesThroughBus) {
    FetchCountingBus bus;
    Arm7Tdmi cpu(bus);
    cpu.reset(0x02000000U);

    // ARM: MOV R0,#0x10; MOV SP,#0x03000000; ADD R1,R0,#5; STR R1,[SP,#-4]!; LDR R3,[SP],#4; BX R4
    load_arm(bus, 0x02000000U,
             {0xE3A00010U, 0xE3A0D403U, 0xE2801005U, 0xE52D1004U, 0xE49D3004U, 0xE12FFF14U});
    cpu.registers().write(RegisterIndex::R4, 0x02000101U);

    // Thumb: MOV R0,#0x15; ADD R0,#5; PUSH {R0}; POP {R1}; BX R2
    load_thumb(bus, 0x02000100U, {0x2015U, 0x3005U, 0xB401U, 0xBC02U, 0x4710U});
    cpu.registers().write(RegisterIndex::R2, 0x02000200U);

    const u64 arm_elapsed = cpu.run_cycles(11U);
    EXPECT_EQ(arm_elapsed, 11U);
    EXPECT_TRUE(cpu.registers().cpsr().thumb());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R3), 0x15U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Sp), 0x03000000U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000100U);

    u64 thumb_elapsed = 0U;
    for (int i = 0; i < 5; ++i) {
        thumb_elapsed += cpu.step();
    }
    EXPECT_FALSE(cpu.registers().cpsr().thumb());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R0), 0x1AU);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R1), 0x1AU);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000200U);
    EXPECT_EQ(bus.fetch32_count, 6U);
    EXPECT_EQ(bus.fetch16_count, 5U);
    EXPECT_EQ(cpu.total_cycles(), arm_elapsed + thumb_elapsed);
}

TEST(Arm7TdmiTest, TrapsCoprocessorOpcodesToUndefined) {
    const std::initializer_list<u32> opcodes = {
        0xEE010F10U,  // MCR
        0xEE110F10U,  // MRC
        0xEE000F00U,  // CDP
        0xED900F00U,  // LDC
        0xED800F00U,  // STC
    };
    for (const u32 opcode : opcodes) {
        test::MockMemoryBus bus;
        Arm7Tdmi cpu(bus);
        cpu.reset(0x02000000U);
        enter_user_unmasked(cpu, 0x02000000U, false);
        const StatusRegister pre_cpsr = cpu.registers().cpsr();
        bus.write32(0x02000000U, opcode);

        EXPECT_EQ(cpu.step(), 3U);
        EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Undefined);
        EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x00000004U);
        EXPECT_EQ(cpu.registers().read(RegisterIndex::Lr), 0x02000004U);
        EXPECT_EQ(cpu.registers().spsr().raw(), pre_cpsr.raw());
        EXPECT_TRUE(cpu.registers().cpsr().irq_disabled());
        EXPECT_FALSE(cpu.registers().cpsr().fiq_disabled());
        EXPECT_FALSE(cpu.registers().cpsr().thumb());
    }
}

TEST(Arm7TdmiTest, DispatchesIrqAndReturnsToArmViaSubsPcLr4) {
    test::MockMemoryBus bus;
    Arm7Tdmi cpu(bus);
    cpu.reset(0x02000000U);
    enter_user_unmasked(cpu, 0x02000000U, false);
    cpu.registers().write(RegisterIndex::Sp, 0x03000100U);
    const StatusRegister user_cpsr = cpu.registers().cpsr();

    bus.write32(0x02000000U, 0xE2800001U);  // ADD R0, R0, #1
    bus.write32(0x00000018U, 0xE25EF004U);  // SUBS PC, LR, #4

    EXPECT_EQ(cpu.step(), 1U);
    cpu.set_irq_line(true);
    EXPECT_EQ(cpu.step(), 3U);

    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Irq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x00000018U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Lr), 0x02000008U);
    EXPECT_EQ(cpu.registers().spsr().raw(), user_cpsr.raw());
    EXPECT_TRUE(cpu.registers().cpsr().irq_disabled());
    EXPECT_FALSE(cpu.registers().cpsr().fiq_disabled());
    EXPECT_NE(cpu.registers().read(RegisterIndex::Sp), 0x03000100U);

    cpu.set_irq_line(false);
    cpu.step();
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::User);
    EXPECT_EQ(cpu.registers().cpsr().raw(), user_cpsr.raw());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000004U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Sp), 0x03000100U);
}

TEST(Arm7TdmiTest, DispatchesIrqFromThumbAndReturnsViaLdmPcCaret) {
    test::MockMemoryBus bus;
    Arm7Tdmi cpu(bus);
    cpu.reset(0x00000000U);
    cpu.registers().switch_mode(CpuMode::Irq);
    cpu.registers().write(RegisterIndex::Sp, 0x03001000U);

    enter_user_unmasked(cpu, 0x02000102U, true);
    cpu.registers().write(RegisterIndex::R0, 0x11223344U);
    cpu.registers().write(RegisterIndex::Sp, 0x03000400U);

    // IRQ handler: SUB LR,LR,#4; STMFD SP!,{R0,LR}; MOV R0,#0; LDMFD SP!,{R0,PC}^
    load_arm(bus, 0x00000018U, {0xE24EE004U, 0xE92D4001U, 0xE3A00000U, 0xE8FD8001U});

    cpu.set_irq_line(true);
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Irq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x00000018U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Lr), 0x02000106U);
    EXPECT_TRUE(cpu.registers().spsr().thumb());
    EXPECT_FALSE(cpu.registers().cpsr().thumb());

    cpu.set_irq_line(false);
    for (int i = 0; i < 4; ++i) {
        cpu.step();
    }

    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::User);
    EXPECT_TRUE(cpu.registers().cpsr().thumb());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000102U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R0), 0x11223344U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Sp), 0x03000400U);
    cpu.registers().switch_mode(CpuMode::Irq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Sp), 0x03001000U);
}

TEST(Arm7TdmiTest, DispatchesFiqBanksR8ToR12AndPreemptsIrq) {
    test::MockMemoryBus bus;
    Arm7Tdmi cpu(bus);
    cpu.reset(0x02000000U);
    enter_user_unmasked(cpu, 0x02000000U, false);
    cpu.registers().write(RegisterIndex::R8, 0xDEADBEEFU);

    load_arm(bus, 0x0000001CU, {0xE3A08042U, 0xE25EF004U});  // MOV R8,#0x42; SUBS PC,LR,#4

    cpu.set_irq_line(true);
    cpu.set_fiq_line(true);
    EXPECT_EQ(cpu.step(), 3U);

    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Fiq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x0000001CU);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Lr), 0x02000004U);
    EXPECT_TRUE(cpu.registers().cpsr().irq_disabled());
    EXPECT_TRUE(cpu.registers().cpsr().fiq_disabled());

    cpu.set_irq_line(false);
    cpu.set_fiq_line(false);
    cpu.step();
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R8), 0x42U);
    EXPECT_EQ(cpu.registers().read_user(RegisterIndex::R8), 0xDEADBEEFU);

    cpu.step();
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::User);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R8), 0xDEADBEEFU);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000000U);
}

TEST(Arm7TdmiTest, RespectsIrqFiqMasksAndWakesFromHaltedWfi) {
    test::MockMemoryBus bus;
    Arm7Tdmi cpu(bus);
    cpu.reset(0x02000000U);
    load_arm(bus, 0x02000000U, {0xE2800001U, 0xE2800002U});

    // (a) I=1, F=1: both ignored; (d) halted idles for 4 cycles.
    cpu.set_irq_line(true);
    cpu.set_fiq_line(true);
    EXPECT_EQ(cpu.step(), 1U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R0), 1U);

    cpu.set_irq_line(false);
    cpu.set_fiq_line(false);
    cpu.set_halted(true);
    EXPECT_EQ(cpu.run_cycles(4U), 4U);
    EXPECT_TRUE(cpu.is_halted());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000004U);

    // (e) Masked wake (I=1): wakes and executes next instruction without vectoring.
    cpu.set_irq_line(true);
    EXPECT_EQ(cpu.step(), 1U);
    EXPECT_FALSE(cpu.is_halted());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R0), 3U);

    // (b) I=1, F=0: IRQ ignored, FIQ dispatches to 0x1C.
    cpu.registers().set_cpsr(cpu.registers().cpsr().with_fiq_disabled(false));
    cpu.set_fiq_line(true);
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Fiq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x0000001CU);

    // (c) & (f) I=0, F=1 while halted: FIQ ignored, IRQ wakes and vectors to 0x18.
    cpu.reset(0x02000000U);
    cpu.registers().set_cpsr(
        cpu.registers().cpsr().with_irq_disabled(false).with_fiq_disabled(true));
    cpu.set_halted(true);
    cpu.set_fiq_line(true);
    cpu.set_irq_line(true);
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_FALSE(cpu.is_halted());
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Irq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x00000018U);
}

}  // namespace
}  // namespace elmasds::cpu
