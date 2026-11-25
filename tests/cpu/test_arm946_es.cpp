#include "cpu/arm9/arm946_es.hpp"
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

void enter_user_unmasked(Arm946Es& cpu, Address pc, bool thumb) {
    cpu.registers().switch_mode(CpuMode::User);
    cpu.registers().set_cpsr(
        cpu.registers().cpsr().with_irq_disabled(false).with_fiq_disabled(false).with_thumb(thumb));
    cpu.registers().write(RegisterIndex::Pc, pc);
}

TEST(Arm946EsTest, ExecutesMixedArmV5TeAndThumbAcrossExternalBusAndTcm) {
    FetchCountingBus bus;
    Arm946Es cpu(bus);
    cpu.reset(0x02000000U, false);

    // Configure DTCM (0x027C0000, 16 KiB) & ITCM (0x01000000, 32 KiB) via CP15 MCRs.
    cpu.registers().write(RegisterIndex::R0, 0x027C000AU);
    cpu.registers().write(RegisterIndex::R1, 0x0100000CU);
    cpu.registers().write(RegisterIndex::R2, (1U << 16U) | (1U << 18U));
    load_arm(bus, 0x02000000U,
             {
                 0xEE090F11U,  // MCR p15, 0, R0, c9, c1, 0 (DTCM)
                 0xEE091F31U,  // MCR p15, 0, R1, c9, c1, 1 (ITCM)
                 0xEE012F10U,  // MCR p15, 0, R2, c1, c0, 0 (enable DTCM + ITCM)
                 0xE16F1F10U,  // CLZ R1, R0 (0x027C000A -> 6 leading zeros)
                 0xE1032050U,  // QADD R2, R0, R3 (saturates R0 + R3 -> 0x7FFFFFFF, sets Q)
                 0xE5852000U,  // STR R2, [R5] (writes into DTCM)
                 0xE5956000U,  // LDR R6, [R5] (reads from DTCM)
                 0xE12FFF34U,  // BLX R4 (jump to Thumb in ITCM, LR = 0x02000020)
             });
    EXPECT_EQ(cpu.run_cycles(3U), 3U);

    // Load Thumb routine directly into ITCM via router and set ARMv5TE operands.
    load_thumb(cpu.context().bus, 0x01000000U, {0x202AU, 0x3008U, 0xB401U, 0xBC04U, 0x4770U});
    cpu.registers().write(RegisterIndex::R0, 0x027C000AU);
    cpu.registers().write(RegisterIndex::R3, 0x7FFFFFFFU);
    cpu.registers().write(RegisterIndex::R4, 0x01000001U);
    cpu.registers().write(RegisterIndex::R5, 0x027C0000U);
    cpu.registers().write(RegisterIndex::Sp, 0x027C0400U);  // Stack in DTCM

    EXPECT_EQ(cpu.run_cycles(10U), 10U);  // CLZ(1)+QADD(1)+STR(2)+LDR(3)+BLX(3) = 10
    EXPECT_TRUE(cpu.registers().cpsr().thumb());
    EXPECT_TRUE(cpu.registers().cpsr().saturation());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R1), 6U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R6), 0x7FFFFFFFU);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x01000000U);

    // Execute all 5 Thumb instructions from ITCM (using DTCM stack), BX LR back to ARM.
    for (int i = 0; i < 5; ++i) {
        cpu.step();
    }
    EXPECT_FALSE(cpu.registers().cpsr().thumb());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R0), 0x32U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R2), 0x32U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000020U);
    EXPECT_EQ(bus.fetch32_count, 8U);
    EXPECT_EQ(bus.fetch16_count, 0U);  // All Thumb fetches served by ITCM
}

