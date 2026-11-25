#include "cpu/arm_architecture/bus_interface.hpp"
#include "hardware/dma/dma_controller.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/irq_source.hpp"
#include "memory/memory_bus/arm7_memory_bus.hpp"
#include "memory/memory_bus/arm9_memory_bus.hpp"
#include "memory/memory_bus/memory_system.hpp"

#include <gtest/gtest.h>
#include <unordered_map>
#include <vector>

namespace elmasds::hardware::dma {
namespace {

class RecordingBus final : public cpu::CpuBusInterface {
public:
    u8 read8(Address address) override { return read_byte(address); }

    u16 read16(Address address) override {
        const Address aligned = address & ~1U;
        const u16 lo = read_byte(aligned);
        const u16 hi = read_byte(aligned + 1U);
        return static_cast<u16>(lo | (hi << 8U));
    }

    u32 read32(Address address) override {
        const Address aligned = address & ~3U;
        const u32 lo = read16(aligned);
        const u32 hi = read16(aligned + 2U);
        return lo | (hi << 16U);
    }

    void write8(Address address, u8 value) override {
        write_log.push_back(address);
        storage[address] = value;
    }

    void write16(Address address, u16 value) override {
        const Address aligned = address & ~1U;
        write_log.push_back(aligned);
        storage[aligned] = static_cast<u8>(value & 0xFFU);
        storage[aligned + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
    }

    void write32(Address address, u32 value) override {
        const Address aligned = address & ~3U;
        write_log.push_back(aligned);
        storage[aligned] = static_cast<u8>(value & 0xFFU);
        storage[aligned + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
        storage[aligned + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
        storage[aligned + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
    }

    std::unordered_map<Address, u8> storage;
    std::vector<Address> write_log;

private:
    [[nodiscard]] u8 read_byte(Address address) const {
        const auto it = storage.find(address);
        return (it == storage.end()) ? 0U : it->second;
    }
};

struct DmaHarness {
    RecordingBus bus;
    interrupts::InterruptController irq;
    DmaController dma;

    explicit DmaHarness(interrupts::CpuKind cpu = interrupts::CpuKind::Arm9)
        : irq(cpu), dma(cpu, bus, irq) {}
};

TEST(DmaControllerTest, ResetAndOutOfRangeAccessesAreSafe) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    DmaHarness arm7(interrupts::CpuKind::Arm7);

    EXPECT_EQ(arm9.dma.cpu(), interrupts::CpuKind::Arm9);
    EXPECT_EQ(arm9.dma.read32(0x040000ACU), 0U);
    EXPECT_EQ(arm9.dma.read32(0x040000F0U), 0U);
    EXPECT_FALSE(arm9.dma.is_channel_active(0U));
    EXPECT_FALSE(arm9.dma.is_channel_active(4U));

    arm9.dma.write32(0x040000ACU, 0x12345678U);
    arm9.dma.write32(0x040000F0U, 0x12345678U);
    EXPECT_EQ(arm9.dma.read32(0x040000ACU), 0U);
    EXPECT_EQ(arm9.dma.read32(0x040000F0U), 0U);

    // ARM7 ignores DMA0..3FILL (0x040000E0..0x040000EF).
    arm7.dma.write32(0x040000E0U, 0xCAFEBABEU);
    EXPECT_EQ(arm7.dma.read32(0x040000E0U), 0U);
    EXPECT_EQ(arm7.dma.fill_register(0U), 0U);

    arm9.dma.write32(0x040000E0U, 0xCAFEBABEU);
    arm9.dma.write32(0x040000B0U, 0x02000100U);
    arm9.dma.write16(0x040000BAU, 0x8800U);  // VBlank enable
    EXPECT_TRUE(arm9.dma.is_channel_active(0U));

    arm9.dma.reset();
    EXPECT_FALSE(arm9.dma.is_channel_active(0U));
    EXPECT_EQ(arm9.dma.read32(0x040000B0U), 0U);
    EXPECT_EQ(arm9.dma.read32(0x040000E0U), 0U);
}

TEST(DmaControllerTest, RegisterMasksAndReadbackMatchArm9AndArm7Specs) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    DmaHarness arm7(interrupts::CpuKind::Arm7);

    // ARM9 masks SAD/DAD to 0x0FFFFFFE and allows full 16-bit CNT_H + 21-bit count.
    arm9.dma.write32(0x040000B0U, 0xFFFFFFFFU);
    arm9.dma.write32(0x040000B4U, 0xFFFFFFFFU);
    arm9.dma.write32(0x040000B8U, 0x081FFFFFU);  // VBlank + 21-bit count
    EXPECT_EQ(arm9.dma.read32(0x040000B0U), 0x0FFFFFFEU);
    EXPECT_EQ(arm9.dma.read32(0x040000B4U), 0x0FFFFFFEU);
    EXPECT_EQ(arm9.dma.read32(0x040000B8U), 0x081FFFFFU);

    // ARM7 DMA0: 27-bit SAD/DAD, 14-bit CNT_L, CNT_H masked by 0xF7E0.
    arm7.dma.write32(0x040000B0U, 0xFFFFFFFFU);
    arm7.dma.write32(0x040000B4U, 0xFFFFFFFFU);
    arm7.dma.write32(0x040000B8U, 0x1FFFU << 16U | 0xFFFFU);
    EXPECT_EQ(arm7.dma.read32(0x040000B0U), 0x07FFFFFEU);
    EXPECT_EQ(arm7.dma.read32(0x040000B4U), 0x07FFFFFEU);
    EXPECT_EQ(arm7.dma.read16(0x040000B8U), 0x3FFFU);
    EXPECT_EQ(arm7.dma.read16(0x040000BAU), 0x17E0U);

    // ARM7 DMA1: 28-bit SAD, 27-bit DAD; DMA3: 28-bit SAD/DAD, 16-bit CNT_L.
    arm7.dma.write32(0x040000BCU, 0xFFFFFFFFU);
    arm7.dma.write32(0x040000C0U, 0xFFFFFFFFU);
    arm7.dma.write32(0x040000D4U, 0xFFFFFFFFU);
    arm7.dma.write32(0x040000D8U, 0xFFFFFFFFU);
    arm7.dma.write16(0x040000DCU, 0xFFFFU);
    EXPECT_EQ(arm7.dma.read32(0x040000BCU), 0x0FFFFFFEU);
    EXPECT_EQ(arm7.dma.read32(0x040000C0U), 0x07FFFFFEU);
    EXPECT_EQ(arm7.dma.read32(0x040000D4U), 0x0FFFFFFEU);
    EXPECT_EQ(arm7.dma.read32(0x040000D8U), 0x0FFFFFFEU);
    EXPECT_EQ(arm7.dma.read16(0x040000DCU), 0xFFFFU);
}

TEST(DmaControllerTest, Supports8Bit16BitAnd32BitMmioAcrossChannelsAndFill) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    arm9.bus.write32(0x02000100U, 0x11223344U);

    // 8-bit writes to DMA1SAD, DMA1DAD, DMA1CNT_L, DMA1CNT_H, and DMA1FILL.
    arm9.dma.write8(0x040000BCU, 0x00U);
    arm9.dma.write8(0x040000BDU, 0x01U);
    arm9.dma.write8(0x040000BEU, 0x00U);
    arm9.dma.write8(0x040000BFU, 0x02U);
    arm9.dma.write16(0x040000C0U, 0x0200U);
    arm9.dma.write16(0x040000C2U, 0x0200U);
    arm9.dma.write8(0x040000C4U, 0x01U);
    arm9.dma.write8(0x040000C6U, 0x00U);
    arm9.dma.write8(0x040000C7U, 0x84U);  // Enable + 32-bit Immediate

    EXPECT_EQ(arm9.bus.read32(0x02000200U), 0x11223344U);
    EXPECT_EQ(arm9.dma.read8(0x040000BDU), 0x01U);

    arm9.dma.write16(0x040000E4U, 0xBEEFU);
    arm9.dma.write16(0x040000E6U, 0xDEADU);
    EXPECT_EQ(arm9.dma.read32(0x040000E4U), 0xDEADBEEFU);
    EXPECT_EQ(arm9.dma.read8(0x040000E7U), 0xDEU);
}

TEST(DmaControllerTest, AddressingModesProhibitedSrcRepeatClearAndIrqs) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    arm9.bus.write32(0x02000108U, 0xAAAA0001U);
    arm9.bus.write32(0x02000104U, 0xBBBB0002U);

