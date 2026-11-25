#include "core/emulator.hpp"
#include "storage/save_states/save_state.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace elmasds::storage::save_states {
namespace {

TEST(SaveStateTest, RoundTripsCpuRegistersMemoryVramAndRejectsCorruptSnapshots) {
    core::Emulator emu;
    emu.reset();

    emu.arm9().registers().write(cpu::RegisterIndex::R4, 0x12345678U);
    emu.arm7().registers().write(cpu::RegisterIndex::R5, 0x87654321U);
    emu.write_main_ram32(0x02000100U, 0xDEADBEEFU);
    emu.memory().vram().write_vram_cnt(memory::VramBankId::A, 0x80U);
    emu.memory().vram().write_arm9_16(0x06800000U, 0x55AAU);
    emu.set_button_pressed(input::DsButton::Start, true);

    const auto state_res = emu.save_state();
    ASSERT_TRUE(state_res.is_ok());

    // Mutate emulator state after capturing snapshot.
    emu.arm9().registers().write(cpu::RegisterIndex::R4, 0U);
    emu.arm7().registers().write(cpu::RegisterIndex::R5, 0U);
    emu.write_main_ram32(0x02000100U, 0U);
    emu.memory().vram().write_arm9_16(0x06800000U, 0U);
    emu.set_button_pressed(input::DsButton::Start, false);

    // Restore snapshot and verify original state.
    ASSERT_TRUE(emu.load_state(state_res.value()).is_ok());
    EXPECT_EQ(emu.arm9().registers().read(cpu::RegisterIndex::R4), 0x12345678U);
    EXPECT_EQ(emu.arm7().registers().read(cpu::RegisterIndex::R5), 0x87654321U);
    EXPECT_EQ(emu.read_main_ram32(0x02000100U), 0xDEADBEEFU);
    EXPECT_EQ(emu.memory().vram().read_arm9_16(0x06800000U), 0x55AAU);
    EXPECT_TRUE(emu.keypad().is_button_pressed(input::DsButton::Start));

    // Corrupt magic and verify rejection.
    auto corrupted = state_res.value();
    corrupted[0U] = 'X';
    EXPECT_TRUE(emu.load_state(corrupted).is_err());

    // File persistence round-trip.
    const auto tmp_file = std::filesystem::temp_directory_path() / "elmasds_test_state.ess";
    ASSERT_TRUE(emu.save_state_to_file(tmp_file).is_ok());
    emu.write_main_ram32(0x02000100U, 0x11111111U);
    ASSERT_TRUE(emu.load_state_from_file(tmp_file).is_ok());
    EXPECT_EQ(emu.read_main_ram32(0x02000100U), 0xDEADBEEFU);
    std::filesystem::remove(tmp_file);
}

}  // namespace
}  // namespace elmasds::storage::save_states
