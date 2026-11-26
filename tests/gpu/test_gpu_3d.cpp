#include "core/emulator.hpp"
#include "gpu/engine_3d/engine_3d.hpp"
#include "gpu/engines_2d/color_effects.hpp"

#include <gtest/gtest.h>

namespace elmasds::gpu {
namespace {

TEST(Gpu3DTest, ExecutesMatrixStackCommandsAndExposesClipMatrixResults) {
    core::Emulator emu;
    emu.reset();

    auto& bus = emu.arm9().context().bus;

    // Set MTX_MODE = 1 (Position matrix), load identity, translate by (2.0, 3.0, 4.0).
    bus.write32(0x04000440U, 1U);            // MTX_MODE = Position
    bus.write32(0x04000454U, 0U);            // MTX_IDENTITY
    bus.write32(0x04000470U, 2U * 0x1000U);  // MTX_TRANS X = 2.0
    bus.write32(0x04000470U, 3U * 0x1000U);  // MTX_TRANS Y = 3.0
    bus.write32(0x04000470U, 4U * 0x1000U);  // MTX_TRANS Z = 4.0

    EXPECT_EQ(bus.read32(kRegClipMtxResultStart + 12U * 4U), 2U * 0x1000U);
    EXPECT_EQ(bus.read32(kRegClipMtxResultStart + 13U * 4U), 3U * 0x1000U);
    EXPECT_EQ(bus.read32(kRegClipMtxResultStart + 14U * 4U), 4U * 0x1000U);

    // Push matrix, scale by 2.0, verify stack pointer = 1 in GXSTAT, then pop back.
    bus.write32(0x04000444U, 0U);  // MTX_PUSH
    EXPECT_EQ((bus.read32(kRegGxStat) >> 8U) & 0x1FU, 1U);

    bus.write32(0x0400046CU, 2U * 0x1000U);
    bus.write32(0x0400046CU, 2U * 0x1000U);
    bus.write32(0x0400046CU, 2U * 0x1000U);
    EXPECT_EQ(bus.read32(kRegClipMtxResultStart + 0U), 2U * 0x1000U);

    bus.write32(0x04000448U, 1U);  // MTX_POP(1)
    EXPECT_EQ((bus.read32(kRegGxStat) >> 8U) & 0x1FU, 0U);
    EXPECT_EQ(bus.read32(kRegClipMtxResultStart + 0U), 1U * 0x1000U);
}

TEST(Gpu3DTest, Rasterizes3DTriangleIntoEngineABg0OnVBlankSwapBuffers) {
    core::Emulator emu;
    emu.reset();

    auto& bus = emu.arm9().context().bus;

    // Enable Engine A Display Mode 1 (normal), BG0 enabled (bit 8), BG0 in 3D mode (bit 3).
    bus.write16(0x05000000U, 0x03E0U);  // Green backdrop to verify transparent texels fall back cleanly.
    bus.write32(kEngineABase + 0x00U, (1U << 16U) | (1U << 8U) | (1U << 3U));

    // Configure viewport (0, 0, 255, 191) and identity matrices.
    bus.write32(0x04000580U, (191U << 24U) | (255U << 16U) | 0U);  // VIEWPORT
    bus.write32(0x04000440U, 0U);                                   // MTX_MODE = Projection
    bus.write32(0x04000454U, 0U);                                   // MTX_IDENTITY
    bus.write32(0x04000440U, 1U);                                   // MTX_MODE = Position
    bus.write32(0x04000454U, 0U);                                   // MTX_IDENTITY

    // POLY_ATTR: front + back surfaces visible, alpha = 31.
    bus.write32(0x040004A4U, (31U << 16U) | (1U << 7U) | (1U << 6U));
    // BEGIN_VTXS: Triangles (0)
    bus.write32(0x04000500U, 0U);
    // COLOR: Pure Red (R=31, G=0, B=0 -> 0x001F)
    bus.write32(0x04000480U, 0x001FU);

    // Emit 3 vertices covering the center of the screen in 4.12 fixed point:
    // V0 = (-0.5, -0.5, 0), V1 = (0.5, -0.5, 0), V2 = (0.0, 0.5, 0)
    bus.write32(0x0400048CU, (0xF800U << 16U) | 0xF800U);
    bus.write32(0x0400048CU, 0x0000U);
    bus.write32(0x0400048CU, (0xF800U << 16U) | 0x0800U);
    bus.write32(0x0400048CU, 0x0000U);
    bus.write32(0x0400048CU, (0x0800U << 16U) | 0x0000U);
    bus.write32(0x0400048CU, 0x0000U);

    // END_VTXS and SWAP_BUFFERS
    bus.write32(0x04000504U, 0U);
    bus.write32(0x04000540U, 0U);

    // Run 2 frames: Frame 1 commits SWAP_BUFFERS at VBlank, Frame 2 composites 3D BG0 onto Top.
    emu.run_frame();
    emu.run_frame();

    const u32 center_pixel = emu.top_screen()[96U * kScreenWidth + 128U];
    EXPECT_EQ(center_pixel, bgr555_to_argb8888(0x001FU));
}

TEST(Gpu3DTest, DecodesCompressed4x4TextureFormat5WithBlendAndTransparentModes) {
    core::Emulator emu;
    emu.reset();
    auto& bus = emu.arm9().context().bus;
    auto& vram = emu.memory().vram();

    // Map Bank A as Texture Slot 0 (MST=3, OFS=0), Bank B as Slot 1 (MST=3, OFS=1), Bank E as TexPal (MST=3).
    vram.write_vram_cnt(memory::VramBankId::A, 0x83U);
    vram.write_vram_cnt(memory::VramBankId::B, 0x8BU);
    vram.write_vram_cnt(memory::VramBankId::E, 0x83U);

    // 8x8 Format 5 texture at base=0: row 0 block(0,0) byte = 0b11_10_01_00 (codes 0,1,2,3 across u=0..3).
    vram.mutable_bank_data(memory::VramBankId::A)[0U] = 0xE4U;
    // Block 1 (u=4..7, v=0): byte at offset +4 = 0xFF (code 3 transparent in mode 0).
    vram.mutable_bank_data(memory::VramBankId::A)[4U] = 0xFFU;
    // Slot 1 palette info: block 0 = Mode 1 (0x4000) | off=0; block 1 = Mode 0 (0x0000) | off=0.
    auto slot1 = vram.mutable_bank_data(memory::VramBankId::B);
    slot1[0U] = 0x00U; slot1[1U] = 0x40U;
    slot1[2U] = 0x00U; slot1[3U] = 0x00U;
    // Palette E: color0 = red (0x001F), color1 = blue (0x7C00).
    auto pal_e = vram.mutable_bank_data(memory::VramBankId::E);
    pal_e[0U] = 0x1FU; pal_e[1U] = 0x00U;
    pal_e[2U] = 0x00U; pal_e[3U] = 0x7CU;

    bus.write16(0x05000000U, 0x03E0U);  // Green backdrop to verify transparent texels fall back cleanly.
    bus.write32(kEngineABase + 0x00U, (1U << 16U) | (1U << 8U) | (1U << 3U));
    bus.write32(kDisp3dCntStart, 1U);  // Texture mapping enable.
    bus.write32(0x04000350U, 0U);     // CLEAR_COLOR alpha=0 so transparent 3D texels reveal 2D backdrop.
    bus.write32(0x04000580U, (191U << 24U) | (255U << 16U));
    bus.write32(0x04000440U, 0U); bus.write32(0x04000454U, 0U);
    bus.write32(0x04000440U, 1U); bus.write32(0x04000454U, 0U);
    bus.write32(0x040004A4U, (31U << 16U) | (1U << 7U) | (1U << 6U));
    bus.write32(0x040004ACU, 0U);  // PLTT_BASE = 0
    // TEXIMAGE_PARAM: Format 5 (5 << 26), 8x8 (width=0, height=0), addr=0.
    bus.write32(0x040004A8U, (5U << 26U));
    bus.write32(0x04000480U, 0x7FFFU);
    bus.write32(0x04000500U, 1U);  // Quads

    // Quad across (-1..1, -1..1) with texcoords (s,t) = (0..8, 0..8) in 4.4 fixed point (<< 4).
    bus.write32(0x04000488U, 0U); bus.write32(0x0400048CU, (0x1000U << 16U) | 0xF000U); bus.write32(0x0400048CU, 0U);
    bus.write32(0x04000488U, (8U << 4)); bus.write32(0x0400048CU, (0x1000U << 16U) | 0x1000U); bus.write32(0x0400048CU, 0U);
    bus.write32(0x04000488U, (8U << 16 << 4) | (8U << 4)); bus.write32(0x0400048CU, (0xF000U << 16U) | 0x1000U); bus.write32(0x0400048CU, 0U);
    bus.write32(0x04000488U, (8U << 16 << 4)); bus.write32(0x0400048CU, (0xF000U << 16U) | 0xF000U); bus.write32(0x0400048CU, 0U);

    bus.write32(0x04000504U, 0U);
    bus.write32(0x04000540U, 0U);
    emu.run_frame();
    emu.run_frame();

    // u=0..3 along top scanline: u=0 (red), u=1 (blue), u=2 (50/50 blend red+blue), u=5 (transparent -> white clear/backdrop).
    EXPECT_EQ(emu.top_screen()[16U], bgr555_to_argb8888(0x001FU));
    EXPECT_EQ(emu.top_screen()[48U], bgr555_to_argb8888(0x7C00U));
    EXPECT_EQ(emu.top_screen()[80U], bgr555_to_argb8888((15U) | (15U << 10U)));
    EXPECT_EQ(emu.top_screen()[176U], bgr555_to_argb8888(0x03E0U));
}

}  // namespace
}  // namespace elmasds::gpu
