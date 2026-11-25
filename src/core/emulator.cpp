#include "core/emulator.hpp"

#include "cpu/arm_architecture/cpu_mode.hpp"
#include "storage/firmware/system_image_loader.hpp"
#include "storage/rom_loader/nds_rom.hpp"
#include "storage/save_manager/save_memory.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace elmasds::core {

namespace {

constexpr TickCount kMaxCpuSliceTicks = 32ULL;
constexpr TickCount kArm7TicksPerCycle = 2ULL;
constexpr Address kBootHeaderRamAddress = 0x027FFE00U;
constexpr std::size_t kBootHeaderCopySize = 0x170U;

void write_bytes_to_bus(cpu::CpuBusInterface& bus, Address dest, std::span<const u8> bytes) {
    for (std::size_t i = 0U; i < bytes.size(); ++i) {
        bus.write8(static_cast<Address>(dest + i), bytes[i]);
    }
}

void configure_direct_boot_stacks(cpu::RegisterBank& regs, Address entry_pc,
                                  const std::array<Address, 3U>& stacks) noexcept {
    regs.switch_mode(cpu::CpuMode::Irq);
    regs.write(cpu::RegisterIndex::Sp, stacks[1U]);
    regs.switch_mode(cpu::CpuMode::Supervisor);
    regs.write(cpu::RegisterIndex::Sp, stacks[2U]);
    regs.switch_mode(cpu::CpuMode::System);
    regs.write(cpu::RegisterIndex::Sp, stacks[0U]);
    regs.write(cpu::RegisterIndex::R12, entry_pc);
    regs.write(cpu::RegisterIndex::Lr, entry_pc);
    regs.write(cpu::RegisterIndex::Pc, entry_pc);
    regs.set_cpsr(cpu::StatusRegister{0x000000DFU});  // System mode, I=1, F=1, ARM state.
}

}  // namespace

Emulator::Emulator(EmulatorConfig config)
    : config_(std::move(config)),
      diagnostics_(config_.log_level),
      arm9_bus_(memory_),
      arm7_bus_(memory_),
      arm9_(arm9_bus_),
      arm7_(arm7_bus_),
      arm9_irq_(hardware::interrupts::CpuKind::Arm9,
                hardware::interrupts::make_interrupt_target(&arm9_)),
      arm7_irq_(hardware::interrupts::CpuKind::Arm7,
                hardware::interrupts::make_interrupt_target(&arm7_)),
      arm9_sys_ctrl_(hardware::interrupts::CpuKind::Arm9),
      arm7_sys_ctrl_(hardware::interrupts::CpuKind::Arm7,
                     hardware::interrupts::make_halt_handler(&arm7_)),
      arm9_timers_(hardware::interrupts::CpuKind::Arm9, scheduler_, arm9_irq_),
      arm7_timers_(hardware::interrupts::CpuKind::Arm7, scheduler_, arm7_irq_),
      arm9_dma_(hardware::interrupts::CpuKind::Arm9, arm9_bus_, arm9_irq_),
      arm7_dma_(hardware::interrupts::CpuKind::Arm7, arm7_bus_, arm7_irq_),
      ipc_(arm9_irq_, arm7_irq_),
      arm7_spi_(&arm7_irq_),
      cartridge_(hardware::cartridge::CartridgeConfig{&arm9_irq_, &arm7_irq_, &arm9_dma_,
                                                      &arm7_dma_}),
      gpu_(gpu::GpuDependencies{scheduler_, memory_, arm9_irq_, arm7_irq_, arm9_dma_, arm7_dma_}),
      keypad_(arm9_irq_, arm7_irq_),
      audio_(scheduler_, arm7_bus_),
      arm9_hle_bios_(hardware::interrupts::CpuKind::Arm9, arm9_irq_),
      arm7_hle_bios_(hardware::interrupts::CpuKind::Arm7, arm7_irq_) {
    arm9_.context().swi_handler = arm9_hle_bios_.make_swi_handler();
    arm7_.context().swi_handler = arm7_hle_bios_.make_swi_handler();
    register_mmio_devices();
    load_configured_system_images();
    diagnostics_.info("Emulator", "Core initialized with ARM9/ARM7 and Phase 3-6 subsystems.");
}

