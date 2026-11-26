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
constexpr u32 kDoneMagic = 0xD04E5A71U;
constexpr TickCount kDefaultSteps = 40'000ULL;

struct EepromCase {
    M3SaveTypeHint hint = M3SaveTypeHint::Eeprom8KB;
    u16 addr = 0x1234U;
};

struct FlashCase {
    M3SaveTypeHint hint = M3SaveTypeHint::Flash256KB;
    u8 expected_cap_id = 0x12U;
    u32 test_addr = 0x034567U;
};

struct AutoDetectCase {
    SyntheticSaveKind kind = SyntheticSaveKind::Eeprom512B;
    std::size_t expected_bytes = 512U;
    u8 addr_bytes = 1U;
    u32 patch_offset = 0x10U;
    u8 expected_jedec_cap = 0U;
};

struct SpiReadReq {
    std::vector<u8> prefix;
    u32 read_bytes = 1U;
    u32 slot = 0U;
};

constexpr u32 as_hint(M3SaveTypeHint hint) noexcept {
    return static_cast<u32>(hint);
}

u8 addr_byte(u32 addr, u32 shift) noexcept {
    return static_cast<u8>((addr >> shift) & 0xFFU);
}

void emit_wren_tx(ArmProgramEmitter& emit, std::vector<u8> tx) {
    static constexpr std::array<u8, 1U> kWren{0x06U};
    emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = kWren});
    emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = tx});
}

void emit_spi_read(ArmProgramEmitter& emit, SpiReadReq req) {
    req.prefix.resize(req.prefix.size() + req.read_bytes, 0x00U);
    const Address dst = result_slot_addr(req.slot);
    emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = req.prefix, .rx_ram_addr = dst});
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

void expect_file_bytes(std::span<const u8> data,
                       std::size_t offset,
                       std::initializer_list<u8> expected) {
    std::size_t idx = 0U;
    for (const u8 want : expected) {
        ASSERT_LT(offset + idx, data.size());
        EXPECT_EQ(data[offset + idx], want) << "offset=" << (offset + idx);
        ++idx;
    }
}

// F27: Save Memory Types (Tier 1 Happy Path)

TEST(Tier1SaveMemoryE2e, Eeprom512BWritesAndReadsBoth256BytePages) {
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(dir.path() / "ee512.sav", as_hint(M3SaveTypeHint::Eeprom512B))
                    .is_ok());
    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x10U, 0xA1U, 0xB2U});
        emit_wren_tx(emit, {0x0AU, 0x10U, 0xC3U, 0xD4U});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x10U}, 2U, 0U});
        emit_spi_read(emit, SpiReadReq{{0x0BU, 0x10U}, 2U, 2U});
    });
    expect_bytes(harness, result_slot_addr(0U) + 2U, {0xA1U, 0xB2U});
    expect_bytes(harness, result_slot_addr(2U) + 2U, {0xC3U, 0xD4U});
}

class Tier1Eeprom2ByteTest : public ::testing::TestWithParam<EepromCase> {};

TEST_P(Tier1Eeprom2ByteTest, WriteReadRoundTripWithTwoByteBigEndianAddress) {
    const EepromCase tc = GetParam();
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(dir.path() / "ee.sav", as_hint(tc.hint)).is_ok());

    run_arm9_guest(harness, [tc](ArmProgramEmitter& emit) {
        const u8 hi = addr_byte(tc.addr, 8U);
        const u8 lo = addr_byte(tc.addr, 0U);
        emit_wren_tx(emit, {0x02U, hi, lo, 0x12U, 0x34U, 0x56U, 0x78U});
        emit_spi_read(emit, SpiReadReq{{0x03U, hi, lo}, 4U, 0U});
    });
    expect_bytes(harness, result_slot_addr(0U) + 3U, {0x12U, 0x34U, 0x56U, 0x78U});
}

INSTANTIATE_TEST_SUITE_P(
    Eeprom8KAnd64K,
    Tier1Eeprom2ByteTest,
    ::testing::Values(EepromCase{M3SaveTypeHint::Eeprom8KB, 0x1234U},
                      EepromCase{M3SaveTypeHint::Eeprom64KB, 0xABCDU}));

class Tier1FlashSaveTest : public ::testing::TestWithParam<FlashCase> {};

