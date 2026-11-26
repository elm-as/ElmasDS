#pragma once

#include "audio/audio_engine.hpp"
#include "core/scheduler/event_scheduler.hpp"
#include "cpu/arm7/arm7_tdmi.hpp"
#include "cpu/arm9/arm946_es.hpp"
#include "cpu/bios_hle/hle_bios.hpp"
#include "gpu/gpu.hpp"
#include "hardware/cartridge/cartridge_controller.hpp"
#include "hardware/dma/dma_controller.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/system_control_mmio.hpp"
#include "hardware/ipc/ipc_controller.hpp"
#include "hardware/math/math_coprocessor.hpp"
#include "hardware/rtc/rtc_controller.hpp"
#include "hardware/spi/spi_controller.hpp"
#include "hardware/timers/timer_controller.hpp"
#include "input/input_mapping.hpp"
#include "input/keypad_mmio.hpp"
#include "memory/memory_bus/arm7_memory_bus.hpp"
#include "memory/memory_bus/arm9_memory_bus.hpp"
#include "memory/memory_bus/memory_system.hpp"
#include "primitives/diagnostic_log.hpp"
#include "primitives/fixed_width_types.hpp"
#include "primitives/status_result.hpp"
#include "storage/save_states/save_state.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace elmasds::core {

/**
 * @brief Startup configuration passed to an Emulator instance.
 */
struct EmulatorConfig {
    LogLevel log_level = LogLevel::Info;
    std::string bios9_path;
    std::string bios7_path;
    std::string firmware_path;
};

/**
 * @brief Top-level emulation coordinator owning hardware subsystems and timeline.
 *
 * Responsibility:
 *   Coordinates ARM946E-S, ARM7TDMI, MemorySystem, Interrupts, Timers, DMA, IPC, SPI, RTC,
 *   Cartridge/Save, and direct `.nds` boot through a single deterministic EventScheduler
 *   without depending on frontend libraries (SDL3/OpenGL).
 */
class Emulator {
public:
    explicit Emulator(EmulatorConfig config = {});

    Emulator(const Emulator&) = delete;
    Emulator& operator=(const Emulator&) = delete;
    Emulator(Emulator&&) = delete;
    Emulator& operator=(Emulator&&) = delete;

    void reset() noexcept;
    void step_ticks(TickCount delta_ticks);
    void run_frame();

    [[nodiscard]] Status load_rom_bytes(std::span<const u8> rom_bytes);
    [[nodiscard]] Status load_rom_file(const std::filesystem::path& rom_path);
    [[nodiscard]] Status load_bios9(std::span<const u8> bios9);
    [[nodiscard]] Status load_bios7(std::span<const u8> bios7);
    [[nodiscard]] Status load_firmware(std::span<const u8> firmware);
    [[nodiscard]] Status attach_save(const std::filesystem::path& save_path, u32 type_hint = 0U);
    [[nodiscard]] Status flush_save();
    [[nodiscard]] Status direct_boot();
    [[nodiscard]] Result<std::vector<u8>> save_state() const {
        return storage::save_states::SaveStateManager::serialize(*this);
    }
    [[nodiscard]] Status load_state(std::span<const u8> state_bytes) {
        return storage::save_states::SaveStateManager::deserialize(*this, state_bytes);
    }
    [[nodiscard]] Status save_state_to_file(const std::filesystem::path& path) const {
        return storage::save_states::SaveStateManager::save_to_file(*this, path);
    }
    [[nodiscard]] Status load_state_from_file(const std::filesystem::path& path) {
        return storage::save_states::SaveStateManager::load_from_file(*this, path);
    }

    void trigger_vblank();
    void trigger_hblank();
    void trigger_gxfifo();
    void set_button_pressed(input::DsButton button, bool pressed) noexcept {
        keypad_.set_button_pressed(button, pressed);
    }
    void set_touch_state(u16 x, u16 y, bool pressed) noexcept;
    void set_touch_pixel(u16 pixel_x, u16 pixel_y, bool pressed) noexcept;
    void set_rtc_datetime(const hardware::rtc::RtcDateTime& datetime) noexcept;

    [[nodiscard]] TickCount current_tick() const noexcept { return scheduler_.current_tick(); }
    [[nodiscard]] const EmulatorConfig& config() const noexcept { return config_; }
    [[nodiscard]] EventScheduler& scheduler() noexcept { return scheduler_; }
    [[nodiscard]] const EventScheduler& scheduler() const noexcept { return scheduler_; }
    [[nodiscard]] DiagnosticLog& diagnostics() noexcept { return diagnostics_; }
    [[nodiscard]] const DiagnosticLog& diagnostics() const noexcept { return diagnostics_; }

