#include "e2e/arm_program_emitter.hpp"
#include "e2e/e2e_m3_guest.hpp"
#include "e2e/e2e_m3_helpers.hpp"
#include "e2e/emulator_e2e_harness.hpp"
#include "e2e/nds_rom_builder.hpp"
#include "e2e/system_image_builder.hpp"
#include "primitives/fixed_width_types.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <span>
#include <vector>

namespace elmasds::e2e::m3 {
namespace {

constexpr u32 kDoneSlot = 63U;
constexpr u32 kDoneMagic = 0xD04E5A72U;
constexpr TickCount kDefaultSteps = 40'000ULL;

struct FallbackSizeCase {
    std::size_t file_bytes = 12345U;
    u8 sentinel = 0x5AU;
};

struct SpiReadReq {
    std::vector<u8> prefix;
    u32 read_bytes = 1U;
    u32 slot = 0U;
};

void emit_wren(ArmProgramEmitter& emit) {
    static constexpr std::array<u8, 1U> kCmd{0x06U};
    emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = kCmd});
}

void emit_wrdi(ArmProgramEmitter& emit) {
    static constexpr std::array<u8, 1U> kCmd{0x04U};
    emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = kCmd});
}

void emit_rdsr(ArmProgramEmitter& emit, u32 slot) {
    static constexpr std::array<u8, 2U> kCmd{0x05U, 0x00U};
    emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = kCmd, .rx_ram_addr = result_slot_addr(slot)});
}

void emit_raw_tx(ArmProgramEmitter& emit, std::vector<u8> tx) {
    emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = tx});
}

void emit_wren_tx(ArmProgramEmitter& emit, std::vector<u8> tx) {
    emit_wren(emit);
    emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = tx});
}

void emit_spi_read(ArmProgramEmitter& emit, SpiReadReq req) {
    req.prefix.resize(req.prefix.size() + req.read_bytes, 0x00U);
    emit_auxspi_seq(
        emit, AuxSpiSeqSpec{.tx_bytes = req.prefix, .rx_ram_addr = result_slot_addr(req.slot)});
}

template <typename BuildFn>
void run_arm9_guest(EmulatorE2eHarness& harness, BuildFn&& build) {
    ArmProgramEmitter emit(kArm9DefaultBase);
    build(emit);
    emit_store_const_result(emit, kDoneSlot, kDoneMagic);
    emit_halt_loop(emit);
    const NdsRomSpec spec = make_base_rom_spec(emit.words(), make_idle_words());
    boot_rom_and_step(harness, spec, kDefaultSteps);
    ASSERT_EQ(harness.read_main_ram32(result_slot_addr(kDoneSlot)), kDoneMagic);
}

void expect_bytes(const EmulatorE2eHarness& harness,
                  Address base_addr,
                  std::initializer_list<u8> expected) {
    u32 idx = 0U;
    for (const u8 want : expected) {
        EXPECT_EQ(harness.read_main_ram8(base_addr + idx), want) << "idx=" << idx;
        ++idx;
    }
}

u8 addr_byte(u32 addr, u32 shift) {
    return static_cast<u8>((addr >> shift) & 0xFFU);
}

// F27: Save Memory Types (Tier 2 Boundary & Negative)

TEST(Tier2SaveMemoryE2e, WriteAndEraseWithoutWrenAreIgnoredAndRdsrZero) {
    ScopedTempDir dir;
    const std::vector<u8> img = build_save_image(SaveImageSpec{
        .kind = SyntheticSaveKind::Flash256K,
        .fill_byte = 0xFFU,
        .initial_patch = {0x42U},
        .patch_offset = 0x000100U,
    });
    const auto sav_path = dir.write_file("no_wren.sav", img);

    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(sav_path, static_cast<u32>(M3SaveTypeHint::None)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_rdsr(emit, 0U);
        emit_raw_tx(emit, {0x02U, 0x00U, 0x00U, 0x10U, 0xABU});
        emit_raw_tx(emit, {0xDBU, 0x00U, 0x01U, 0x00U});
        emit_raw_tx(emit, {0xD8U, 0x00U, 0x00U, 0x00U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x00U, 0x10U}, 1U, 2U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x01U, 0x00U}, 1U, 4U});
    });

    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U) & 0x02U, 0U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 4U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(4U) + 4U), 0x42U);
}