TEST_P(Tier1FlashSaveTest, ReadsJedecIdAndFastReadAfterWrite) {
    const FlashCase tc = GetParam();
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(dir.path() / "fl.sav", as_hint(tc.hint)).is_ok());

    run_arm9_guest(harness, [tc](ArmProgramEmitter& emit) {
        const u8 a2 = addr_byte(tc.test_addr, 16U);
        const u8 a1 = addr_byte(tc.test_addr, 8U);
        const u8 a0 = addr_byte(tc.test_addr, 0U);
        emit_spi_read(emit, SpiReadReq{{0x9FU}, 3U, 0U});
        emit_wren_tx(emit, {0x02U, a2, a1, a0, 0x9AU, 0xBCU});
        emit_spi_read(emit, SpiReadReq{{0x0BU, a2, a1, a0, 0x00U}, 2U, 2U});
    });
    expect_bytes(harness, result_slot_addr(0U) + 1U, {0x20U, 0x40U, tc.expected_cap_id});
    expect_bytes(harness, result_slot_addr(2U) + 5U, {0x9AU, 0xBCU});
}

INSTANTIATE_TEST_SUITE_P(
    FlashCapacities,
    Tier1FlashSaveTest,
    ::testing::Values(FlashCase{M3SaveTypeHint::Flash256KB, 0x12U, 0x034567U},
                      FlashCase{M3SaveTypeHint::Flash512KB, 0x13U, 0x056789U},
                      FlashCase{M3SaveTypeHint::Flash1MB, 0x14U, 0x09ABCDU}));

TEST(Tier1SaveMemoryE2e, FlashPageSubsectorAndSectorEraseRestore0xFF) {
    ScopedTempDir dir;
    EmulatorE2eHarness harness;
    ASSERT_TRUE(
        harness.attach_save(dir.path() / "erase.sav", as_hint(M3SaveTypeHint::Flash512KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x00U, 0x01U, 0x05U, 0x11U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x02U, 0x05U, 0x22U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x1FU, 0x00U, 0x33U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x20U, 0x10U, 0x44U});
        emit_wren_tx(emit, {0x02U, 0x01U, 0xFFU, 0x00U, 0x55U});
        emit_wren_tx(emit, {0x02U, 0x02U, 0x00U, 0x10U, 0x66U});
        emit_wren_tx(emit, {0xDBU, 0x00U, 0x01U, 0x00U});
        emit_wren_tx(emit, {0x20U, 0x00U, 0x10U, 0x00U});
        emit_wren_tx(emit, {0xD8U, 0x01U, 0x00U, 0x00U});
        static constexpr std::array<u32, 6U> kAddrs{
            0x000105U, 0x000205U, 0x001F00U, 0x002010U, 0x01FF00U, 0x020010U};
        for (u32 i = 0U; i < 6U; ++i) {
            const u32 a = kAddrs[i];
            const std::vector<u8> p{0x03U, addr_byte(a, 16U), addr_byte(a, 8U), addr_byte(a, 0U)};
            emit_spi_read(emit, SpiReadReq{p, 1U, i * 2U});
        }
    });
    static constexpr std::array<u8, 6U> kWant{0xFFU, 0x22U, 0xFFU, 0x44U, 0xFFU, 0x66U};
    for (u32 i = 0U; i < 6U; ++i) {
        EXPECT_EQ(harness.read_main_ram8(result_slot_addr(i * 2U) + 4U), kWant[i]);
    }
}

