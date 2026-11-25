#include "memory/arm7_wram/arm7_wram.hpp"
#include "memory/bios/bios_memory.hpp"
#include "memory/main_ram/main_ram.hpp"
#include "memory/oam/oam_ram.hpp"
#include "memory/palette/palette_ram.hpp"
#include "memory/shared_wram/shared_wram.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace elmasds::memory {
namespace {

TEST(MemoryRegionsTest, MainRamMirrorsAcrossFourMegabytes) {
    MainRam ram;
    ram.write32(0x02000100U, 0xCAFEBABEU);
    EXPECT_EQ(ram.read32(0x02400100U), 0xCAFEBABEU);
    EXPECT_EQ(ram.read16(0x02800102U), 0xCAFEU);
    EXPECT_EQ(ram.read8(0x02C00100U), 0xBEU);
}

TEST(MemoryRegionsTest, SharedWramRespectsAllFourWramCntModes) {
    SharedWram wram;

    // Mode 0: all 32 KiB to ARM9, nothing to ARM7.
    wram.set_wram_cnt(0U);
    wram.write_arm9_32(0x03000000U, 0x11111111U);
    wram.write_arm9_32(0x03004000U, 0x22222222U);
    EXPECT_EQ(wram.read_arm9_32(0x03000000U), 0x11111111U);
    EXPECT_EQ(wram.read_arm9_32(0x03004000U), 0x22222222U);
    EXPECT_FALSE(wram.arm7_has_mapping());
    EXPECT_EQ(wram.read_arm7_32(0x03000000U), 0U);

    // Mode 1: 1st half -> ARM7, 2nd half -> ARM9.
    wram.set_wram_cnt(1U);
    EXPECT_EQ(wram.read_arm7_32(0x03000000U), 0x11111111U);
    EXPECT_EQ(wram.read_arm9_32(0x03000000U), 0x22222222U);

    // Mode 2: 1st half -> ARM9, 2nd half -> ARM7.
    wram.set_wram_cnt(2U);
    EXPECT_EQ(wram.read_arm9_32(0x03000000U), 0x11111111U);
    EXPECT_EQ(wram.read_arm7_32(0x03000000U), 0x22222222U);

    // Mode 3: nothing to ARM9, all 32 KiB to ARM7.
    wram.set_wram_cnt(3U);
    EXPECT_EQ(wram.read_arm9_32(0x03000000U), 0U);
    wram.write_arm9_32(0x03000000U, 0x99999999U);
    EXPECT_EQ(wram.read_arm7_32(0x03000000U), 0x11111111U);
    EXPECT_EQ(wram.read_arm7_32(0x03004000U), 0x22222222U);
}

TEST(MemoryRegionsTest, PaletteAndOamIgnoreEightBitWritesAndMirror) {
    PaletteRam palette;
    OamRam oam;

    palette.write16(0x05000010U, 0x7FFFU);
    palette.write8(0x05000010U, 0x00U);  // Ignored on NDS.
    EXPECT_EQ(palette.read16(0x05000810U), 0x7FFFU);

    oam.write32(0x07000020U, 0x12345678U);
    oam.write8(0x07000021U, 0xFFU);  // Ignored on NDS.
    EXPECT_EQ(oam.read32(0x07000820U), 0x12345678U);
}

TEST(MemoryRegionsTest, BiosEnforcesReadOnlyAndArm7PcProtection) {
    BiosMemory bios7(BiosMemory::kArm7BiosSize, true);
    std::vector<u8> image(BiosMemory::kArm7BiosSize, 0U);
    image[0x10U] = 0x78U;
    image[0x11U] = 0x56U;
    image[0x12U] = 0x34U;
    image[0x13U] = 0x12U;
    ASSERT_TRUE(bios7.load(image).is_ok());

    EXPECT_EQ(bios7.read32(0x10U, 0x00000100U), 0x12345678U);
    // Protected when PC >= 0x4000.
    EXPECT_EQ(bios7.read32(0x10U, 0x02000000U), 0U);
}

}  // namespace
}  // namespace elmasds::memory
