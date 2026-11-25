#pragma once

#include "core/emulator.hpp"
#include "primitives/diagnostic_log.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"

#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace elmasds::e2e {

inline constexpr TickCount kMasterTicksPerFrame = 560'190ULL;

struct E2eConfig {
    LogLevel log_level = LogLevel::Warning;
    std::string bios9_path;
    std::string bios7_path;
    std::string firmware_path;
};

struct TouchInputState {
    u16 x = 0U;
    u16 y = 0U;
    bool pressed = false;
};

struct RtcDateTime {
    u8 year = 26U;
    u8 month = 11U;
    u8 day = 25U;
    u8 weekday = 3U;
    u8 hour = 12U;
    u8 minute = 0U;
    u8 second = 0U;
};

/**
 * @brief Opaque-box E2E test harness wrapping `elmasds::core::Emulator`.
 */
class EmulatorE2eHarness {
public:
    explicit EmulatorE2eHarness(const E2eConfig& config = {});
    ~EmulatorE2eHarness();

    EmulatorE2eHarness(const EmulatorE2eHarness&) = delete;
    EmulatorE2eHarness& operator=(const EmulatorE2eHarness&) = delete;

    void reset() noexcept;

    [[nodiscard]] Status load_rom_bytes(std::span<const u8> rom_bytes);
    [[nodiscard]] Status load_rom_file(const std::filesystem::path& rom_path);
    [[nodiscard]] Status load_bios9(std::span<const u8> bios9);
    [[nodiscard]] Status load_bios7(std::span<const u8> bios7);
    [[nodiscard]] Status load_firmware(std::span<const u8> firmware);
    [[nodiscard]] Status attach_save(const std::filesystem::path& save_path, u32 type_hint = 0U);
    [[nodiscard]] Status flush_save();
    [[nodiscard]] Status direct_boot();

    void step_ticks(TickCount ticks);
    void step_frames(u32 frame_count);
    void trigger_vblank();
    void trigger_hblank();
    void trigger_gxfifo();
    void set_touch_state(const TouchInputState& state);
    void set_rtc_datetime(const RtcDateTime& datetime);

    [[nodiscard]] TickCount current_tick() const noexcept;
    [[nodiscard]] u32 read_main_ram32(Address addr) const;
    [[nodiscard]] u16 read_main_ram16(Address addr) const;
    [[nodiscard]] u8 read_main_ram8(Address addr) const;
    void write_main_ram32(Address addr, u32 value);
    [[nodiscard]] u32 read_shared_wram32(Address addr) const;

    [[nodiscard]] Address arm9_pc() const noexcept;
    [[nodiscard]] Address arm7_pc() const noexcept;
    [[nodiscard]] bool is_arm9_halted() const noexcept;
    [[nodiscard]] bool is_arm7_halted() const noexcept;

private:
    std::unique_ptr<core::Emulator> emulator_;
};

}  // namespace elmasds::e2e