TEST(Tier1SaveMemoryE2e, Sram32KBSpiRoundTripAndNtr031InfraredPassthrough) {
    ScopedTempDir dir;
    EmulatorE2eHarness sram_harness;
    ASSERT_TRUE(
        sram_harness.attach_save(dir.path() / "sram.sav", as_hint(M3SaveTypeHint::Sram32KB))
            .is_ok());
    run_arm9_guest(sram_harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x00U, 0x45U, 0x67U, 0xDEU, 0xADU});
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x45U, 0x67U}, 2U, 0U});
    });
    expect_bytes(sram_harness, result_slot_addr(0U) + 4U, {0xDEU, 0xADU});

    EmulatorE2eHarness ir_harness;
    ASSERT_TRUE(
        ir_harness.attach_save(dir.path() / "ir.sav", as_hint(M3SaveTypeHint::Flash512KB)).is_ok());
    run_arm9_guest(ir_harness, [](ArmProgramEmitter& emit) {
        static constexpr std::array<u8, 2U> kIrId{0x08U, 0x00U};
        static constexpr std::array<u8, 5U> kPass{0x00U, 0x9FU, 0x00U, 0x00U, 0x00U};
        emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = kIrId, .rx_ram_addr = result_slot_addr(0U)});
        emit_auxspi_seq(emit, AuxSpiSeqSpec{.tx_bytes = kPass, .rx_ram_addr = result_slot_addr(2U)});
    });
    EXPECT_EQ(ir_harness.read_main_ram8(result_slot_addr(0U) + 1U), 0xAAU);
    expect_bytes(ir_harness, result_slot_addr(2U) + 2U, {0x20U, 0x40U, 0x13U});
}

// F28: Capacity Detection & Deferred Disk Persistence (Tier 1 Happy Path)

class Tier1SaveAutoDetectTest : public ::testing::TestWithParam<AutoDetectCase> {};

TEST_P(Tier1SaveAutoDetectTest, AutoDetectsExistingSaveCapacityWithHintNone) {
    const AutoDetectCase tc = GetParam();
    ScopedTempDir dir;
    const std::vector<u8> img = build_save_image(SaveImageSpec{
        .kind = tc.kind,
        .fill_byte = 0xFFU,
        .initial_patch = {0x5AU, 0xC7U},
        .patch_offset = tc.patch_offset,
    });
    ASSERT_EQ(img.size(), tc.expected_bytes);
    const auto sav_path = dir.write_file("autodetect.sav", img);

    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(sav_path, as_hint(M3SaveTypeHint::None)).is_ok());
    run_arm9_guest(harness, [tc](ArmProgramEmitter& emit) {
        std::vector<u8> rd_cmd{0x03U};
        if (tc.addr_bytes >= 3U) {
            rd_cmd.push_back(addr_byte(tc.patch_offset, 16U));
        }
        if (tc.addr_bytes >= 2U) {
            rd_cmd.push_back(addr_byte(tc.patch_offset, 8U));
        }
        rd_cmd.push_back(addr_byte(tc.patch_offset, 0U));
        emit_spi_read(emit, SpiReadReq{rd_cmd, 2U, 0U});
        if (tc.expected_jedec_cap != 0U) {
            emit_spi_read(emit, SpiReadReq{{0x9FU}, 3U, 4U});
        }
    });
    expect_bytes(harness, result_slot_addr(0U) + 1U + tc.addr_bytes, {0x5AU, 0xC7U});
    if (tc.expected_jedec_cap != 0U) {
        expect_bytes(harness, result_slot_addr(4U) + 1U, {0x20U, 0x40U, tc.expected_jedec_cap});
    }
}

INSTANTIATE_TEST_SUITE_P(
    AllSupportedSizes,
    Tier1SaveAutoDetectTest,
    ::testing::Values(
        AutoDetectCase{SyntheticSaveKind::Eeprom512B, 512U, 1U, 0x10U, 0U},
        AutoDetectCase{SyntheticSaveKind::Eeprom8K, 8192U, 2U, 0x0240U, 0U},
        AutoDetectCase{SyntheticSaveKind::Sram32K, 32768U, 3U, 0x004120U, 0U},
        AutoDetectCase{SyntheticSaveKind::Eeprom64K, 65536U, 2U, 0x8200U, 0U},
        AutoDetectCase{SyntheticSaveKind::Flash256K, 262144U, 3U, 0x021000U, 0x12U},
        AutoDetectCase{SyntheticSaveKind::Flash512K, 524288U, 3U, 0x041000U, 0x13U},
        AutoDetectCase{SyntheticSaveKind::Flash1M, 1048576U, 3U, 0x081000U, 0x14U}));

