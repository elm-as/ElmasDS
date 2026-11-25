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

}  // namespace
}  // namespace elmasds::gpu