    // DMA0: 32-bit Dec src -> Dec dst, Immediate+Repeat (must still clear Enable), IRQ ch0.
    arm9.dma.write32(0x040000B0U, 0x02000108U);
    arm9.dma.write32(0x040000B4U, 0x02000208U);
    arm9.dma.write32(0x040000B8U, 0xC6A00002U);  // En|IRQ|32bit|Repeat|Src=Dec|Dst=Dec|cnt=2
    EXPECT_EQ(arm9.bus.read32(0x02000208U), 0xAAAA0001U);
    EXPECT_EQ(arm9.bus.read32(0x02000204U), 0xBBBB0002U);
    EXPECT_FALSE(arm9.dma.is_channel_active(0U));
    EXPECT_EQ(arm9.irq.if_flags(), interrupts::irq_bit_mask(interrupts::IrqSource::Dma0));

    // DMA1: Fixed src, 16-bit Inc dst, IRQ ch1.
    arm9.bus.write16(0x02000300U, 0x5A5AU);
    arm9.dma.write32(0x040000BCU, 0x02000300U);
    arm9.dma.write32(0x040000C0U, 0x02000400U);
    arm9.dma.write32(0x040000C4U, 0xC1000003U);
    EXPECT_EQ(arm9.bus.read16(0x02000400U), 0x5A5AU);
    EXPECT_EQ(arm9.bus.read16(0x02000404U), 0x5A5AU);

