#include "dma_test_rig.hpp"

#include "memory/memory_bus/arm7_memory_bus.hpp"
#include "memory/memory_bus/arm9_memory_bus.hpp"
#include "memory/memory_bus/memory_system.hpp"

#include <array>
#include <gtest/gtest.h>

namespace elmasds::hardware::dma {
namespace {

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

    // HBlank paused while VBlank active, resumes when cleared; Cartridge trigger.
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

TEST(DmaControllerTest, GeometryFifoWaitsForTriggerAndRejectsWrongTrigger) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    arm9.bus.write32(0x02000300U, 0x55556666U);
    arm9.bus.write_log.clear();

    // Channel 2: Enable | GeometryFifo (7 << 27) | 32-bit | count=1 | Repeat=0.
    arm9.dma.write32(0x040000C8U, 0x02000300U);
    arm9.dma.write32(0x040000CCU, 0x02000408U);
    arm9.dma.write32(0x040000D0U, 0xBC000001U);

    // Must remain active with 0 bus writes and untouched destination before GXFIFO trigger.
    EXPECT_TRUE(arm9.dma.is_channel_active(2U));
    EXPECT_TRUE(arm9.bus.write_log.empty());
    EXPECT_EQ(arm9.bus.read32(0x02000408U), 0U);

    // Wrong triggers must be ignored.
    arm9.dma.trigger_vblank();
    arm9.dma.trigger_hblank();
    EXPECT_TRUE(arm9.dma.is_channel_active(2U));
    EXPECT_TRUE(arm9.bus.write_log.empty());
    EXPECT_EQ(arm9.bus.read32(0x02000408U), 0U);