    [[nodiscard]] u32 read_main_ram32(Address addr) const noexcept {
        return memory_.main_ram().read32(addr & 0x003FFFFFU);
    }
    [[nodiscard]] u16 read_main_ram16(Address addr) const noexcept {
        return memory_.main_ram().read16(addr & 0x003FFFFFU);
    }
    [[nodiscard]] u8 read_main_ram8(Address addr) const noexcept {
        return memory_.main_ram().read8(addr & 0x003FFFFFU);
    }
    void write_main_ram32(Address addr, u32 value) noexcept {
        memory_.main_ram().write32(addr & 0x003FFFFFU, value);
    }
    [[nodiscard]] u32 read_shared_wram32(Address addr) const noexcept {
        return memory_.shared_wram().read_arm9_32(addr & 0x00007FFFU);
    }

    [[nodiscard]] Address arm9_pc() const noexcept {
        return arm9_.registers().read(cpu::RegisterIndex::Pc);
    }
    [[nodiscard]] Address arm7_pc() const noexcept {
        return arm7_.registers().read(cpu::RegisterIndex::Pc);
    }
    [[nodiscard]] bool is_arm9_halted() const noexcept { return arm9_.is_halted(); }
    [[nodiscard]] bool is_arm7_halted() const noexcept { return arm7_.is_halted(); }

    [[nodiscard]] std::span<const u32> top_screen() const noexcept {
        return gpu_.top_framebuffer();
    }
    [[nodiscard]] std::span<const u32> bottom_screen() const noexcept {
        return gpu_.bottom_framebuffer();
    }
    [[nodiscard]] u64 frame_count() const noexcept { return gpu_.frame_count(); }

    [[nodiscard]] cpu::Arm946Es& arm9() noexcept { return arm9_; }
    [[nodiscard]] cpu::Arm7Tdmi& arm7() noexcept { return arm7_; }
    [[nodiscard]] memory::MemorySystem& memory() noexcept { return memory_; }
    [[nodiscard]] gpu::Gpu& gpu() noexcept { return gpu_; }
    [[nodiscard]] const gpu::Gpu& gpu() const noexcept { return gpu_; }
    [[nodiscard]] input::KeypadController& keypad() noexcept { return keypad_; }
    [[nodiscard]] const input::KeypadController& keypad() const noexcept { return keypad_; }
    [[nodiscard]] audio::AudioEngine& audio() noexcept { return audio_; }
    [[nodiscard]] const audio::AudioEngine& audio() const noexcept { return audio_; }
    [[nodiscard]] hardware::cartridge::CartridgeController& cartridge() noexcept {
        return cartridge_;
    }
    [[nodiscard]] const hardware::cartridge::CartridgeController& cartridge() const noexcept {
        return cartridge_;
    }

private:
    using ImageLoader = Status (Emulator::*)(std::span<const u8>);

    /// Describes one optional user-provided BIOS/firmware file declared in EmulatorConfig.
    struct SystemImageRequest {
        std::string_view name;
        const std::string& path;
        std::size_t expected_size = 0U;
        ImageLoader loader = nullptr;
    };

    void register_mmio_devices();
    void load_configured_system_images();
    void populate_post_bios_memory(const storage::rom_loader::NdsRom& rom);
    [[nodiscard]] Status load_system_image_file(const SystemImageRequest& request);
    void report_load_status(std::string_view image_name, const Status& status);
    void step_cpu_slice(TickCount slice_ticks);

    EmulatorConfig config_;
    DiagnosticLog diagnostics_;
    EventScheduler scheduler_;

    memory::MemorySystem memory_;
    memory::Arm9MemoryBus arm9_bus_;
    memory::Arm7MemoryBus arm7_bus_;
    cpu::Arm946Es arm9_;
    cpu::Arm7Tdmi arm7_;

    hardware::interrupts::InterruptController arm9_irq_;
    hardware::interrupts::InterruptController arm7_irq_;
    hardware::interrupts::SystemControlMmio arm9_sys_ctrl_;
    hardware::interrupts::SystemControlMmio arm7_sys_ctrl_;
    hardware::timers::TimerController arm9_timers_;
    hardware::timers::TimerController arm7_timers_;
    hardware::dma::DmaController arm9_dma_;
    hardware::dma::DmaController arm7_dma_;
    hardware::ipc::IpcController ipc_;
    hardware::math::MathCoprocessor arm9_math_;
    hardware::spi::SpiController arm7_spi_;
    hardware::rtc::RtcController arm7_rtc_;
    hardware::cartridge::CartridgeController cartridge_;
    gpu::Gpu gpu_;
    input::KeypadController keypad_;
    audio::AudioEngine audio_;
    cpu::HleBios arm9_hle_bios_;
    cpu::HleBios arm7_hle_bios_;

    bool firmware_loaded_ = false;
    bool cpu_execution_enabled_ = false;
    TickCount arm9_cycle_debt_ = 0U;
    TickCount arm7_cycle_debt_ = 0U;
};

}  // namespace elmasds::core