void Emulator::register_mmio_devices() {
    auto& mmio9 = memory_.arm9_mmio();
    auto& mmio7 = memory_.arm7_mmio();

    gpu_.register_mmio(mmio9, mmio7);

    mmio9.register_device({hardware::dma::kDmaBaseAddress, hardware::dma::kDmaFillEndAddress,
                           &arm9_dma_});
    mmio9.register_device({hardware::timers::kTimerBaseAddress, hardware::timers::kTimerEndAddress,
                           &arm9_timers_});
    mmio9.register_device({hardware::cartridge::kCartMmioStart, hardware::cartridge::kCartMmioEnd,
                           &cartridge_.arm9_mmio()});
    mmio9.register_device({hardware::cartridge::kRegRomData, hardware::cartridge::kRegRomDataEnd,
                           &cartridge_.arm9_mmio()});
    mmio9.register_device({hardware::cartridge::kRegExMemCnt, hardware::cartridge::kRegExMemCntEnd,
                           &cartridge_.arm9_mmio()});
    mmio9.register_device({hardware::interrupts::InterruptController::kMmioStart,
                           hardware::interrupts::InterruptController::kMmioEnd, &arm9_irq_});
    mmio9.register_device({hardware::interrupts::SystemControlMmio::kMmioStart,
                           hardware::interrupts::SystemControlMmio::kMmioEnd, &arm9_sys_ctrl_});
    mmio9.register_device({input::kKeypadMmioStart, input::kKeypadMmioEnd, &keypad_.arm9_mmio()});

    mmio7.register_device({hardware::dma::kDmaBaseAddress, hardware::dma::kDmaChannelEndAddress,
                           &arm7_dma_});
    mmio7.register_device({hardware::timers::kTimerBaseAddress, hardware::timers::kTimerEndAddress,
                           &arm7_timers_});
    mmio7.register_device({hardware::rtc::kRtcMmioStart, hardware::rtc::kRtcMmioEnd, &arm7_rtc_});
    mmio7.register_device({hardware::cartridge::kCartMmioStart, hardware::cartridge::kCartMmioEnd,
                           &cartridge_.arm7_mmio()});
    mmio7.register_device({hardware::cartridge::kRegRomData, hardware::cartridge::kRegRomDataEnd,
                           &cartridge_.arm7_mmio()});
    mmio7.register_device({hardware::cartridge::kRegExMemCnt, hardware::cartridge::kRegExMemCntEnd,
                           &cartridge_.arm7_mmio()});
    mmio7.register_device({hardware::spi::kSpiMmioStart, hardware::spi::kSpiMmioEnd, &arm7_spi_});
    mmio7.register_device({hardware::interrupts::InterruptController::kMmioStart,
                           hardware::interrupts::InterruptController::kMmioEnd, &arm7_irq_});
    mmio7.register_device({hardware::interrupts::SystemControlMmio::kMmioStart,
                           hardware::interrupts::SystemControlMmio::kMmioEnd, &arm7_sys_ctrl_});
    mmio7.register_device({input::kKeypadMmioStart, input::kKeypadMmioEnd, &keypad_.arm7_mmio()});
    mmio7.register_device({audio::kAudioMmioStart, audio::kAudioMmioEnd, &audio_});

    ipc_.register_mmio(mmio9, mmio7);
}

void Emulator::report_load_status(std::string_view image_name, const Status& status) {
    if (status.is_ok()) {
        diagnostics_.info("Emulator", std::string(image_name) + " loaded.");
        return;
    }
    diagnostics_.error("Emulator",
                       std::string(image_name) + " load failed: " + status.error().message);
}

Status Emulator::load_system_image_file(const SystemImageRequest& request) {
    auto file = storage::firmware::load_binary_file(request.path, request.expected_size);
    if (file.is_err()) {
        return Status::err(file.error().code, file.error().message);
    }
    return (this->*request.loader)(file.value());
}

void Emulator::load_configured_system_images() {
    const std::array<SystemImageRequest, 3U> requests = {{
        {"BIOS9", config_.bios9_path, storage::firmware::kArm9BiosSize, &Emulator::load_bios9},
        {"BIOS7", config_.bios7_path, storage::firmware::kArm7BiosSize, &Emulator::load_bios7},
        {"Firmware", config_.firmware_path, 0U, &Emulator::load_firmware},
    }};
    for (const auto& request : requests) {
        if (request.path.empty()) {
            continue;
        }
        report_load_status(request.name, load_system_image_file(request));
    }
}