TEST(Arm946EsTest, TogglesHivecsAndRoutesSwiAndUndefinedVectors) {
    test::MockMemoryBus bus;
    Arm946Es cpu(bus);
    cpu.reset(0x02000000U, false);

    load_arm(bus, 0x00000008U, {0xE1B0F00EU});  // MOVS PC, LR
    load_arm(bus, 0xFFFF0008U, {0xE1B0F00EU});  // MOVS PC, LR
    load_arm(bus, 0x02000000U,
             {
                 0xEF000000U,  // SWI #0 -> 0x00000008
                 0xEE110F10U,  // MRC p15, 0, R0, c1, c0, 0
                 0xE3800A02U,  // ORR R0, R0, #0x2000 (HIVECS bit 13)
                 0xEE010F10U,  // MCR p15, 0, R0, c1, c0, 0
                 0xEF000000U,  // SWI #0 -> 0xFFFF0008
                 0xEE010F10U,  // MCR in User mode -> Undefined at 0xFFFF0004
             });

    // Low vectors SWI + return
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x00000008U);
    cpu.step();
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000004U);

    // Enable HIVECS via CP15 MCR
    cpu.run_cycles(3U);
    EXPECT_TRUE(cpu.cp15().high_vectors());
    EXPECT_TRUE(cpu.context().high_vectors);

    // High vectors SWI + return
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0xFFFF0008U);
    cpu.step();
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000014U);

    // User-mode CP15 write traps to high Undefined vector
    enter_user_unmasked(cpu, 0x02000014U, false);
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Undefined);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0xFFFF0004U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Lr), 0x02000018U);
}

TEST(Arm946EsTest, HaltsOnWaitForInterruptAndWakesOnMaskedAndUnmaskedIrq) {
    test::MockMemoryBus bus;
    Arm946Es cpu(bus);
    cpu.reset(0x02000000U, true);

    load_arm(bus, 0x02000000U,
             {
                 0xEE070F90U,  // MCR p15, 0, R0, c7, c0, 4 (WFI)
                 0xE2800001U,  // ADD R0, R0, #1
                 0xEE070F90U,  // MCR p15, 0, R0, c7, c0, 4 (WFI)
                 0xE2800002U,  // ADD R0, R0, #2
             });
    load_arm(bus, 0xFFFF0018U, {0xE25EF004U});  // SUBS PC, LR, #4

    // Phase A: CPSR.I=1 (masked). WFI halts, idles, then masked IRQ wakes inline.
    EXPECT_EQ(cpu.step(), 1U);
    EXPECT_TRUE(cpu.is_halted());
    EXPECT_EQ(cpu.run_cycles(4U), 4U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000004U);

    cpu.set_irq_line(true);
    EXPECT_EQ(cpu.step(), 1U);
    EXPECT_FALSE(cpu.is_halted());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R0), 1U);

    // Executing WFI while IRQ is already asserted (even masked) does not latch halted.
    EXPECT_EQ(cpu.step(), 1U);
    EXPECT_FALSE(cpu.is_halted());

    // Phase B: Unmasked IRQ from halted WFI jumps to HIVECS IRQ vector and returns.
    cpu.set_irq_line(false);
    cpu.registers().write(RegisterIndex::Pc, 0x02000008U);
    cpu.registers().set_cpsr(cpu.registers().cpsr().with_irq_disabled(false));
    EXPECT_EQ(cpu.step(), 1U);
    EXPECT_TRUE(cpu.is_halted());

    cpu.set_irq_line(true);
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_FALSE(cpu.is_halted());
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Irq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0xFFFF0018U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Lr), 0x02000010U);

    cpu.set_irq_line(false);
    cpu.step();
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x0200000CU);
    cpu.step();
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R0), 3U);
}