    // DMA2: Src=3 (prohibited) -> safe no-op, zero writes, Enable cleared, no IRQ.
    arm9.bus.write_log.clear();
    arm9.dma.write32(0x040000C8U, 0x02000100U);
    arm9.dma.write32(0x040000CCU, 0x02000500U);
    arm9.dma.write32(0x040000D0U, 0xC5800004U);  // En|IRQ|32bit|Src=3|cnt=4
    EXPECT_TRUE(arm9.bus.write_log.empty());
    EXPECT_FALSE(arm9.dma.is_channel_active(2U));
    EXPECT_EQ(arm9.irq.if_flags() & interrupts::irq_bit_mask(interrupts::IrqSource::Dma2), 0U);
}

TEST(DmaControllerTest, ZeroWordCountMaxRulesAnd21BitCount) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    DmaHarness arm7(interrupts::CpuKind::Arm7);

    // Verify 0-count max computation rules across ARM9 and ARM7 channels.
    EXPECT_EQ(arm9.dma.channel(0U).compute_word_count(interrupts::CpuKind::Arm9, 0U), 0x200000U);
    EXPECT_EQ(arm7.dma.channel(0U).compute_word_count(interrupts::CpuKind::Arm7, 0U), 0x4000U);
    EXPECT_EQ(arm7.dma.channel(2U).compute_word_count(interrupts::CpuKind::Arm7, 2U), 0x4000U);
    EXPECT_EQ(arm7.dma.channel(3U).compute_word_count(interrupts::CpuKind::Arm7, 3U), 0x10000U);

    // ARM9 21-bit transfer (> 0xFFFF): 0x10002 halfwords using Fixed src -> Inc dst.
    arm9.bus.write16(0x02000000U, 0x1234U);
    arm9.dma.write32(0x040000B0U, 0x02000000U);
    arm9.dma.write32(0x040000B4U, 0x02100000U);
    arm9.dma.write32(0x040000B8U, 0x81010002U);  // En | Src=Fixed | count=0x10002
    EXPECT_EQ(arm9.bus.read16(0x02100000U + (0x10001U * 2U)), 0x1234U);
    EXPECT_EQ(arm9.bus.read16(0x02100000U + (0x10002U * 2U)), 0x0000U);

