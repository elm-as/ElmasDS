#include "core/emulator.hpp"
#include "input/input_mapping.hpp"
#include "input/keypad_mmio.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace elmasds::input {
namespace {

TEST(KeypadControllerTest, TracksActiveLowButtonsAndFiresKeyCntIrqInOrAndAndModes) {
    core::Emulator emu;
    emu.reset();

    auto& arm9_bus = emu.arm9().context().bus;
    auto& arm7_bus = emu.arm7().context().bus;

    EXPECT_EQ(arm9_bus.read16(kRegKeyInput), 0x03FFU);
    EXPECT_EQ(arm7_bus.read16(kRegExtKeyIn), 0x007FU);

    // Enable Keypad IRQ on ARM9 in OR mode for A (bit 0) or B (bit 1): bit 14 = 1, bit 15 = 0.
    arm9_bus.write16(kRegKeyCnt, 0x4003U);
    emu.set_button_pressed(DsButton::A, true);
    EXPECT_EQ(arm9_bus.read16(kRegKeyInput), 0x03FEU);
    EXPECT_NE(
        arm9_bus.read32(hardware::interrupts::InterruptController::kRegIf) &
            hardware::interrupts::irq_bit_mask(hardware::interrupts::IrqSource::Keypad),
        0U);

    // Clear IF, switch ARM9 KEYCNT to AND mode for A + B (bit 14 = 1, bit 15 = 1).
    arm9_bus.write32(hardware::interrupts::InterruptController::kRegIf,
                     hardware::interrupts::irq_bit_mask(hardware::interrupts::IrqSource::Keypad));
    arm9_bus.write16(kRegKeyCnt, 0xC003U);
    EXPECT_EQ(
        arm9_bus.read32(hardware::interrupts::InterruptController::kRegIf) &
            hardware::interrupts::irq_bit_mask(hardware::interrupts::IrqSource::Keypad),
        0U);

    emu.set_button_pressed(DsButton::B, true);
    EXPECT_NE(
        arm9_bus.read32(hardware::interrupts::InterruptController::kRegIf) &
            hardware::interrupts::irq_bit_mask(hardware::interrupts::IrqSource::Keypad),
        0U);

    // Extended buttons on ARM7 (X, Y, PenDown).
    emu.set_button_pressed(DsButton::X, true);
    emu.set_touch_pixel(128U, 96U, true);
    const u16 ext = arm7_bus.read16(kRegExtKeyIn);
    EXPECT_EQ(ext & (1U << 0U), 0U);
    EXPECT_EQ(ext & (1U << 6U), 0U);
}

TEST(InputKeyMapTest, SupportsDefaultBindingsConflictDetectionShortcutsAndFilePersistence) {
    InputKeyMap map;
    EXPECT_EQ(map.key_for_button(DsButton::A), HostKey::L);
    EXPECT_EQ(map.key_for_button(DsButton::B), HostKey::K);
    EXPECT_EQ(map.key_for_button(DsButton::X), HostKey::I);
    EXPECT_EQ(map.key_for_button(DsButton::Y), HostKey::J);
    EXPECT_EQ(map.key_for_button(DsButton::L), HostKey::Q);
    EXPECT_EQ(map.key_for_button(DsButton::R), HostKey::E);
    EXPECT_EQ(map.key_for_button(DsButton::Start), HostKey::Enter);
    EXPECT_EQ(map.key_for_button(DsButton::Select), HostKey::RightShift);

    // Reject duplicate binding to a key already used by another DS button.
    EXPECT_TRUE(map.bind_button(DsButton::A, HostKey::K).is_err());
    EXPECT_TRUE(map.bind_button(DsButton::A, HostKey::Z).is_ok());
    EXPECT_EQ(map.button_for_key(HostKey::Z), DsButton::A);

    EXPECT_EQ(InputKeyMap::match_shortcut(HostKey::O, KeyModifiers{true, false, false}),
              EmulatorShortcut::OpenRom);
    EXPECT_EQ(InputKeyMap::match_shortcut(HostKey::Space, KeyModifiers{}),
              EmulatorShortcut::TogglePause);
    EXPECT_EQ(InputKeyMap::match_shortcut(HostKey::F11, KeyModifiers{}),
              EmulatorShortcut::ToggleFullscreen);

    const auto tmp_file = std::filesystem::temp_directory_path() / "elmasds_test_keymap.cfg";
    ASSERT_TRUE(map.save_to_file(tmp_file).is_ok());

    InputKeyMap loaded;
    ASSERT_TRUE(loaded.load_from_file(tmp_file).is_ok());
    EXPECT_EQ(loaded.key_for_button(DsButton::A), HostKey::Z);
    std::filesystem::remove(tmp_file);
}

TEST(HleBiosTest, ExecutesDivSqrtCpuSetCrc16AndLz77WithoutProprietaryBios) {
    core::Emulator emu;
    emu.reset();

    auto& arm9 = emu.arm9();
    auto& regs = arm9.registers();
    auto& bus = arm9.context().bus;

    // Place a Thumb SWI 0x09 (Div), SWI 0x0D (Sqrt), SWI 0x0B (CpuSet), SWI 0x11 (LZ77UnComp)
    // sequence in Main RAM at 0x02000000.
    bus.write16(0x02000000U, 0xDF09U);  // SWI 0x09 (Div)
    bus.write16(0x02000002U, 0xDF0DU);  // SWI 0x0D (Sqrt)
    bus.write16(0x02000004U, 0xDF0BU);  // SWI 0x0B (CpuSet)
    bus.write16(0x02000006U, 0xDF11U);  // SWI 0x11 (LZ77UnCompWram)

    regs.set_cpsr(regs.cpsr().with_thumb(true));
    regs.write(cpu::RegisterIndex::Pc, 0x02000000U);
    regs.write(cpu::RegisterIndex::R0, 100U);
    regs.write(cpu::RegisterIndex::R1, 7U);
    arm9.step();
    EXPECT_EQ(regs.read(cpu::RegisterIndex::R0), 14U);
    EXPECT_EQ(regs.read(cpu::RegisterIndex::R1), 2U);
    EXPECT_EQ(regs.read(cpu::RegisterIndex::Pc), 0x02000002U);

    // SWI 0x0D (Sqrt): sqrt(144) = 12
    regs.write(cpu::RegisterIndex::R0, 144U);
    arm9.step();
    EXPECT_EQ(regs.read(cpu::RegisterIndex::R0), 12U);

    // SWI 0x0B (CpuSet): 32-bit fixed fill of 4 words at 0x02000100 with value at 0x02000080.
    bus.write32(0x02000080U, 0xCAFEBABEU);
    regs.write(cpu::RegisterIndex::R0, 0x02000080U);
    regs.write(cpu::RegisterIndex::R1, 0x02000100U);
    regs.write(cpu::RegisterIndex::R2, (1U << 26U) | (1U << 24U) | 4U);
    arm9.step();
    for (u32 i = 0U; i < 4U; ++i) {
        EXPECT_EQ(bus.read32(0x02000100U + i * 4U), 0xCAFEBABEU);
    }

    // SWI 0x11 (LZ77UnComp): decompress 4 literal bytes "NDS!" to 0x02000200.
    bus.write32(0x02000180U, (4U << 8U) | 0x10U);
    bus.write8(0x02000184U, 0x00U);  // 8 literal flags
    bus.write8(0x02000185U, static_cast<u8>('N'));
    bus.write8(0x02000186U, static_cast<u8>('D'));
    bus.write8(0x02000187U, static_cast<u8>('S'));
    bus.write8(0x02000188U, static_cast<u8>('!'));
    regs.write(cpu::RegisterIndex::R0, 0x02000180U);
    regs.write(cpu::RegisterIndex::R1, 0x02000200U);
    arm9.step();
    EXPECT_EQ(bus.read32(0x02000200U), 0x2153444EU);
}

}  // namespace
}  // namespace elmasds::input