TEST(Arm946EsTest, DispatchesIrqWithHivecsOffAndOnAndReturnsViaSubsPcLr4) {
    for (const bool high_vectors : {false, true}) {
        test::MockMemoryBus bus;
        Arm946Es cpu(bus);
        cpu.reset(0x02000000U, high_vectors);
        enter_user_unmasked(cpu, 0x02000000U, false);
        cpu.registers().write(RegisterIndex::Sp, 0x03000100U);
        const StatusRegister user_cpsr = cpu.registers().cpsr();
        const Address vector = high_vectors ? 0xFFFF0018U : 0x00000018U;

        bus.write32(0x02000000U, 0xE2800001U);  // ADD R0, R0, #1
        bus.write32(vector, 0xE25EF004U);       // SUBS PC, LR, #4

        EXPECT_EQ(cpu.step(), 1U);
        cpu.set_irq_line(true);
        EXPECT_EQ(cpu.step(), 3U);
        EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Irq);
        EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), vector);
        EXPECT_EQ(cpu.registers().read(RegisterIndex::Lr), 0x02000008U);
        EXPECT_EQ(cpu.registers().spsr().raw(), user_cpsr.raw());
        EXPECT_TRUE(cpu.registers().cpsr().irq_disabled());
        EXPECT_FALSE(cpu.registers().cpsr().fiq_disabled());

        cpu.set_irq_line(false);
        cpu.step();
        EXPECT_EQ(cpu.registers().current_mode(), CpuMode::User);
        EXPECT_EQ(cpu.registers().cpsr().raw(), user_cpsr.raw());
        EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000004U);
        EXPECT_EQ(cpu.registers().read(RegisterIndex::Sp), 0x03000100U);
    }
}

TEST(Arm946EsTest, DispatchesIrqFromThumbWithHivecsAndReturnsViaLdmPcCaret) {
    test::MockMemoryBus bus;
    Arm946Es cpu(bus);
    cpu.reset(0x00000000U, true);
    cpu.registers().switch_mode(CpuMode::Irq);
    cpu.registers().write(RegisterIndex::Sp, 0x03001000U);

    enter_user_unmasked(cpu, 0x02000102U, true);
    cpu.registers().write(RegisterIndex::R0, 0x11223344U);
    cpu.registers().write(RegisterIndex::Sp, 0x03000400U);

    load_arm(bus, 0xFFFF0018U, {0xE24EE004U, 0xE92D4001U, 0xE3A00000U, 0xE8FD8001U});

    cpu.set_irq_line(true);
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Irq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0xFFFF0018U);
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
}

TEST(Arm946EsTest, EnforcesFiqPriorityR8ToR12BankingMasksAndThumbReturn) {
    test::MockMemoryBus bus;
    Arm946Es cpu(bus);
    cpu.reset(0x02000000U, true);
    bus.write32(0x02000000U, 0xE2800001U);  // ADD R0, R0, #1
    load_arm(bus, 0xFFFF001CU, {0xE3A08042U, 0xE25EF004U});

    // Masked I=1, F=1 ignores both lines.
    cpu.set_irq_line(true);
    cpu.set_fiq_line(true);
    EXPECT_EQ(cpu.step(), 1U);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R0), 1U);

    // Unmasked Thumb User state: FIQ preempts IRQ, banks R8-R12, returns to Thumb.
    enter_user_unmasked(cpu, 0x02000102U, true);
    cpu.registers().write(RegisterIndex::R8, 0xDEADBEEFU);
    EXPECT_EQ(cpu.step(), 3U);
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::Fiq);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0xFFFF001CU);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Lr), 0x02000106U);
    EXPECT_TRUE(cpu.registers().cpsr().irq_disabled());
    EXPECT_TRUE(cpu.registers().cpsr().fiq_disabled());

    cpu.set_irq_line(false);
    cpu.set_fiq_line(false);
    cpu.step();
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R8), 0x42U);
    EXPECT_EQ(cpu.registers().read_user(RegisterIndex::R8), 0xDEADBEEFU);
    cpu.step();
    EXPECT_EQ(cpu.registers().current_mode(), CpuMode::User);
    EXPECT_TRUE(cpu.registers().cpsr().thumb());
    EXPECT_EQ(cpu.registers().read(RegisterIndex::R8), 0xDEADBEEFU);
    EXPECT_EQ(cpu.registers().read(RegisterIndex::Pc), 0x02000102U);
}

}  // namespace
}  // namespace elmasds::cpu