    // ARM7 DMA0 count=0 executes 0x4000 halfwords.
    arm7.bus.write16(0x03800000U, 0x7788U);
    arm7.dma.write32(0x040000B0U, 0x03800000U);
    arm7.dma.write32(0x040000B4U, 0x03810000U);
    arm7.dma.write32(0x040000B8U, 0x81000000U);  // En | Src=Fixed | count=0 (-> 0x4000)
    EXPECT_EQ(arm7.bus.read16(0x03810000U + (0x3FFFU * 2U)), 0x7788U);
    EXPECT_EQ(arm7.bus.read16(0x03810000U + (0x4000U * 2U)), 0x0000U);
}

TEST(DmaControllerTest, ShadowLatchesLoadOnRisingEdgeAndRepeatDestReload) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    arm9.bus.write32(0x02001000U, 0x00000011U);
    arm9.bus.write32(0x02001004U, 0x00000022U);
    arm9.bus.write32(0x02001008U, 0x00000033U);
    arm9.bus.write32(0x0200100CU, 0x00000044U);

    // Channel 0: VBlank, Repeat, 32-bit, Dest=Inc/Reload (3), count=2 -> latches at 0->1.
    arm9.dma.write32(0x040000B0U, 0x02001000U);
    arm9.dma.write32(0x040000B4U, 0x02002000U);
    arm9.dma.write32(0x040000B8U, 0x8E600002U);

    // Mutate MMIO SAD/DAD/CNT_L while active and write 1->1: latches must NOT reload!
    arm9.dma.write32(0x040000B0U, 0x02009000U);
    arm9.dma.write32(0x040000B4U, 0x02003000U);
    arm9.dma.write32(0x040000B8U, 0x8E600001U);

    arm9.bus.write_log.clear();
    arm9.dma.trigger_vblank();
    EXPECT_EQ(arm9.bus.read32(0x02002000U), 0x00000011U);
    EXPECT_EQ(arm9.bus.read32(0x02002004U), 0x00000022U);

    // Second VBlank: Dest=3 reloads internal_dad from DAD (0x02003000), count from CNT_L (1).
    arm9.bus.write_log.clear();
    arm9.dma.trigger_vblank();
    EXPECT_EQ(arm9.bus.read32(0x02003000U), 0x00000033U);
    EXPECT_EQ(arm9.bus.read32(0x02003004U), 0x00000000U);
}

TEST(DmaControllerTest, TriggerTypesHBlankPauseAndPriorityOrder) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    for (u32 ch = 0U; ch < 4U; ++ch) {
        arm9.bus.write32(0x02000100U + (ch * 4U), 0xA0U + ch);
        const Address base = 0x040000B0U + (ch * 12U);
        arm9.dma.write32(base, 0x02000100U + (ch * 4U));
        arm9.dma.write32(base + 4U, 0x02000200U);
        // VBlank (1 << 27), 32-bit, Fixed dst, count=1.
        arm9.dma.write32(base + 8U, 0x8C400001U);
    }
    arm9.bus.write_log.clear();
    arm9.dma.trigger_vblank();
    ASSERT_EQ(arm9.bus.write_log.size(), 4U);
    EXPECT_EQ(arm9.bus.read32(0x02000200U), 0xA3U);  // DMA3 ran last (0 > 1 > 2 > 3)

    // HBlank paused while VBlank active, resumes when cleared; Cartridge & GXFIFO triggers.
    arm9.bus.write32(0x02000300U, 0x55556666U);
    arm9.dma.write32(0x040000B0U, 0x02000300U);
    arm9.dma.write32(0x040000B4U, 0x02000400U);
    arm9.dma.write32(0x040000B8U, 0x94000001U);  // HBlank (2 << 27)
    arm9.dma.set_vblank_active(true);
    arm9.dma.trigger_hblank();
    EXPECT_TRUE(arm9.dma.is_channel_active(0U));
    arm9.dma.set_vblank_active(false);
    arm9.dma.trigger_hblank();
    EXPECT_FALSE(arm9.dma.is_channel_active(0U));
    EXPECT_EQ(arm9.bus.read32(0x02000400U), 0x55556666U);

    arm9.dma.write32(0x040000BCU, 0x02000300U);
    arm9.dma.write32(0x040000C0U, 0x02000404U);
    arm9.dma.write32(0x040000C4U, 0xAC000001U);  // CartridgeSlot1 (5 << 27)
    arm9.dma.trigger_cartridge();
    EXPECT_EQ(arm9.bus.read32(0x02000404U), 0x55556666U);

    arm9.dma.write32(0x040000C8U, 0x02000300U);
    arm9.dma.write32(0x040000CCU, 0x02000408U);
    arm9.dma.write32(0x040000D0U, 0xBC000001U);  // GeometryFifo (7 << 27)
    arm9.dma.trigger_gxfifo();
    EXPECT_EQ(arm9.bus.read32(0x02000408U), 0x55556666U);

    // ARM7 Wireless (ch0 timing=3) and GbaSlot (ch1 timing=3).
    DmaHarness arm7(interrupts::CpuKind::Arm7);
    arm7.bus.write32(0x03800000U, 0x13572468U);
    arm7.dma.write32(0x040000B0U, 0x03800000U);
    arm7.dma.write32(0x040000B4U, 0x03800100U);
    arm7.dma.write32(0x040000B8U, 0xB4000001U);  // ch0 timing=3 -> Wireless
    arm7.dma.write32(0x040000BCU, 0x03800000U);
    arm7.dma.write32(0x040000C0U, 0x03800200U);
    arm7.dma.write32(0x040000C4U, 0xB4000001U);  // ch1 timing=3 -> GbaSlot
    arm7.dma.trigger(DmaTrigger::Wireless);
    EXPECT_EQ(arm7.bus.read32(0x03800100U), 0x13572468U);
    EXPECT_EQ(arm7.bus.read32(0x03800200U), 0x00000000U);
    arm7.dma.trigger(DmaTrigger::GbaSlot);
    EXPECT_EQ(arm7.bus.read32(0x03800200U), 0x13572468U);
}

