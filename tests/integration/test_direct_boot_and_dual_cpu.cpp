#include "core/emulator.hpp"
#include "storage/rom_loader/nds_rom_header.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace elmasds::core {
namespace {

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

std::vector<u8> encode_words(const std::vector<u32>& words) {
    std::vector<u8> out(words.size() * 4U, u8{0});
    for (std::size_t i = 0U; i < words.size(); ++i) {
        write_le32(out, i * 4U, words[i]);
    }
    return out;
}

std::vector<u8> build_dual_cpu_ipc_rom() {
    // ARM9 program at 0x02000000:
    //   r0 = 0x04000180 (IPC base)
    //   r1 = 0x8000     (IPCFIFOCNT enable)
    //   STRH r1, [r0, #4]           ; enable ARM9 IPC FIFO
    //   r2 = 0xCAFE1234 (loaded from literal pool)
    //   STR r2, [r0, #8]            ; IPCFIFOSEND = 0xCAFE1234
    // wait_loop:
    //   LDRH r3, [r0, #4]           ; read IPCFIFOCNT
    //   TST r3, #0x100              ; recv empty?
    //   BNE wait_loop
    //   r4 = 0x04100000
    //   LDR r5, [r4]                ; IPCFIFORECV
    //   r6 = 0x02100000
    //   STR r5, [r6]                ; store ARM7's reply in Main RAM
    // done:
    //   B done
    const std::vector<u32> arm9_prog = {
        0xE59F002CU,  // 0x00: LDR r0, [pc, #44] -> 0x34 (0x04000180)
        0xE3A01902U,  // 0x04: MOV r1, #0x8000
        0xE1C010B4U,  // 0x08: STRH r1, [r0, #4]
        0xE59F2024U,  // 0x0C: LDR r2, [pc, #36] -> 0x38 (0xCAFE1234)
        0xE5802008U,  // 0x10: STR r2, [r0, #8]
        0xE1D030B4U,  // 0x14: LDRH r3, [r0, #4]
        0xE3130C01U,  // 0x18: TST r3, #0x100
        0x1AFFFFFCU,  // 0x1C: BNE 0x14
        0xE59F4014U,  // 0x20: LDR r4, [pc, #20] -> 0x3C (0x04100000)
        0xE5945000U,  // 0x24: LDR r5, [r4]
        0xE59F6010U,  // 0x28: LDR r6, [pc, #16] -> 0x40 (0x02100000)
        0xE5865000U,  // 0x2C: STR r5, [r6]
        0xEAFFFFFEU,  // 0x30: B 0x30 (halt loop)
        0x04000180U,  // 0x34
        0xCAFE1234U,  // 0x38
        0x04100000U,  // 0x3C
        0x02100000U,  // 0x40
    };

    // ARM7 program at 0x03800000:
    //   Enables IPC FIFO, waits until recv not empty, reads word, adds 1, sends back.
    const std::vector<u32> arm7_code = {
        0xE59F0024U,  // 0x00: LDR r0, [pc, #36] -> 0x2C (0x04000180)
        0xE3A01902U,  // 0x04: MOV r1, #0x8000
        0xE1C010B4U,  // 0x08: STRH r1, [r0, #4]
        0xE1D030B4U,  // 0x0C: LDRH r3, [r0, #4]
        0xE3130C01U,  // 0x10: TST r3, #0x100
        0x1AFFFFFCU,  // 0x14: BNE 0x0C
        0xE59F4010U,  // 0x18: LDR r4, [pc, #16] -> 0x30 (0x04100000)
        0xE5945000U,  // 0x1C: LDR r5, [r4]
        0xE2855001U,  // 0x20: ADD r5, r5, #1
        0xE5805008U,  // 0x24: STR r5, [r0, #8]
        0xEAFFFFFEU,  // 0x28: B 0x28
        0x04000180U,  // 0x2C
        0x04100000U,  // 0x30
    };

    const auto arm9_bytes = encode_words(arm9_prog);
    const auto arm7_bytes = encode_words(arm7_code);

    std::vector<u8> rom(0x9000U, u8{0});
    const char* title = "DUALCPU";
    for (std::size_t i = 0U; i < 7U; ++i) {
        rom[i] = static_cast<u8>(title[i]);
    }
    write_le32(rom, 0x020U, 0x4000U);
    write_le32(rom, 0x024U, 0x02000000U);
    write_le32(rom, 0x028U, 0x02000000U);
    write_le32(rom, 0x02CU, static_cast<u32>(arm9_bytes.size()));

    write_le32(rom, 0x030U, 0x8000U);
    write_le32(rom, 0x034U, 0x03800000U);
    write_le32(rom, 0x038U, 0x03800000U);
    write_le32(rom, 0x03CU, static_cast<u32>(arm7_bytes.size()));

    std::copy(arm9_bytes.begin(), arm9_bytes.end(), rom.begin() + 0x4000);
    std::copy(arm7_bytes.begin(), arm7_bytes.end(), rom.begin() + 0x8000);

    const u16 logo_crc = storage::rom_loader::compute_nds_crc16(
        std::span<const u8>(rom).subspan(storage::rom_loader::kNdsLogoOffset,
                                         storage::rom_loader::kNdsLogoSize));
    write_le16(rom, 0x15CU, logo_crc);
    const u16 hdr_crc =
        storage::rom_loader::compute_nds_crc16(std::span<const u8>(rom).subspan(0U, 0x15EU));
    write_le16(rom, 0x15EU, hdr_crc);
    return rom;
}

TEST(EmulatorIntegrationTest, DirectBootExecutesDualCpuIpcRoundTrip) {
    Emulator emulator(EmulatorConfig{.log_level = LogLevel::Warning});
    EXPECT_TRUE(emulator.direct_boot().is_err());

    const auto rom = build_dual_cpu_ipc_rom();
    ASSERT_TRUE(emulator.load_rom_bytes(rom).is_ok());
    ASSERT_TRUE(emulator.direct_boot().is_ok());

    // Header must be copied to 0x027FFE00 per NDS direct boot contract.
    EXPECT_EQ(emulator.read_main_ram8(0x027FFE00U), 'D');
    EXPECT_EQ(emulator.read_main_ram8(0x027FFE01U), 'U');
    EXPECT_EQ(emulator.arm9_pc(), 0x02000000U);
    EXPECT_EQ(emulator.arm7_pc(), 0x03800000U);

    // Step deterministic master ticks: ARM9 -> IPC -> ARM7 (+1) -> IPC -> ARM9 -> Main RAM.
    emulator.step_ticks(1024ULL);
    EXPECT_EQ(emulator.read_main_ram32(0x02100000U), 0xCAFE1235U);
}

}  // namespace
}  // namespace elmasds::core
