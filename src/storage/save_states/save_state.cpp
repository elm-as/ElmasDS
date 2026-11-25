#include "storage/save_states/save_state.hpp"

#include "core/emulator.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace elmasds::storage::save_states {

namespace {

class BinaryWriter final {
public:
    void write_u8(u8 v) { buf_.push_back(v); }
    void write_u16(u16 v) {
        write_u8(static_cast<u8>(v & 0xFFU));
        write_u8(static_cast<u8>((v >> 8U) & 0xFFU));
    }
    void write_u32(u32 v) {
        write_u16(static_cast<u16>(v & 0xFFFFU));
        write_u16(static_cast<u16>((v >> 16U) & 0xFFFFU));
    }
    void write_bytes(std::span<const u8> bytes) {
        buf_.insert(buf_.end(), bytes.begin(), bytes.end());
    }

    [[nodiscard]] std::vector<u8> take() noexcept { return std::move(buf_); }

private:
    std::vector<u8> buf_;
};

class BinaryReader final {
public:
    explicit BinaryReader(std::span<const u8> data) noexcept : data_(data) {}

    [[nodiscard]] bool has_remaining(std::size_t count) const noexcept {
        return (offset_ + count) <= data_.size();
    }

    [[nodiscard]] bool read_u8(u8& out) noexcept {
        if (!has_remaining(1U)) {
            return false;
        }
        out = data_[offset_++];
        return true;
    }

    [[nodiscard]] bool read_u16(u16& out) noexcept {
        u8 b0 = 0U;
        u8 b1 = 0U;
        if (!read_u8(b0) || !read_u8(b1)) {
            return false;
        }
        out = static_cast<u16>(b0) | (static_cast<u16>(b1) << 8U);
        return true;
    }

    [[nodiscard]] bool read_u32(u32& out) noexcept {
        u16 w0 = 0U;
        u16 w1 = 0U;
        if (!read_u16(w0) || !read_u16(w1)) {
            return false;
        }
        out = static_cast<u32>(w0) | (static_cast<u32>(w1) << 16U);
        return true;
    }

    [[nodiscard]] bool read_bytes(std::span<u8> dest) noexcept {
        if (!has_remaining(dest.size())) {
            return false;
        }
        std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset_), dest.size(),
                    dest.begin());
        offset_ += dest.size();
        return true;
    }

private:
    std::span<const u8> data_;
    std::size_t offset_ = 0U;
};

void write_register_bank(BinaryWriter& w, const cpu::RegisterBank& regs) {
    for (u8 bank = 0U; bank < cpu::RegisterBank::kBankCount; ++bank) {
        const auto mode_bank = static_cast<cpu::ModeBank>(bank);
        for (u8 r = 0U; r <= 15U; ++r) {
            w.write_u32(regs.read_bank_register(static_cast<cpu::RegisterIndex>(r), mode_bank));
        }
    }
    w.write_u32(regs.cpsr().raw());
    for (std::size_t i = 0U; i < cpu::RegisterBank::kSpsrCount; ++i) {
        w.write_u32(regs.read_spsr_bank(i).raw());
    }
}

[[nodiscard]] bool read_register_bank(BinaryReader& r, cpu::RegisterBank& regs) noexcept {
    for (u8 bank = 0U; bank < cpu::RegisterBank::kBankCount; ++bank) {
        const auto mode_bank = static_cast<cpu::ModeBank>(bank);
        for (u8 reg_idx = 0U; reg_idx <= 15U; ++reg_idx) {
            u32 val = 0U;
            if (!r.read_u32(val)) {
                return false;
            }
            regs.write_bank_register(static_cast<cpu::RegisterIndex>(reg_idx), mode_bank, val);
        }
    }
    u32 cpsr_raw = 0U;
    if (!r.read_u32(cpsr_raw)) {
        return false;
    }
    regs.set_cpsr(cpu::StatusRegister{cpsr_raw});
    for (std::size_t i = 0U; i < cpu::RegisterBank::kSpsrCount; ++i) {
        u32 spsr_raw = 0U;
        if (!r.read_u32(spsr_raw)) {
            return false;
        }
        regs.write_spsr_bank(i, cpu::StatusRegister{spsr_raw});
    }
    return true;
}