TEST(DmaControllerTest, MemorySystemIntegrationWithArm9FillAndArm7Transfers) {
    memory::MemorySystem mem;
    memory::Arm9MemoryBus bus9(mem);
    memory::Arm7MemoryBus bus7(mem);
    interrupts::InterruptController irq9(interrupts::CpuKind::Arm9);
    interrupts::InterruptController irq7(interrupts::CpuKind::Arm7);
    DmaController dma9(interrupts::CpuKind::Arm9, bus9, irq9);
    DmaController dma7(interrupts::CpuKind::Arm7, bus7, irq7);

    mem.arm9_mmio().register_device({kDmaBaseAddress, kDmaFillEndAddress, &dma9});
    mem.arm7_mmio().register_device({kDmaBaseAddress, kDmaChannelEndAddress, &dma7});

    // Use ARM9 DMA0FILL (0x040000E0) via Arm9MemoryBus to fill Main RAM (0x02001000).
    bus9.write32(0x040000E0U, 0x11223344U);
    bus9.write32(0x040000B0U, 0x040000E0U);
    bus9.write32(0x040000B4U, 0x02001000U);
    bus9.write32(0x040000B8U, 0xC5000004U);  // En|IRQ|32bit|Src=Fixed|cnt=4
    for (Address offset = 0U; offset < 16U; offset += 4U) {
        EXPECT_EQ(bus9.read32(0x02001000U + offset), 0x11223344U);
    }
    EXPECT_EQ(irq9.if_flags(), interrupts::irq_bit_mask(interrupts::IrqSource::Dma0));

    // ARM7 DMA3 copies Main RAM block into ARM7 WRAM (0x03800100) and signals Dma3 IRQ.
    bus7.write32(0x040000D4U, 0x02001000U);
    bus7.write32(0x040000D8U, 0x03800100U);
    bus7.write32(0x040000DCU, 0xC4000004U);
    for (Address offset = 0U; offset < 16U; offset += 4U) {
        EXPECT_EQ(bus7.read32(0x03800100U + offset), 0x11223344U);
    }
    EXPECT_EQ(irq7.if_flags(), interrupts::irq_bit_mask(interrupts::IrqSource::Dma3));
}

}  // namespace
}  // namespace elmasds::hardware::dma