TEST(Tier2SaveMemoryE2e, WrdiClearsWrenAndBlocksSubsequentWrite) {
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    const auto sav = dir.path() / "wrdi.sav";
    ASSERT_TRUE(harness.attach_save(sav, static_cast<u32>(M3SaveTypeHint::Eeprom8KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren(emit);
        emit_rdsr(emit, 0U);
        emit_wrdi(emit);
        emit_rdsr(emit, 2U);
        emit_raw_tx(emit, {0x02U, 0x00U, 0x20U, 0x5AU});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x20U}, 1U, 4U});
    });

    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U), 0x02U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 1U), 0x00U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(4U) + 3U), 0xFFU);
}

TEST(Tier2SaveMemoryE2e, WrenAutoClearsAfterWriteTransactionCompletes) {
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    const auto sav = dir.path() / "autoclr.sav";
    ASSERT_TRUE(harness.attach_save(sav, static_cast<u32>(M3SaveTypeHint::Eeprom8KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x00U, 0x30U, 0x77U});
        emit_rdsr(emit, 0U);
        emit_raw_tx(emit, {0x02U, 0x00U, 0x31U, 0x88U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x30U}, 2U, 2U});
    });

    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 1U), 0x00U);
    expect_bytes(harness, result_slot_addr(2U) + 3U, {0x77U, 0xFFU});
}

TEST(Tier2SaveMemoryE2e, Eeprom512BReadWrapsAcross512ByteBoundary) {
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    const auto sav = dir.path() / "wrap512.sav";
    ASSERT_TRUE(harness.attach_save(sav, static_cast<u32>(M3SaveTypeHint::Eeprom512B)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x00U, 0xA0U, 0xA1U});
        emit_wren_tx(emit, {0x0AU, 0xFEU, 0xB0U, 0xB1U});
        emit_spi_read(emit, SpiReadReq{{0x0BU, 0xFEU}, 4U, 0U});
    });

    expect_bytes(harness, result_slot_addr(0U) + 2U, {0xB0U, 0xB1U, 0xA0U, 0xA1U});
}

TEST(Tier2SaveMemoryE2e, Eeprom8KBReadWrapsAcross8KiBBoundary) {
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    const auto sav = dir.path() / "wrap8k.sav";
    ASSERT_TRUE(harness.attach_save(sav, static_cast<u32>(M3SaveTypeHint::Eeprom8KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x00U, 0x00U, 0xC0U, 0xC1U});
        emit_wren_tx(emit, {0x02U, 0x1FU, 0xFEU, 0xD0U, 0xD1U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x1FU, 0xFEU}, 4U, 0U});
    });

    expect_bytes(harness, result_slot_addr(0U) + 3U, {0xD0U, 0xD1U, 0xC0U, 0xC1U});
}

TEST(Tier2SaveMemoryE2e, UnknownSpiOpcodeLeavesDataIntactAndPageErasePreservesNeighbors) {
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    const auto sav = dir.path() / "unk_op.sav";
    ASSERT_TRUE(harness.attach_save(sav, static_cast<u32>(M3SaveTypeHint::Flash256KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x00U, 0x00U, 0xFFU, 0x11U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x01U, 0x00U, 0x22U, 0x33U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x01U, 0xFFU, 0x44U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x02U, 0x00U, 0x55U});

        emit_wren_tx(emit, {0xEEU, 0x00U, 0x01U, 0x00U, 0x99U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x01U, 0x00U}, 2U, 0U});

        emit_wren_tx(emit, {0xDBU, 0x00U, 0x01U, 0x80U});
        static constexpr std::array<u32, 4U> kAddrs{0x0000FFU, 0x000100U, 0x0001FFU, 0x000200U};
        for (u32 i = 0U; i < 4U; ++i) {
            const u32 a = kAddrs[i];
            emit_spi_read(
                emit,
                SpiReadReq{{0x03U, addr_byte(a, 16U), addr_byte(a, 8U), addr_byte(a, 0U)},
                           1U,
                           2U + (i * 2U)});
        }
    });

    expect_bytes(harness, result_slot_addr(0U) + 4U, {0x22U, 0x33U});
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 4U), 0x11U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(4U) + 4U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(6U) + 4U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(8U) + 4U), 0x55U);
}

// F28: Capacity Detection & Deferred Persistence (Tier 2 Boundary & Negative)