[[nodiscard]] u32 current_rom_crc(const core::Emulator& emu) noexcept {
    const auto* rom = emu.cartridge().rom();
    if (rom == nullptr) {
        return 0U;
    }
    return rom->header().header_crc;
}

}  // namespace

Result<std::vector<u8>> SaveStateManager::serialize(const core::Emulator& emulator) {
    auto& emu = const_cast<core::Emulator&>(emulator);
    BinaryWriter w;

    w.write_bytes(kSaveStateMagic);
    w.write_u32(kSaveStateFormatVersion);
    w.write_u32(current_rom_crc(emu));

    // ARM9 + ARM7 CPU cores
    write_register_bank(w, emu.arm9().registers());
    w.write_u8(emu.arm9().is_halted() ? 1U : 0U);
    w.write_u32(emu.arm9().cp15().control_register());
    w.write_u32(emu.arm9().cp15().dtcm_register());
    w.write_u32(emu.arm9().cp15().itcm_register());
    w.write_bytes(emu.arm9().tcm().itcm_data());
    w.write_bytes(emu.arm9().tcm().dtcm_data());

    write_register_bank(w, emu.arm7().registers());
    w.write_u8(emu.arm7().is_halted() ? 1U : 0U);

    // Physical memory regions
    auto& mem = emu.memory();
    w.write_bytes(mem.main_ram().data());
    w.write_u8(mem.shared_wram().wram_cnt());
    w.write_bytes(mem.shared_wram().data());
    w.write_bytes(mem.arm7_wram().data());
    for (std::size_t i = 0U; i < memory::VramController::kBankCount; ++i) {
        const auto bank = static_cast<memory::VramBankId>(i);
        w.write_u8(mem.vram().read_vram_cnt(bank));
        w.write_bytes(mem.vram().bank_data(bank));
    }
    w.write_bytes(mem.palette().data());
    w.write_bytes(mem.oam().data());

    // Keypad & Audio & GPU PowCnt1 / DispCnt
    w.write_u16(emu.keypad().keyinput());
    w.write_u16(emu.keypad().extkeyin());
    w.write_u16(emu.keypad().arm9_keycnt());
    w.write_u16(emu.keypad().arm7_keycnt());
    w.write_u16(emu.audio().soundcnt());
    w.write_u16(emu.audio().soundbias());
    w.write_u16(emu.gpu().powcnt1().value());
    w.write_u32(emu.gpu().regs_a().dispcnt());
    w.write_u32(emu.gpu().regs_b().dispcnt());

    return Result<std::vector<u8>>::ok(w.take());
}