    // Matching GXFIFO trigger executes transfer and clears Enable (Repeat=0).
    arm9.dma.trigger_gxfifo();
    EXPECT_FALSE(arm9.dma.is_channel_active(2U));
    EXPECT_EQ(arm9.bus.write_log.size(), 1U);
    EXPECT_EQ(arm9.bus.read32(0x02000408U), 0x55556666U);
}

TEST(DmaControllerTest, GeometryFifoRepeatTransfersMultipleBlocksAndReloadsDest) {
    DmaHarness arm9(interrupts::CpuKind::Arm9);
    arm9.bus.write32(0x02000500U, 0x11111111U);
    arm9.bus.write32(0x02000504U, 0x22222222U);
    arm9.bus.write32(0x02000700U, 0x33333333U);
    arm9.bus.write32(0x02000704U, 0x44444444U);
    arm9.bus.write_log.clear();

    // Channel 2: GeometryFifo + Repeat=1 + Dest=Inc(0), count=1.
    arm9.dma.write32(0x040000C8U, 0x02000500U);
    arm9.dma.write32(0x040000CCU, 0x02000600U);
    arm9.dma.write32(0x040000D0U, 0xBE000001U);
    EXPECT_TRUE(arm9.dma.is_channel_active(2U));
    EXPECT_TRUE(arm9.bus.write_log.empty());
    EXPECT_EQ(arm9.bus.read32(0x02000600U), 0U);

    arm9.dma.trigger_gxfifo();
    EXPECT_TRUE(arm9.dma.is_channel_active(2U));
    EXPECT_EQ(arm9.bus.read32(0x02000600U), 0x11111111U);
    EXPECT_EQ(arm9.bus.read32(0x02000604U), 0U);

    arm9.dma.trigger(DmaTrigger::GeometryFifo);
    EXPECT_TRUE(arm9.dma.is_channel_active(2U));
    EXPECT_EQ(arm9.bus.read32(0x02000604U), 0x22222222U);

    // Channel 1: GeometryFifo + Repeat=1 + Dest=Inc/Reload(3), count=1.
    arm9.dma.write32(0x040000BCU, 0x02000700U);
    arm9.dma.write32(0x040000C0U, 0x02000800U);
    arm9.dma.write32(0x040000C4U, 0xBE600001U);
    arm9.dma.trigger_gxfifo();
    EXPECT_TRUE(arm9.dma.is_channel_active(1U));
    EXPECT_EQ(arm9.bus.read32(0x02000800U), 0x33333333U);

    arm9.dma.trigger_gxfifo();
    EXPECT_TRUE(arm9.dma.is_channel_active(1U));
    EXPECT_EQ(arm9.bus.read32(0x02000800U), 0x44444444U);
}

TEST(DmaControllerTest, ReentrantDmaWritesToMmioAreBoundedAndPreserveCount) {
    memory::MemorySystem mem;
    memory::Arm9MemoryBus bus9(mem);
    interrupts::InterruptController irq9(interrupts::CpuKind::Arm9);
    DmaController dma9(interrupts::CpuKind::Arm9, bus9, irq9);
    mem.arm9_mmio().register_device({kDmaBaseAddress, kDmaFillEndAddress, &dma9});

    // Self-modifying DMA0 writes 0 then Enable=1|Immediate|count=0x100 to its own DMA0CNT.
    bus9.write32(0x02000100U, 0x00000000U);
    bus9.write32(0x02000104U, 0x84000100U);
    bus9.write32(0x040000B0U, 0x02000100U);
    bus9.write32(0x040000B4U, 0x040000B8U);
    bus9.write32(0x040000B8U, 0x84400002U);  // En|32bit|Dst=Fixed|Src=Inc|cnt=2
    EXPECT_FALSE(dma9.is_channel_active(0U));
    EXPECT_EQ(dma9.channel(0U).internal_sad, 0x02000108U);

    // Cross-channel cycle DMA0 -> DMA1 -> DMA0: in_progress_ bounds depth and keeps DMA0 count.
    bus9.write32(0x02000200U, 0x84400002U);  // Triggers DMA1 Immediate, count=2
    bus9.write32(0x02000204U, 0x00000000U);
    bus9.write32(0x02000300U, 0x00000000U);
    bus9.write32(0x02000304U, 0x84000040U);  // Re-latches DMA0 count=0x40 (blocked by guard)
    bus9.write32(0x040000BCU, 0x02000300U);
    bus9.write32(0x040000C0U, 0x040000B8U);  // DMA1 -> DMA0CNT (Fixed dst)
    bus9.write32(0x040000C4U, 0x04400000U);  // 32-bit, Dst=Fixed, Src=Inc, disabled
    bus9.write32(0x040000B0U, 0x02000200U);
    bus9.write32(0x040000B4U, 0x040000C4U);  // DMA0 -> DMA1CNT (Fixed dst)
    bus9.write32(0x040000B8U, 0x84400002U);  // En|32bit|Dst=Fixed|Src=Inc|cnt=2

    EXPECT_FALSE(dma9.is_channel_active(0U));
    EXPECT_FALSE(dma9.is_channel_active(1U));
    EXPECT_EQ(dma9.channel(0U).internal_sad, 0x02000208U);
    EXPECT_EQ(dma9.channel(1U).internal_sad, 0x02000308U);
}

TEST(DmaControllerTest, Arm7DmaBiosReadRespectsCurrentPcProtection) {
    memory::MemorySystem mem;
    memory::Arm7MemoryBus bus7(mem);
    interrupts::InterruptController irq7(interrupts::CpuKind::Arm7);
    DmaController dma7(interrupts::CpuKind::Arm7, bus7, irq7);
    mem.arm7_mmio().register_device({kDmaBaseAddress, kDmaChannelEndAddress, &dma7});

    const std::array<u8, 4> bios_pattern{0xEFU, 0xBEU, 0xADU, 0xDEU};
    mem.bios7().install_hle_stub(bios_pattern);
    ASSERT_EQ(mem.bios7().read32(0U, 0U), 0xDEADBEEFU);

    // Executing from Main RAM (0x02000000) must deny DMA7 reads from ARM7 BIOS and return 0.
    bus7.write32(0x03800200U, 0x11111111U);
    bus7.set_current_pc(0x02000000U);
    bus7.write32(0x040000D4U, 0x00000000U);
    bus7.write32(0x040000D8U, 0x03800200U);
    bus7.write32(0x040000DCU, 0x84000001U);  // En | 32-bit | Immediate | cnt=1
    EXPECT_EQ(bus7.read32(0x03800200U), 0U);
}

}  // namespace
}  // namespace elmasds::hardware::dma
