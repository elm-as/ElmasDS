#include "cpu/arm7/arm7_tdmi.hpp"
#include "cpu/arm9/arm946_es.hpp"
#include "memory/memory_bus/arm7_memory_bus.hpp"
#include "memory/memory_bus/arm9_memory_bus.hpp"
#include "memory/memory_bus/memory_system.hpp"

#include <gtest/gtest.h>

namespace elmasds::memory {
namespace {

class StubMmioDevice final : public MmioDevice {
public:
    u8 read8(Address /*address*/) override { return static_cast<u8>(value & 0xFFU); }
    u16 read16(Address /*address*/) override { return static_cast<u16>(value & 0xFFFFU); }
    u32 read32(Address /*address*/) override { return value; }
    void write8(Address /*address*/, u8 v) override { value = v; }
    void write16(Address /*address*/, u16 v) override { value = v; }
    void write32(Address /*address*/, u32 v) override { value = v; }

    u32 value = 0;
};

TEST(VramControllerTest, MapsLcdcEngineAndArm7WramWithOverlappingOrSemantics) {
    MemorySystem sys;
    Arm9MemoryBus bus9(sys);
    Arm7MemoryBus bus7(sys);

    // Map Bank A as LCDC (0x80 -> MST=0, enable=1) and Bank B as LCDC (0x80).
    bus9.write8(0x04000240U, 0x80U);
    bus9.write8(0x04000241U, 0x80U);
    bus9.write32(0x06800000U, 0x0F0F0F0FU);
    bus9.write32(0x06820000U, 0xF0F00000U);
    // ARM9 8-bit writes to VRAM are ignored.
    bus9.write8(0x06800000U, 0xFFU);
    EXPECT_EQ(bus9.read32(0x06800000U), 0x0F0F0F0FU);

    // Remap both Bank A (0x81: Eng A BG ofs=0) and Bank B (0x81: Eng A BG ofs=0) to 0x06000000.
    // Reading overlapping banks yields bitwise OR: 0x0F0F0F0F | 0xF0F00000 = 0xFFFF0F0F.
    bus9.write16(0x04000240U, 0x8181U);
    EXPECT_EQ(bus9.read32(0x06000000U), 0xFFFF0F0FU);

    // Map Bank C to ARM7 VRAM-WRAM at 0x06000000 (0x82: MST=2, ofs=0, enable=1).
    bus9.write8(0x04000242U, 0x82U);
    EXPECT_EQ(bus7.read8(0x04000240U), 0x01U);  // VRAMSTAT bit 0 set.
    bus7.write8(0x06000010U, 0x5AU);            // ARM7 8-bit write succeeds in VRAM-WRAM.
    EXPECT_EQ(bus7.read8(0x06000010U), 0x5AU);
}

TEST(MemoryBusTest, RoutesArm7WramFallbackAndMmioDevices) {
    MemorySystem sys;
    Arm9MemoryBus bus9(sys);
    Arm7MemoryBus bus7(sys);

    StubMmioDevice custom_mmio;
    sys.arm9_mmio().register_device(MmioRegistration{0x04001000U, 0x04001003U, &custom_mmio});
    bus9.write32(0x04001000U, 0x13572468U);
    EXPECT_EQ(bus9.read32(0x04001000U), 0x13572468U);

    // When WRAMCNT == 0, ARM7 0x03000000 aliases ARM7 private 64 KiB WRAM at 0x03800000.
    EXPECT_EQ(bus7.read8(0x04000241U), 0U);
    bus7.write32(0x03800100U, 0xAABBCCDDU);
    EXPECT_EQ(bus7.read32(0x03000100U), 0xAABBCCDDU);
}

TEST(MemoryBusIntegrationTest, Arm9AndArm7CommunicateThroughSharedWramAndMainRam) {
    MemorySystem sys;
    Arm9MemoryBus bus9(sys);
    Arm7MemoryBus bus7(sys);

    cpu::Arm946Es arm9(bus9);
    cpu::Arm7Tdmi arm7(bus7);
    arm9.reset(0x02000000U, false);
    arm7.reset(0x03800000U);

    // ARM9 program at Main RAM 0x02000000:
    //   MOV R0, #0x04000000       (E3A00301)
    //   MOV R1, #1                (E3A01001)
    //   STRB R1, [R0, #0x247]     (E5C01247) -> WRAMCNT = 1 (1st half -> ARM7, 2nd -> ARM9)
    //   MOV R2, #0x03000000       (E3A02403)
    //   MOV R3, #0x42             (E3A03042)
    //   STR R3, [R2, #0x10]       (E5823010) -> writes 0x42 into Shared WRAM 2nd half (+0x10)
    //   MOV R1, #2                (E3A01002)
    //   STRB R1, [R0, #0x247]     (E5C01247) -> WRAMCNT = 2 (2nd half now visible to ARM7!)
    bus9.write32(0x02000000U, 0xE3A00301U);
    bus9.write32(0x02000004U, 0xE3A01001U);
    bus9.write32(0x02000008U, 0xE5C01247U);
    bus9.write32(0x0200000CU, 0xE3A02403U);
    bus9.write32(0x02000010U, 0xE3A03042U);
    bus9.write32(0x02000014U, 0xE5823010U);
    bus9.write32(0x02000018U, 0xE3A01002U);
    bus9.write32(0x0200001CU, 0xE5C01247U);

    for (int i = 0; i < 8; ++i) {
        arm9.step();
    }
    EXPECT_EQ(sys.shared_wram().wram_cnt(), 2U);
    EXPECT_GT(bus9.accumulated_waitstates(), 0ULL);

    // ARM7 program at ARM7-WRAM 0x03800000:
    //   MOV R0, #0x03000000       (E3A00403)
    //   LDR R1, [R0, #0x10]       (E5901010) -> reads 0x42 from Shared WRAM 2nd half
    //   ADD R1, R1, #8            (E2811008) -> 0x4A
    //   MOV R2, #0x02000000       (E3A02302? Wait: #0x02000000 is 0x02 ror 8 -> rot4=4 => E3A02402)
    //   STR R1, [R2, #0x100]      (E5821100) -> writes 0x4A to Main RAM 0x02000100
    bus7.write32(0x03800000U, 0xE3A00403U);
    bus7.write32(0x03800004U, 0xE5901010U);
    bus7.write32(0x03800008U, 0xE2811008U);
    bus7.write32(0x0380000CU, 0xE3A02402U);
    bus7.write32(0x03800010U, 0xE5821100U);

    for (int i = 0; i < 5; ++i) {
        arm7.step();
    }
    EXPECT_EQ(bus9.read32(0x02000100U), 0x4AU);
    EXPECT_GT(bus7.accumulated_waitstates(), 0ULL);
}

}  // namespace
}  // namespace elmasds::memory