Status SaveStateManager::deserialize(core::Emulator& emulator, std::span<const u8> state_bytes) {
    BinaryReader r(state_bytes);
    std::array<u8, 8U> magic{};
    if (!r.read_bytes(magic) || magic != kSaveStateMagic) {
        return Status::err(ErrorCode::CorruptData, "Invalid save state magic header.");
    }

    u32 version = 0U;
    u32 rom_crc = 0U;
    if (!r.read_u32(version) || !r.read_u32(rom_crc)) {
        return Status::err(ErrorCode::CorruptData, "Truncated save state header.");
    }
    if (version != kSaveStateFormatVersion) {
        return Status::err(ErrorCode::UnsupportedFormat, "Unsupported save state format version.");
    }
    if (rom_crc != current_rom_crc(emulator)) {
        return Status::err(ErrorCode::InvalidArgument,
                           "Save state ROM CRC does not match currently loaded ROM.");
    }

    if (!read_register_bank(r, emulator.arm9().registers())) {
        return Status::err(ErrorCode::CorruptData, "Truncated ARM9 register state.");
    }
    u8 arm9_halted = 0U;
    u32 cp15_ctrl = 0U;
    u32 cp15_dtcm = 0U;
    u32 cp15_itcm = 0U;
    if (!r.read_u8(arm9_halted) || !r.read_u32(cp15_ctrl) || !r.read_u32(cp15_dtcm) ||
        !r.read_u32(cp15_itcm)) {
        return Status::err(ErrorCode::CorruptData, "Truncated ARM9 CP15 state.");
    }
    emulator.arm9().context().halted = (arm9_halted != 0U);
    emulator.arm9().cp15().restore_registers(cp15_ctrl, cp15_dtcm, cp15_itcm);
    emulator.arm9().context().high_vectors = emulator.arm9().cp15().high_vectors();
    if (!r.read_bytes(emulator.arm9().tcm().mutable_itcm_data()) ||
        !r.read_bytes(emulator.arm9().tcm().mutable_dtcm_data())) {
        return Status::err(ErrorCode::CorruptData, "Truncated ARM9 TCM state.");
    }

    if (!read_register_bank(r, emulator.arm7().registers())) {
        return Status::err(ErrorCode::CorruptData, "Truncated ARM7 register state.");
    }
    u8 arm7_halted = 0U;
    if (!r.read_u8(arm7_halted)) {
        return Status::err(ErrorCode::CorruptData, "Truncated ARM7 halt state.");
    }
    emulator.arm7().set_halted(arm7_halted != 0U);

    auto& mem = emulator.memory();
    u8 wram_cnt = 0U;
    if (!r.read_bytes(mem.main_ram().mutable_data()) || !r.read_u8(wram_cnt) ||
        !r.read_bytes(mem.shared_wram().mutable_data()) ||
        !r.read_bytes(mem.arm7_wram().mutable_data())) {
        return Status::err(ErrorCode::CorruptData, "Truncated RAM/WRAM state.");
    }
    mem.shared_wram().set_wram_cnt(wram_cnt);

    for (std::size_t i = 0U; i < memory::VramController::kBankCount; ++i) {
        const auto bank = static_cast<memory::VramBankId>(i);
        u8 vcnt = 0U;
        if (!r.read_u8(vcnt) || !r.read_bytes(mem.vram().mutable_bank_data(bank))) {
            return Status::err(ErrorCode::CorruptData, "Truncated VRAM state.");
        }
        mem.vram().write_vram_cnt(bank, vcnt);
    }
    if (!r.read_bytes(mem.palette().mutable_data()) || !r.read_bytes(mem.oam().mutable_data())) {
        return Status::err(ErrorCode::CorruptData, "Truncated Palette/OAM state.");
    }

    u16 keyin = 0U;
    u16 extkey = 0U;
    u16 kcnt9 = 0U;
    u16 kcnt7 = 0U;
    u16 sndcnt = 0U;
    u16 sndbias = 0U;
    u16 powcnt1 = 0U;
    u32 dispcnt_a = 0U;
    u32 dispcnt_b = 0U;
    if (!r.read_u16(keyin) || !r.read_u16(extkey) || !r.read_u16(kcnt9) || !r.read_u16(kcnt7) ||
        !r.read_u16(sndcnt) || !r.read_u16(sndbias) || !r.read_u16(powcnt1) ||
        !r.read_u32(dispcnt_a) || !r.read_u32(dispcnt_b)) {
        return Status::err(ErrorCode::CorruptData, "Truncated peripheral state.");
    }
    emulator.keypad().restore_state(keyin, extkey, kcnt9, kcnt7);
    emulator.audio().restore_state(sndcnt, sndbias, emulator.audio().channels());
    emulator.gpu().powcnt1().set_value(powcnt1);
    emulator.gpu().regs_a().write32(gpu::kEngineABase, dispcnt_a);
    emulator.gpu().regs_b().write32(gpu::kEngineBBase, dispcnt_b);

    return Status::ok();
}

Status SaveStateManager::save_to_file(const core::Emulator& emulator,
                                      const std::filesystem::path& file_path) {
    auto state = serialize(emulator);
    if (state.is_err()) {
        return Status::err(state.error().code, state.error().message);
    }
    std::ofstream out(file_path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return Status::err(ErrorCode::IoFailure, "Failed to open save state file for write.");
    }
    out.write(reinterpret_cast<const char*>(state.value().data()),
              static_cast<std::streamsize>(state.value().size()));
    return out.good() ? Status::ok()
                      : Status::err(ErrorCode::IoFailure, "Failed writing save state file.");
}

Status SaveStateManager::load_from_file(core::Emulator& emulator,
                                        const std::filesystem::path& file_path) {
    std::ifstream in(file_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return Status::err(ErrorCode::FileNotFound, "Save state file not found.");
    }
    const auto size = in.tellg();
    if (size <= 0) {
        return Status::err(ErrorCode::CorruptData, "Save state file is empty.");
    }
    std::vector<u8> bytes(static_cast<std::size_t>(size));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!in) {
        return Status::err(ErrorCode::IoFailure, "Failed reading save state file.");
    }
    return deserialize(emulator, bytes);
}

}  // namespace elmasds::storage::save_states