void Emulator::reset() noexcept {
    scheduler_.reset();
    memory_.reset();
    arm9_irq_.reset();
    arm7_irq_.reset();
    arm9_sys_ctrl_.reset();
    arm7_sys_ctrl_.reset();
    arm9_timers_.reset();
    arm7_timers_.reset();
    arm9_dma_.reset();
    arm7_dma_.reset();
    ipc_.reset();
    arm7_spi_.reset();
    arm7_rtc_.reset();
    cartridge_.reset();
    gpu_.reset();
    keypad_.reset();
    audio_.reset();
    arm9_.reset(0xFFFF0000U, true);
    arm7_.reset(0x00000000U);
    arm9_.context().swi_handler = arm9_hle_bios_.make_swi_handler();
    arm7_.context().swi_handler = arm7_hle_bios_.make_swi_handler();
    arm9_hle_bios_.set_enabled(!memory_.bios9().is_loaded());
    arm7_hle_bios_.set_enabled(!memory_.bios7().is_loaded());
    arm9_cycle_debt_ = 0U;
    arm7_cycle_debt_ = 0U;
    cpu_execution_enabled_ = memory_.bios9().is_loaded() && memory_.bios7().is_loaded();
    diagnostics_.info("Emulator", "Emulator state reset.");
}

Status Emulator::load_rom_bytes(std::span<const u8> rom_bytes) {
    auto loaded = storage::rom_loader::NdsRom::load_from_bytes(rom_bytes);
    if (loaded.is_err()) {
        return Status::err(loaded.error().code, loaded.error().message);
    }
    cartridge_.insert_rom(std::move(loaded.value()));
    return Status::ok();
}

Status Emulator::load_rom_file(const std::filesystem::path& rom_path) {
    auto loaded = storage::rom_loader::NdsRom::load_from_file(rom_path);
    if (loaded.is_err()) {
        return Status::err(loaded.error().code, loaded.error().message);
    }
    cartridge_.insert_rom(std::move(loaded.value()));
    return Status::ok();
}

Status Emulator::load_bios9(std::span<const u8> bios9) {
    const Status s = storage::firmware::load_bios9_image(memory_.bios9(), bios9);
    if (s.is_ok()) {
        arm9_hle_bios_.set_enabled(false);
    }
    return s;
}

Status Emulator::load_bios7(std::span<const u8> bios7) {
    const Status s = storage::firmware::load_bios7_image(memory_.bios7(), bios7);
    if (s.is_ok()) {
        arm7_hle_bios_.set_enabled(false);
    }
    return s;
}

Status Emulator::load_firmware(std::span<const u8> firmware) {
    Status status = storage::firmware::load_firmware_image(arm7_spi_.firmware(), firmware);
    if (status.is_ok()) {
        firmware_loaded_ = true;
    }
    return status;
}

Status Emulator::attach_save(const std::filesystem::path& save_path, u32 type_hint) {
    const auto hint = static_cast<storage::save_manager::SaveType>(type_hint);
    return cartridge_.save_memory().attach_file(save_path, hint);
}

Status Emulator::flush_save() {
    return cartridge_.save_memory().flush_if_dirty();
}

