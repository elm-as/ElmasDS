#include "e2e/emulator_e2e_harness.hpp"

namespace elmasds::e2e {
namespace {

[[nodiscard]] core::EmulatorConfig to_emulator_config(const E2eConfig& config) {
    core::EmulatorConfig emu_config{};
    emu_config.log_level = config.log_level;
    emu_config.bios9_path = config.bios9_path;
    emu_config.bios7_path = config.bios7_path;
    emu_config.firmware_path = config.firmware_path;
    return emu_config;
}

}  // namespace

EmulatorE2eHarness::EmulatorE2eHarness(const E2eConfig& config)
    : emulator_(std::make_unique<core::Emulator>(to_emulator_config(config))) {}

EmulatorE2eHarness::~EmulatorE2eHarness() = default;

void EmulatorE2eHarness::reset() noexcept {
    emulator_->reset();
}

Status EmulatorE2eHarness::load_rom_bytes(std::span<const u8> rom_bytes) {
    return emulator_->load_rom_bytes(rom_bytes);
}

Status EmulatorE2eHarness::load_rom_file(const std::filesystem::path& rom_path) {
    return emulator_->load_rom_file(rom_path);
}

Status EmulatorE2eHarness::load_bios9(std::span<const u8> bios9) {
    return emulator_->load_bios9(bios9);
}

Status EmulatorE2eHarness::load_bios7(std::span<const u8> bios7) {
    return emulator_->load_bios7(bios7);
}

Status EmulatorE2eHarness::load_firmware(std::span<const u8> firmware) {
    return emulator_->load_firmware(firmware);
}

Status EmulatorE2eHarness::attach_save(const std::filesystem::path& save_path, u32 type_hint) {
    return emulator_->attach_save(save_path, type_hint);
}

Status EmulatorE2eHarness::flush_save() {
    return emulator_->flush_save();
}

Status EmulatorE2eHarness::direct_boot() {
    return emulator_->direct_boot();
}

void EmulatorE2eHarness::step_ticks(TickCount ticks) {
    emulator_->step_ticks(ticks);
}

void EmulatorE2eHarness::step_frames(u32 frame_count) {
    emulator_->step_ticks(static_cast<TickCount>(frame_count) * kMasterTicksPerFrame);
}

void EmulatorE2eHarness::trigger_vblank() {
    emulator_->trigger_vblank();
}

void EmulatorE2eHarness::trigger_hblank() {
    emulator_->trigger_hblank();
}

void EmulatorE2eHarness::trigger_gxfifo() {
    emulator_->trigger_gxfifo();
}

void EmulatorE2eHarness::set_touch_state(const TouchInputState& state) {
    emulator_->set_touch_state(state.x, state.y, state.pressed);
}

void EmulatorE2eHarness::set_rtc_datetime(const RtcDateTime& datetime) {
    emulator_->set_rtc_datetime(hardware::rtc::RtcDateTime{
        .year = datetime.year,
        .month = datetime.month,
        .day = datetime.day,
        .weekday = datetime.weekday,
        .hour = datetime.hour,
        .minute = datetime.minute,
        .second = datetime.second,
    });
}

TickCount EmulatorE2eHarness::current_tick() const noexcept {
    return emulator_->current_tick();
}

u32 EmulatorE2eHarness::read_main_ram32(Address addr) const {
    return emulator_->read_main_ram32(addr);
}

u16 EmulatorE2eHarness::read_main_ram16(Address addr) const {
    return emulator_->read_main_ram16(addr);
}

u8 EmulatorE2eHarness::read_main_ram8(Address addr) const {
    return emulator_->read_main_ram8(addr);
}

void EmulatorE2eHarness::write_main_ram32(Address addr, u32 value) {
    emulator_->write_main_ram32(addr, value);
}

u32 EmulatorE2eHarness::read_shared_wram32(Address addr) const {
    return emulator_->read_shared_wram32(addr);
}

Address EmulatorE2eHarness::arm9_pc() const noexcept {
    return emulator_->arm9_pc();
}

Address EmulatorE2eHarness::arm7_pc() const noexcept {
    return emulator_->arm7_pc();
}

bool EmulatorE2eHarness::is_arm9_halted() const noexcept {
    return emulator_->is_arm9_halted();
}

bool EmulatorE2eHarness::is_arm7_halted() const noexcept {
    return emulator_->is_arm7_halted();
}

}  // namespace elmasds::e2e
