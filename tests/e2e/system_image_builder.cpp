#include "e2e/system_image_builder.hpp"

#include "e2e/nds_rom_builder.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace elmasds::e2e {
namespace {

constexpr u32 kWordBranchSelf = 0xEAFFFFFEU;
constexpr u32 kWordPushIrqRegs = 0xE92D500FU;
constexpr u32 kWordPopIrqRegs = 0xE8BD500FU;
constexpr u32 kWordSubPcLr4 = 0xE25EF004U;
constexpr u32 kWordMovsPcLr = 0xE1B0F00EU;
constexpr u32 kWordLdrPcPc0 = 0xE59FF000U;
constexpr u32 kWordLdrR0AtR0 = 0xE5900000U;
constexpr u32 kWordCmpR0Zero = 0xE3500000U;
constexpr u32 kWordMovLrPc = 0xE1A0E00FU;
constexpr u32 kWordBxR0 = 0xE12FFF10U;

void write_le16(std::span<u8> dst, std::size_t offset, u16 value) noexcept {
    if (offset + 2U > dst.size()) {
        return;
    }
    dst[offset] = static_cast<u8>(value & 0xFFU);
    dst[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
}

void write_le32(std::span<u8> dst, std::size_t offset, u32 value) noexcept {
    if (offset + 4U > dst.size()) {
        return;
    }
    dst[offset + 0U] = static_cast<u8>(value & 0xFFU);
    dst[offset + 1U] = static_cast<u8>((value >> 8U) & 0xFFU);
    dst[offset + 2U] = static_cast<u8>((value >> 16U) & 0xFFU);
    dst[offset + 3U] = static_cast<u8>((value >> 24U) & 0xFFU);
}

[[nodiscard]] u32 encode_branch_words(std::size_t from_word,
                                      std::size_t to_word,
                                      u8 cond = 0xEU) noexcept {
    const i32 word_delta = static_cast<i32>(to_word) - static_cast<i32>(from_word) - 2;
    return (static_cast<u32>(cond) << 28U) | (0x5U << 25U) |
           (static_cast<u32>(word_delta) & 0x00FFFFFFU);
}

[[nodiscard]] u32 encode_ldr_pc_rel(std::size_t instr_word,
                                    std::size_t literal_word,
                                    u8 rd = 0U) noexcept {
    const i32 byte_delta = (static_cast<i32>(literal_word) - static_cast<i32>(instr_word) - 2) * 4;
    const u32 u_bit = (byte_delta >= 0) ? (1U << 23U) : 0U;
    const u32 abs_off = static_cast<u32>(byte_delta >= 0 ? byte_delta : -byte_delta);
    return 0xE51F0000U | u_bit | (static_cast<u32>(rd) << 12U) | (abs_off & 0xFFFU);
}

void write_bios_word(std::span<u8> bios, std::size_t word_index, u32 word) noexcept {
    write_le32(bios, word_index * 4U, word);
}

void write_common_vectors(std::span<u8> bios, Address reset_pc, bool include_irq) noexcept {
    for (std::size_t i = 0; i < 8U; ++i) {
        write_bios_word(bios, i, kWordBranchSelf);
    }
    write_bios_word(bios, 0U, encode_branch_words(0U, 8U));
    write_bios_word(bios, 2U, encode_branch_words(2U, 12U));
    if (include_irq) {
        write_bios_word(bios, 6U, encode_branch_words(6U, 16U));
    }
    write_bios_word(bios, 8U, kWordLdrPcPc0);
    write_bios_word(bios, 10U, reset_pc);
}

void write_bios_words(std::span<u8> bios,
                      std::size_t start_word,
                      std::span<const u32> words) noexcept {
    for (std::size_t i = 0; i < words.size(); ++i) {
        write_bios_word(bios, start_word + i, words[i]);
    }
}

void write_bios9_irq_trampoline(std::span<u8> bios,
                                Address primary_ptr,
                                Address fallback_ptr) noexcept {
    const std::array<u32, 26> words{
        kWordPushIrqRegs, 0xEE110F10U, 0xE3100801U, encode_branch_words(19U, 28U, 0x0U),
        0xEE190F11U,      0xE1A00620U, 0xE1A00600U, 0xE2800C3FU,
        0xE28000FCU,      kWordLdrR0AtR0, kWordCmpR0Zero, encode_branch_words(27U, 36U, 0x1U),
        encode_ldr_pc_rel(28U, 40U), kWordLdrR0AtR0, kWordCmpR0Zero,
        encode_branch_words(31U, 36U, 0x1U), encode_ldr_pc_rel(32U, 41U), kWordLdrR0AtR0,
        kWordCmpR0Zero, encode_branch_words(35U, 38U, 0x0U), kWordMovLrPc, kWordBxR0,
        kWordPopIrqRegs, kWordSubPcLr4, primary_ptr, fallback_ptr,
    };
    write_bios_words(bios, 16U, words);
}

void write_bios7_irq_trampoline(std::span<u8> bios, Address irq_ptr_addr) noexcept {
    const std::array<u32, 10> words{
        kWordPushIrqRegs, encode_ldr_pc_rel(17U, 25U), kWordLdrR0AtR0, kWordCmpR0Zero,
        encode_branch_words(20U, 23U, 0x0U), kWordMovLrPc, kWordBxR0, kWordPopIrqRegs,
        kWordSubPcLr4, irq_ptr_addr,
    };
    write_bios_words(bios, 16U, words);
}

[[nodiscard]] u32 normalize_firmware_size(u32 requested) noexcept {
    if (requested == kFirmwareSize128K || requested == kFirmwareSize256K ||
        requested == kFirmwareSize512K) {
        return requested;
    }
    return kFirmwareSize256K;
}

void write_utf16_field(std::span<u8> dst, const std::u16string& text) noexcept {
    const std::size_t count = std::min(dst.size() / 2U, text.size());
    for (std::size_t i = 0; i < count; ++i) {
        write_le16(dst, i * 2U, static_cast<u16>(text[i]));
    }
}

void write_firmware_user_slot(std::span<u8> slot,
                              const FirmwareSpec& spec,
                              u16 update_counter) noexcept {
    std::fill(slot.begin(), slot.begin() + 0x74, u8{0});
    write_le16(slot, 0x000U, 5U);
    slot[0x002U] = static_cast<u8>(spec.favorite_color & 0x0FU);
    slot[0x003U] = spec.birthday_month;
    slot[0x004U] = spec.birthday_day;

    const std::size_t nick_len = std::min<std::size_t>(10U, spec.nickname.size());
    write_utf16_field(slot.subspan(0x006U, 20U), spec.nickname);
    write_le16(slot, 0x01AU, static_cast<u16>(nick_len));

    const std::size_t msg_len = std::min<std::size_t>(26U, spec.message.size());
    write_utf16_field(slot.subspan(0x01CU, 52U), spec.message);
    write_le16(slot, 0x050U, static_cast<u16>(msg_len));

    write_le16(slot, 0x058U, 0x0200U);
    write_le16(slot, 0x05AU, 0x0200U);
    slot[0x05CU] = 32U;
    slot[0x05DU] = 32U;
    write_le16(slot, 0x05EU, 0x0E00U);
    write_le16(slot, 0x060U, 0x0C00U);
    slot[0x062U] = 224U;
    slot[0x063U] = 160U;

    write_le16(slot, 0x064U, static_cast<u16>(spec.language & 0x07U));
    write_le16(slot, 0x070U, update_counter);

    u16 crc = compute_crc16(slot.subspan(0U, 0x070U), kCrc16DefaultInit);
    if (spec.corrupt_user_settings_crc) {
        crc ^= 0xFFFFU;
    }
    write_le16(slot, 0x072U, crc);
}

[[nodiscard]] std::filesystem::path create_unique_temp_dir() {
    static std::atomic<u64> counter{0U};
    const u64 seq = counter.fetch_add(1U, std::memory_order_relaxed);
    const auto ticks = static_cast<u64>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const std::filesystem::path base = std::filesystem::temp_directory_path();
    const std::string name =
        "elmasds_e2e_" + std::to_string(ticks) + "_" + std::to_string(seq);
    const std::filesystem::path dir = base / name;
    std::filesystem::create_directories(dir);
    return dir;
}

}  // namespace

ScopedTempDir::ScopedTempDir() : dir_path_(create_unique_temp_dir()) {}

ScopedTempDir::~ScopedTempDir() {
    if (!dir_path_.empty()) {
        std::error_code ec;
        std::filesystem::remove_all(dir_path_, ec);
    }
}

ScopedTempDir::ScopedTempDir(ScopedTempDir&& other) noexcept
    : dir_path_(std::exchange(other.dir_path_, {})) {}

ScopedTempDir& ScopedTempDir::operator=(ScopedTempDir&& other) noexcept {
    if (this != &other) {
        if (!dir_path_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(dir_path_, ec);
        }
        dir_path_ = std::exchange(other.dir_path_, {});
    }
    return *this;
}

const std::filesystem::path& ScopedTempDir::path() const noexcept {
    return dir_path_;
}

std::filesystem::path ScopedTempDir::write_file(std::string_view filename,
                                                std::span<const u8> data) const {
    const std::filesystem::path file_path = dir_path_ / filename;
    write_save_file(file_path, data);
    return file_path;
}

std::vector<u8> ScopedTempDir::read_file(std::string_view filename) const {
    return read_save_file(dir_path_ / filename);
}

std::vector<u8> build_synthetic_bios9(const BiosSpec& spec) {
    std::vector<u8> bios(kBios9Size, 0U);
    const Address reset_pc = spec.reset_pc.value_or(0x02000000U);
    write_common_vectors(bios, reset_pc, spec.include_irq_trampoline);

    if (spec.include_swi_halt) {
        write_bios_word(bios, 12U, 0xEE070F90U);
        write_bios_word(bios, 13U, kWordMovsPcLr);
    } else {
        write_bios_word(bios, 12U, kWordMovsPcLr);
    }

    if (spec.include_irq_trampoline) {
        const Address primary = spec.irq_pointer_addr.value_or(0x027FFFFCU);
        write_bios9_irq_trampoline(bios, primary, spec.fallback_irq_pointer_addr);
    }
    return bios;
}

std::vector<u8> build_synthetic_bios7(const BiosSpec& spec) {
    std::vector<u8> bios(kBios7Size, 0U);
    const Address reset_pc = spec.reset_pc.value_or(0x03800000U);
    write_common_vectors(bios, reset_pc, spec.include_irq_trampoline);

    if (spec.include_swi_halt) {
        write_bios_word(bios, 12U, 0xE3A00080U);
        write_bios_word(bios, 13U, encode_ldr_pc_rel(13U, 30U, 1U));
        write_bios_word(bios, 14U, 0xE5C10000U);
        write_bios_word(bios, 15U, kWordMovsPcLr);
        write_bios_word(bios, 30U, 0x04000301U);
    } else {
        write_bios_word(bios, 12U, kWordMovsPcLr);
    }

    if (spec.include_irq_trampoline) {
        const Address irq_ptr = spec.irq_pointer_addr.value_or(0x0380FFFCU);
        write_bios7_irq_trampoline(bios, irq_ptr);
    }
    return bios;
}

std::vector<u8> build_synthetic_firmware(const FirmwareSpec& spec) {
    const u32 size = normalize_firmware_size(spec.size_bytes);
    std::vector<u8> fw(size, 0xFFU);

    fw[0x008U] = static_cast<u8>('E');
    fw[0x009U] = static_cast<u8>('L');
    fw[0x00AU] = static_cast<u8>('M');
    fw[0x00BU] = static_cast<u8>('S');

    const u16 chip_nibble = static_cast<u16>(size / kFirmwareSize128K);
    write_le16(fw, 0x014U, static_cast<u16>((chip_nibble << 12U) | 0x0017U));
    fw[0x01DU] = spec.console_type;

    const u16 user_offset = static_cast<u16>((size - 0x200U) / 8U);
    write_le16(fw, 0x020U, user_offset);

    constexpr u16 kWifiConfigLen = 0x0138U;
    std::fill(fw.begin() + 0x02C, fw.begin() + 0x02C + kWifiConfigLen, u8{0});
    write_le16(fw, 0x02CU, kWifiConfigLen);
    std::copy(spec.mac_address.begin(), spec.mac_address.end(), fw.begin() + 0x036);
    write_le16(fw, 0x03CU, 0x3FFEU);

    u16 wifi_crc = compute_crc16(
        std::span<const u8>(fw.data() + 0x02CU, kWifiConfigLen), 0x0000U);
    if (spec.corrupt_wifi_crc) {
        wifi_crc ^= 0xFFFFU;
    }
    write_le16(fw, 0x02AU, wifi_crc);

    const std::span<u8> fw_span(fw);
    write_firmware_user_slot(fw_span.subspan(size - 0x200U, 0x100U), spec, 0U);
    write_firmware_user_slot(fw_span.subspan(size - 0x100U, 0x100U), spec, 1U);
    return fw;
}

std::size_t save_image_byte_size(SyntheticSaveKind kind) noexcept {
    constexpr std::array<std::size_t, 8> kSaveSizes{
        32U * 1024U,   512U,          8U * 1024U,   64U * 1024U,
        128U * 1024U,  256U * 1024U,  512U * 1024U, 1024U * 1024U,
    };
    const auto idx = static_cast<std::size_t>(kind);
    return (idx < kSaveSizes.size()) ? kSaveSizes[idx] : (8U * 1024U);
}

std::vector<u8> build_save_image(const SaveImageSpec& spec) {
    std::vector<u8> image(save_image_byte_size(spec.kind), spec.fill_byte);
    if (!spec.initial_patch.empty() && spec.patch_offset < image.size()) {
        const std::size_t avail = image.size() - spec.patch_offset;
        const std::size_t count = std::min(avail, spec.initial_patch.size());
        std::copy_n(spec.initial_patch.begin(),
                    count,
                    image.begin() + static_cast<std::ptrdiff_t>(spec.patch_offset));
    }
    return image;
}

void write_save_file(const std::filesystem::path& file_path, std::span<const u8> data) {
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Failed to open file for writing");
    }
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
}

std::vector<u8> read_save_file(const std::filesystem::path& file_path) {
    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in) {
        throw std::runtime_error("Failed to open file for reading");
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<u8> data(static_cast<std::size_t>(std::max<std::streamsize>(0, size)));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(data.data()), size);
    }
    return data;
}

}  // namespace elmasds::e2e