Status Emulator::direct_boot() {
    const storage::rom_loader::NdsRom* rom = cartridge_.rom();
    if (rom == nullptr) {
        return Status::err(ErrorCode::NotInitialized,
                           "Cannot direct boot before an NDS ROM is loaded.");
    }

    reset();
    if (!firmware_loaded_) {
        arm7_spi_.firmware().load_image(storage::firmware::create_default_firmware_image());
    }

    // Standard post-BIOS Shared WRAM mapping (all 32 KiB assigned to ARM7).
    memory_.shared_wram().set_wram_cnt(0x03U);

    const auto& hdr = rom->header();
    write_bytes_to_bus(arm9_bus_, hdr.arm9_ram_address, rom->arm9_binary());
    write_bytes_to_bus(arm7_bus_, hdr.arm7_ram_address, rom->arm7_binary());

    const std::size_t header_copy = std::min<std::size_t>(kBootHeaderCopySize, rom->bytes().size());
    write_bytes_to_bus(arm9_bus_, kBootHeaderRamAddress, rom->bytes().subspan(0U, header_copy));

    arm9_sys_ctrl_.write8(hardware::interrupts::SystemControlMmio::kRegPostFlg, 1U);
    arm7_sys_ctrl_.write8(hardware::interrupts::SystemControlMmio::kRegPostFlg, 1U);

    arm9_.reset(hdr.arm9_entry_address, true);
    configure_direct_boot_stacks(arm9_.registers(), hdr.arm9_entry_address,
                                 {0x03002F7CU, 0x03003F80U, 0x03003FC0U});

    arm7_.reset(hdr.arm7_entry_address);
    configure_direct_boot_stacks(arm7_.registers(), hdr.arm7_entry_address,
                                 {0x0380FD80U, 0x0380FF80U, 0x0380FFC0U});

    cpu_execution_enabled_ = true;
    diagnostics_.info("Emulator", "Direct boot initialized for ROM: " + hdr.title);
    return Status::ok();
}

void Emulator::step_ticks(TickCount delta_ticks) {
    if (!cpu_execution_enabled_ || delta_ticks == 0U) {
        scheduler_.advance_by(delta_ticks);
        return;
    }

    TickCount remaining = delta_ticks;
    while (remaining > 0U) {
        TickCount slice = std::min(remaining, kMaxCpuSliceTicks);
        if (const auto next_tick = scheduler_.next_event_tick(); next_tick.has_value()) {
            const TickCount now = scheduler_.current_tick();
            const TickCount until_event = (*next_tick > now) ? (*next_tick - now) : 1ULL;
            slice = std::min(slice, until_event);
        }
        step_cpu_slice(slice);
        scheduler_.advance_by(slice);
        remaining -= slice;
    }
}

void Emulator::step_cpu_slice(TickCount slice_ticks) {
    arm9_cycle_debt_ += slice_ticks;
    while (arm9_cycle_debt_ > 0U) {
        const u32 cycles = arm9_.step();
        arm9_cycle_debt_ = (cycles >= arm9_cycle_debt_) ? 0U : (arm9_cycle_debt_ - cycles);
    }

    arm7_cycle_debt_ += slice_ticks;
    while (arm7_cycle_debt_ >= kArm7TicksPerCycle) {
        arm7_bus_.set_current_pc(arm7_.registers().read(cpu::RegisterIndex::Pc));
        const TickCount ticks_used = static_cast<TickCount>(arm7_.step()) * kArm7TicksPerCycle;
        arm7_cycle_debt_ = (ticks_used >= arm7_cycle_debt_) ? 0U : (arm7_cycle_debt_ - ticks_used);
    }
}

void Emulator::trigger_vblank() {
    arm9_irq_.request_irq(hardware::interrupts::IrqSource::VBlank);
    arm7_irq_.request_irq(hardware::interrupts::IrqSource::VBlank);
    arm9_dma_.trigger_vblank();
    arm7_dma_.trigger_vblank();
}

void Emulator::trigger_hblank() {
    arm9_irq_.request_irq(hardware::interrupts::IrqSource::HBlank);
    arm7_irq_.request_irq(hardware::interrupts::IrqSource::HBlank);
    arm9_dma_.trigger_hblank();
}

void Emulator::trigger_gxfifo() {
    arm9_irq_.request_irq(hardware::interrupts::IrqSource::GeometryFifo);
    arm9_dma_.trigger_gxfifo();
}

void Emulator::set_touch_state(u16 x, u16 y, bool pressed) noexcept {
    arm7_spi_.touchscreen().set_touch_state(pressed, x, y);
    keypad_.set_button_pressed(input::DsButton::PenDown, pressed);
}

void Emulator::set_touch_pixel(u16 pixel_x, u16 pixel_y, bool pressed) noexcept {
    const auto adc = input::pixel_to_touch_adc(pixel_x, pixel_y);
    set_touch_state(adc.raw_x, adc.raw_y, pressed);
}

void Emulator::run_frame() {
    step_ticks(gpu::kTicksPerFrame);
}

void Emulator::set_rtc_datetime(const hardware::rtc::RtcDateTime& datetime) noexcept {
    arm7_rtc_.set_date_time(datetime);
}

}  // namespace elmasds::core