TEST(Tier2SaveMemoryE2e, AttachEmptySavePathFailsWithError) {
    EmulatorE2eHarness harness;
    const Status status = harness.attach_save("", static_cast<u32>(M3SaveTypeHint::None));
    EXPECT_TRUE(status.is_err());
}

TEST(Tier2SaveMemoryE2e, FlushSaveOnCleanSaveIsNoOpWithoutCreatingFile) {
    ScopedTempDir dir;
    const auto sav_path = dir.path() / "clean.sav";
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(sav_path, static_cast<u32>(M3SaveTypeHint::Eeprom8KB)).is_ok());
    EXPECT_TRUE(harness.flush_save().is_ok());
    EXPECT_FALSE(std::filesystem::exists(sav_path));

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x00U}, 1U, 0U});
    });
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 3U), 0xFFU);
    EXPECT_TRUE(harness.flush_save().is_ok());
    EXPECT_FALSE(std::filesystem::exists(sav_path));
}

class Tier2SaveFallbackTest : public ::testing::TestWithParam<FallbackSizeCase> {};

TEST_P(Tier2SaveFallbackTest, UnsupportedSaveFileSizesFallBackToFlash512KB) {
    const FallbackSizeCase tc = GetParam();
    ScopedTempDir dir;
    std::vector<u8> raw(tc.file_bytes, 0xFFU);
    raw[0x10U] = tc.sentinel;
    const auto sav_path = dir.write_file("fallback.sav", raw);

    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(sav_path, static_cast<u32>(M3SaveTypeHint::None)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_spi_read(emit, SpiReadReq{{0x9FU}, 3U, 0U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x00U, 0x10U}, 1U, 2U});
    });

    expect_bytes(harness, result_slot_addr(0U) + 1U, {0x20U, 0x40U, 0x13U});
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 4U), tc.sentinel);
}

INSTANTIATE_TEST_SUITE_P(
    UnsupportedSaveSizes,
    Tier2SaveFallbackTest,
    ::testing::Values(FallbackSizeCase{12345U, 0x5AU}, FallbackSizeCase{131072U, 0x6CU}));

TEST(Tier2SaveMemoryE2e, ExplicitTypeHintOverridesExistingFileSize) {
    ScopedTempDir dir;
    const std::vector<u8> img = build_save_image(SaveImageSpec{
        .kind = SyntheticSaveKind::Eeprom8K,
        .fill_byte = 0xFFU,
        .initial_patch = {0x6BU},
        .patch_offset = 0x0020U,
    });
    const auto sav_path = dir.write_file("override.sav", img);

    EmulatorE2eHarness harness;
    ASSERT_TRUE(
        harness.attach_save(sav_path, static_cast<u32>(M3SaveTypeHint::Flash256KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_spi_read(emit, SpiReadReq{{0x9FU}, 3U, 0U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x00U, 0x20U}, 1U, 2U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x40U, 0x00U}, 1U, 4U});
    });

    expect_bytes(harness, result_slot_addr(0U) + 1U, {0x20U, 0x40U, 0x12U});
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 4U), 0x6BU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(4U) + 4U), 0xFFU);
}

TEST(Tier2SaveMemoryE2e, TruncatedExistingSavePadsRemainingBytesWith0xFF) {
    ScopedTempDir dir;
    const std::vector<u8> img = build_save_image(SaveImageSpec{
        .kind = SyntheticSaveKind::Eeprom512B,
        .fill_byte = 0x11U,
    });
    const auto sav_path = dir.write_file("truncated.sav", img);

    EmulatorE2eHarness harness;
    ASSERT_TRUE(
        harness.attach_save(sav_path, static_cast<u32>(M3SaveTypeHint::Eeprom8KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        static constexpr std::array<u16, 4U> kOffs{0x0000U, 0x01FFU, 0x0200U, 0x1FFFU};
        for (u32 i = 0U; i < 4U; ++i) {
            emit_spi_read(
                emit,
                SpiReadReq{{0x03U, addr_byte(kOffs[i], 8U), addr_byte(kOffs[i], 0U)}, 1U, i * 2U});
        }
    });

    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(0U) + 3U), 0x11U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(2U) + 3U), 0x11U);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(4U) + 3U), 0xFFU);
    EXPECT_EQ(harness.read_main_ram8(result_slot_addr(6U) + 3U), 0xFFU);
}

}  // namespace
}  // namespace elmasds::e2e::m3