TEST(Tier1SaveMemoryE2e, DeferredWriteDoesNotCreateDiskFileUntilFlush) {
    ScopedTempDir dir;
    const auto sav_path = dir.path() / "deferred.sav";
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(sav_path, as_hint(M3SaveTypeHint::Eeprom8KB)).is_ok());
    EXPECT_FALSE(std::filesystem::exists(sav_path));

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x01U, 0x00U, 0xCAU, 0xFEU, 0xBAU, 0xBEU});
    });
    EXPECT_FALSE(std::filesystem::exists(sav_path));

    ASSERT_TRUE(harness.flush_save().is_ok());
    ASSERT_TRUE(std::filesystem::exists(sav_path));
    const std::vector<u8> saved = read_save_file(sav_path);
    ASSERT_EQ(saved.size(), 8192U);
    expect_file_bytes(saved, 0x0100U, {0xCAU, 0xFEU, 0xBAU, 0xBEU});
}

TEST(Tier1SaveMemoryE2e, ReloadsFlushedSaveInFreshHarnessWithHintNone) {
    ScopedTempDir dir;
    const auto sav_path = dir.path() / "persist.sav";
    {
        EmulatorE2eHarness writer;
        ASSERT_TRUE(writer.attach_save(sav_path, as_hint(M3SaveTypeHint::Eeprom8KB)).is_ok());
        run_arm9_guest(writer, [](ArmProgramEmitter& emit) {
            emit_wren_tx(emit, {0x02U, 0x02U, 0x40U, 0x44U, 0x55U, 0x66U, 0x77U});
        });
        ASSERT_TRUE(writer.flush_save().is_ok());
    }
    EmulatorE2eHarness reader;
    ASSERT_TRUE(reader.attach_save(sav_path, as_hint(M3SaveTypeHint::None)).is_ok());
    run_arm9_guest(reader, [](ArmProgramEmitter& emit) {
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x02U, 0x40U}, 4U, 0U});
    });
    expect_bytes(reader, result_slot_addr(0U) + 3U, {0x44U, 0x55U, 0x66U, 0x77U});
}

TEST(Tier1SaveMemoryE2e, SaveStorageAndDirtyStateSurviveDirectBootReboot) {
    ScopedTempDir dir;
    const auto sav_path = dir.path() / "reboot.sav";
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(sav_path, as_hint(M3SaveTypeHint::Eeprom8KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x00U, 0x80U, 0xDEU, 0xADU, 0xBEU, 0xEFU});
    });
    EXPECT_FALSE(std::filesystem::exists(sav_path));

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_spi_read(emit, SpiReadReq{{0x03U, 0x00U, 0x80U}, 4U, 0U});
    });
    expect_bytes(harness, result_slot_addr(0U) + 3U, {0xDEU, 0xADU, 0xBEU, 0xEFU});
    EXPECT_FALSE(std::filesystem::exists(sav_path));

    ASSERT_TRUE(harness.flush_save().is_ok());
    const std::vector<u8> saved = read_save_file(sav_path);
    expect_file_bytes(saved, 0x0080U, {0xDEU, 0xADU, 0xBEU, 0xEFU});
}

TEST(Tier1SaveMemoryE2e, MultipleWritesAndErasesAccumulateAndFlushOnce) {
    ScopedTempDir dir;
    const auto sav_path = dir.path() / "multi.sav";
    EmulatorE2eHarness harness;
    ASSERT_TRUE(harness.attach_save(sav_path, as_hint(M3SaveTypeHint::Flash256KB)).is_ok());

    run_arm9_guest(harness, [](ArmProgramEmitter& emit) {
        emit_wren_tx(emit, {0x02U, 0x00U, 0x01U, 0x00U, 0x11U, 0x22U, 0x33U, 0x44U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x02U, 0x00U, 0x55U, 0x66U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x10U, 0x00U, 0x77U, 0x88U});
        emit_wren_tx(emit, {0xDBU, 0x00U, 0x01U, 0x00U});
        emit_wren_tx(emit, {0x02U, 0x00U, 0x01U, 0x02U, 0x99U, 0xAAU});
    });
    EXPECT_FALSE(std::filesystem::exists(sav_path));

    ASSERT_TRUE(harness.flush_save().is_ok());
    const std::vector<u8> saved = read_save_file(sav_path);
    ASSERT_EQ(saved.size(), 262144U);
    expect_file_bytes(saved, 0x000100U, {0xFFU, 0xFFU, 0x99U, 0xAAU});
    expect_file_bytes(saved, 0x000200U, {0x55U, 0x66U});
    expect_file_bytes(saved, 0x001000U, {0x77U, 0x88U});
}

}  // namespace
}  // namespace elmasds::e2e::m3
