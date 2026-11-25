#include "hardware/cartridge/cartridge_controller.hpp"
#include "hardware/dma/dma_controller.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "memory/memory_bus/arm9_memory_bus.hpp"
#include "memory/memory_bus/memory_system.hpp"
#include "storage/rom_loader/nds_rom.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace elmasds::hardware::cartridge {
namespace {

using interrupts::CpuKind;
using interrupts::InterruptController;
using interrupts::IrqSource;

void write_le16(std::vector<u8>& buf, std::size_t off, u16 val) {
    buf[off] = static_cast<u8>(val & 0xFFU);
    buf[off + 1U] = static_cast<u8>((val >> 8U) & 0xFFU);
}

void write_le32(std::vector<u8>& buf, std::size_t off, u32 val) {
    buf[off] = static_cast<u8>(val & 0xFFU);
    buf[off + 1U] = static_cast<u8>((val >> 8U) & 0xFFU);
    buf[off + 2U] = static_cast<u8>((val >> 16U) & 0xFFU);
    buf[off + 3U] = static_cast<u8>((val >> 24U) & 0xFFU);
}

storage::rom_loader::NdsRom make_test_rom() {
    std::vector<u8> bytes(0x9000U, u8{0});
    write_le32(bytes, 0x020U, 0x4000U);
    write_le32(bytes, 0x024U, 0x02000000U);
    write_le32(bytes, 0x028U, 0x02000000U);
    write_le32(bytes, 0x02CU, 0x40U);
    write_le32(bytes, 0x030U, 0x8000U);
    write_le32(bytes, 0x034U, 0x03800000U);
    write_le32(bytes, 0x038U, 0x03800000U);
    write_le32(bytes, 0x03CU, 0x40U);

    // Payload words at 0x8200 for 0xB7 read test.
    write_le32(bytes, 0x8200U, 0x11223344U);
    write_le32(bytes, 0x8204U, 0x55667788U);

    const u16 logo_crc = storage::rom_loader::compute_nds_crc16(
        std::span<const u8>(bytes).subspan(storage::rom_loader::kNdsLogoOffset,
                                           storage::rom_loader::kNdsLogoSize));
    write_le16(bytes, 0x15CU, logo_crc);
    const u16 hdr_crc =
        storage::rom_loader::compute_nds_crc16(std::span<const u8>(bytes).subspan(0U, 0x15EU));
    write_le16(bytes, 0x15EU, hdr_crc);

    auto loaded = storage::rom_loader::NdsRom::load_from_bytes(bytes);
    return std::move(loaded.value());
}

TEST(CartridgeControllerTest, ChipIdAndRomDataTransferWithDmaAndIrq) {
    memory::MemorySystem memory;
    memory::Arm9MemoryBus arm9_bus(memory);
    InterruptController arm9_irq(CpuKind::Arm9);
    dma::DmaController arm9_dma(CpuKind::Arm9, arm9_bus, arm9_irq);
    CartridgeController cart(CartridgeConfig{&arm9_irq, nullptr, &arm9_dma, nullptr});
    memory.arm9_mmio().register_device({kCartMmioStart, kCartMmioEnd, &cart.arm9_mmio()});
    memory.arm9_mmio().register_device({kRegRomData, kRegRomDataEnd, &cart.arm9_mmio()});
    memory.arm9_mmio().register_device({kRegExMemCnt, kRegExMemCntEnd, &cart.arm9_mmio()});

    cart.insert_rom(make_test_rom());
    EXPECT_EQ(cart.slot_owner(), CpuKind::Arm9);

    // Enable slot + transfer IRQ (bit 15 | bit 14).
    cart.arm9_mmio().write16(kRegAuxSpiCnt, static_cast<u16>((1U << 15U) | (1U << 14U)));

    // Command 0xB8 (Get Chip ID), block size 7 (4 bytes).
    cart.arm9_mmio().write32(kRegRomCmdStart, 0x000000B8U);
    cart.arm9_mmio().write32(kRegRomCmdStart + 4U, 0x00000000U);
    cart.arm9_mmio().write32(kRegRomCtrl, (1U << 31U) | (7U << 24U));
    EXPECT_EQ(cart.arm9_mmio().read32(kRegRomData), cart.rom()->chip_id());
    EXPECT_NE(arm9_irq.if_flags() & interrupts::irq_bit_mask(IrqSource::CartridgeTransfer), 0U);

    // Clear IRQ and configure ARM9 DMA0 for Cartridge trigger (timing 5 << 27), 32-bit, Fixed src.
    arm9_irq.write32(InterruptController::kRegIf,
                     interrupts::irq_bit_mask(IrqSource::CartridgeTransfer));
    constexpr Address kDst = 0x02010000U;
    arm9_dma.write32(0x040000B0U, kRegRomData);
    arm9_dma.write32(0x040000B4U, kDst);
    arm9_dma.write32(0x040000B8U, (1U << 31U) | (5U << 27U) | (1U << 26U) | (2U << 23U) | 128U);

    // Command 0xB7 (Read Main Data) at offset 0x00008200, block size 1 (512 bytes = 128 words).
    cart.arm9_mmio().write8(kRegRomCmdStart + 0U, 0xB7U);
    cart.arm9_mmio().write8(kRegRomCmdStart + 1U, 0x00U);
    cart.arm9_mmio().write8(kRegRomCmdStart + 2U, 0x00U);
    cart.arm9_mmio().write8(kRegRomCmdStart + 3U, 0x82U);
    cart.arm9_mmio().write8(kRegRomCmdStart + 4U, 0x00U);
    cart.arm9_mmio().write32(kRegRomCtrl, (1U << 31U) | (1U << 24U));

    EXPECT_EQ(arm9_bus.read32(kDst + 0U), 0x11223344U);
    EXPECT_EQ(arm9_bus.read32(kDst + 4U), 0x55667788U);
    EXPECT_NE(arm9_irq.if_flags() & interrupts::irq_bit_mask(IrqSource::CartridgeTransfer), 0U);
}

TEST(CartridgeControllerTest, ExMemCntSwitchesSlotOwnershipBetweenArm9AndArm7) {
    CartridgeController cart;
    cart.insert_rom(make_test_rom());

    // Hand Slot-1 ownership to ARM7 (bit 11 = 1).
    cart.arm9_mmio().write16(kRegExMemCnt, static_cast<u16>(1U << 11U));
    EXPECT_EQ(cart.slot_owner(), CpuKind::Arm7);

    // ARM7 can now read Chip ID; ARM9 writes are ignored.
    cart.arm7_mmio().write16(kRegAuxSpiCnt, static_cast<u16>(1U << 15U));
    cart.arm7_mmio().write32(kRegRomCmdStart, 0x00000090U);
    cart.arm7_mmio().write32(kRegRomCtrl, (1U << 31U) | (7U << 24U));
    EXPECT_EQ(cart.arm7_mmio().read32(kRegRomData), cart.rom()->chip_id());
    EXPECT_EQ(cart.arm9_mmio().read32(kRegRomCtrl), 0U);
}

}  // namespace
}  // namespace elmasds::hardware::cartridge
