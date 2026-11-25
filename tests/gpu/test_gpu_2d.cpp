#include "core/emulator.hpp"
#include "gpu/engines_2d/color_effects.hpp"
#include "gpu/gpu.hpp"

#include <gtest/gtest.h>

namespace elmasds::gpu {
namespace {

TEST(GpuTimingTest, AdvancesVCountAndFiresHBlankVBlankAndVCountIrqs) {
    core::Emulator emu(core::EmulatorConfig{.log_level = LogLevel::Warning});
    auto& bus9 = emu.memory().arm9_mmio();
    auto& bus7 = emu.memory().arm7_mmio();

    // Enable VBlank (bit 3), HBlank (bit 4), VCount (bit 5) IRQs and set LYC = 5 on ARM9 & ARM7.
    const u16 dispstat_cfg = static_cast<u16>((5U << 8U) | 0x0038U);
    bus9.write16(kRegDispStat, dispstat_cfg);
    bus7.write16(kRegDispStat, dispstat_cfg);

    EXPECT_EQ(bus9.read16(kRegVCount), 0U);
    // Advance to HBlank start of scanline 0 (3072 ticks).
    emu.step_ticks(kHDrawTicks);
    EXPECT_NE(bus9.read16(kRegDispStat) & 0x0002U, 0U);  // HBlank flag active.

    // Advance to end of scanline 0 (1188 ticks -> 4260 total).
    emu.step_ticks(kHBlankTicks);
    EXPECT_EQ(bus9.read16(kRegVCount), 1U);
    EXPECT_EQ(bus9.read16(kRegDispStat) & 0x0002U, 0U);

    // Advance to scanline 5 to trigger VCount match.
    emu.step_ticks(kScanlineTicks * 4ULL);
    EXPECT_EQ(bus9.read16(kRegVCount), 5U);
    EXPECT_NE(bus9.read16(kRegDispStat) & 0x0004U, 0U);

    // Advance to scanline 192 (VBlank start) and verify frame_count increments.
    emu.step_ticks(kScanlineTicks * (192ULL - 5ULL));
    EXPECT_EQ(bus9.read16(kRegVCount), 192U);
    EXPECT_NE(bus9.read16(kRegDispStat) & 0x0001U, 0U);
    EXPECT_EQ(emu.frame_count(), 1ULL);
}

TEST(Gpu2DRendererTest, RendersVramDisplayModeAndPowCnt1DisplaySwap) {
    core::Emulator emu(core::EmulatorConfig{.log_level = LogLevel::Warning});
    auto& vram = emu.memory().vram();

    // Enable VRAM Bank A in LCDC mode (0x80) and write bright red (0x001F) at pixel (0, 0).
    vram.write_vram_cnt(memory::VramBankId::A, 0x80U);
    vram.write_arm9_16(0x06800000U, 0x001FU);

    // Engine A DISPCNT: Display Mode 2 (VRAM display, 2 << 16), Bank A (0 << 18).
    emu.gpu().regs_a().write32(0x04000000U, 2U << 16U);
    // Engine B DISPCNT: Display Mode 1 (normal), backdrop palette = pure blue (0x7C00).
    emu.gpu().regs_b().write32(0x04001000U, 1U << 16U);
    emu.memory().palette().write16(0x0400U, 0x7C00U);

    // Default POWCNT1 has bit 15 = 1 (Engine A on top screen, Engine B on bottom screen).
    emu.step_ticks(kScanlineTicks);
    EXPECT_EQ(emu.top_screen()[0U], bgr555_to_argb8888(0x001FU));
    EXPECT_EQ(emu.bottom_screen()[0U], bgr555_to_argb8888(0x7C00U));

    // Clear POWCNT1 bit 15 -> swap screens (Engine A on bottom, Engine B on top).
    emu.reset();
    vram.write_vram_cnt(memory::VramBankId::A, 0x80U);
    vram.write_arm9_16(0x06800000U, 0x001FU);
    emu.gpu().regs_a().write32(0x04000000U, 2U << 16U);
    emu.gpu().regs_b().write32(0x04001000U, 1U << 16U);
    emu.memory().palette().write16(0x0400U, 0x7C00U);
    emu.gpu().powcnt1().write16(kPowCnt1MmioStart, 0x020FU);

    emu.step_ticks(kScanlineTicks);
    EXPECT_EQ(emu.top_screen()[0U], bgr555_to_argb8888(0x7C00U));
    EXPECT_EQ(emu.bottom_screen()[0U], bgr555_to_argb8888(0x001FU));
}

TEST(Gpu2DRendererTest, RendersTextBgSpritesWindowsAndAlphaBlending) {
    core::Emulator emu(core::EmulatorConfig{.log_level = LogLevel::Warning});
    auto& vram = emu.memory().vram();
    auto& pal = emu.memory().palette();
    auto& oam = emu.memory().oam();
    auto& regs_a = emu.gpu().regs_a();

    // Map VRAM Bank A to Engine A BG (0x81) and Bank B to Engine A OBJ (0x82).
    vram.write_vram_cnt(memory::VramBankId::A, 0x81U);
    vram.write_vram_cnt(memory::VramBankId::B, 0x82U);

    // BG palette 1 = pure green (0x03E0); OBJ palette 1 = pure red (0x001F).
    pal.write16(0x0002U, 0x03E0U);
    pal.write16(0x0202U, 0x001FU);

    // BG0: char_base = 0 (0x06000000), screen_base = 1 (0x06000800), priority = 1.
    regs_a.write16(0x04000008U, static_cast<u16>((1U << 8U) | 1U));
    // Tile 1 at 0x06000020: 4bpp row 0 filled with color index 1 (0x1111).
    vram.write_arm9_16(0x06000020U, 0x1111U);
    vram.write_arm9_16(0x06000022U, 0x1111U);
    // Tilemap entry (0, 0) at 0x06000800 references tile 1.
    vram.write_arm9_16(0x06000800U, 0x0001U);

    // OBJ 0 at (0, 0), 8x8 4bpp, tile 0, priority 0, semi-transparent mode (1 << 10).
    vram.write_arm9_16(0x06400000U, 0x1111U);
    oam.write16(0x0000U, static_cast<u16>(1U << 10U));  // Y=0, mode=1 (semi-transparent)
    oam.write16(0x0002U, 0x0000U);                      // X=0, 8x8
    oam.write16(0x0004U, 0x0000U);                      // tile=0, prio=0

    // Configure 50/50 alpha blend between OBJ (1st target) and BG0 (2nd target: bit 8).
    regs_a.write16(0x04000050U, static_cast<u16>((1U << 4U) | (1U << 8U)));
    regs_a.write16(0x04000052U, 0x0808U);  // EVA=8, EVB=8

    // Enable Display Mode 1, BG0 (bit 8), and OBJ (bit 12).
    regs_a.write32(0x04000000U, (1U << 16U) | (1U << 8U) | (1U << 12U));

    emu.step_ticks(kScanlineTicks);
    const u32 expected_blend =
        blend_argb8888(bgr555_to_argb8888(0x001FU), bgr555_to_argb8888(0x03E0U), 8U, 8U);
    EXPECT_EQ(emu.top_screen()[0U], expected_blend);
    // At x=6 (outside the first 4 pixels of OBJ tile 0, inside BG tile 1), pure green is shown.
    EXPECT_EQ(emu.top_screen()[6U], bgr555_to_argb8888(0x03E0U));
}

}  // namespace
}  // namespace elmasds::gpu
