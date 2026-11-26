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
    : config_(std::move(config)), diagnostics_(config_.log_level),
      arm9_bus_(memory_), arm7_bus_(memory_), arm9_(arm9_bus_), arm7_(arm7_bus_),
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
      ipc_(arm9_irq_, arm7_irq_), arm7_spi_(&arm7_irq_),
      cartridge_(hardware::cartridge::CartridgeConfig{&arm9_irq_, &arm7_irq_, &arm9_dma_,
                                                      &arm7_dma_}),
      gpu_(gpu::GpuDependencies{scheduler_, memory_, arm9_irq_, arm7_irq_, arm9_dma_, arm7_dma_}),
      keypad_(arm9_irq_, arm7_irq_), audio_(scheduler_, arm7_bus_),
      arm9_hle_bios_(hardware::interrupts::CpuKind::Arm9, arm9_irq_, &arm9_.cp15()),
      arm7_hle_bios_(hardware::interrupts::CpuKind::Arm7, arm7_irq_) {
    arm9_.tcm().set_main_ram_fast_path(memory_.main_ram().mutable_data().data());
    scheduler_.set_profiler(&profiler_); arm9_.tcm().set_profiler(&profiler_); arm7_bus_.set_profiler(&profiler_);
    arm9_dma_.set_profiler(&profiler_); arm7_dma_.set_profiler(&profiler_);
    arm9_timers_.set_profiler(&profiler_); arm7_timers_.set_profiler(&profiler_);
    gpu_.set_profiler(&profiler_); audio_.set_profiler(&profiler_);
    arm9_.context().swi_handler = arm9_hle_bios_.make_swi_handler();
    arm7_.context().swi_handler = arm7_hle_bios_.make_swi_handler();
    register_mmio_devices();
    load_configured_system_images();
    diagnostics_.info("Emulator", "Core initialized with ARM9/ARM7 and Phase 3-7 subsystems.");
}

void Emulator::register_mmio_devices() {
    auto& m9 = memory_.arm9_mmio();
    auto& m7 = memory_.arm7_mmio();
    gpu_.register_mmio(m9, m7);
    const std::array<memory::MmioRegistration, 9U> reg9 = {{
        {hardware::dma::kDmaBaseAddress, hardware::dma::kDmaFillEndAddress, &arm9_dma_},
        {hardware::timers::kTimerBaseAddress, hardware::timers::kTimerEndAddress, &arm9_timers_},
        {hardware::cartridge::kCartMmioStart, hardware::cartridge::kCartMmioEnd, &cartridge_.arm9_mmio()},
        {hardware::cartridge::kRegRomData, hardware::cartridge::kRegRomDataEnd, &cartridge_.arm9_mmio()},
        {hardware::cartridge::kRegExMemCnt, hardware::cartridge::kRegExMemCntEnd, &cartridge_.arm9_mmio()},
        {hardware::interrupts::InterruptController::kMmioStart, hardware::interrupts::InterruptController::kMmioEnd, &arm9_irq_},
        {hardware::interrupts::SystemControlMmio::kMmioStart, hardware::interrupts::SystemControlMmio::kMmioEnd, &arm9_sys_ctrl_},
        {hardware::math::kMathMmioStart, hardware::math::kMathMmioEnd, &arm9_math_},
        {input::kKeypadMmioStart, input::kKeypadMmioEnd, &keypad_.arm9_mmio()},
    }};
    for (const auto& entry : reg9) { m9.register_device(entry); }

    const std::array<memory::MmioRegistration, 12U> reg7 = {{
        {hardware::dma::kDmaBaseAddress, hardware::dma::kDmaChannelEndAddress, &arm7_dma_},
        {hardware::timers::kTimerBaseAddress, hardware::timers::kTimerEndAddress, &arm7_timers_},
        {hardware::rtc::kRtcMmioStart, hardware::rtc::kRtcMmioEnd, &arm7_rtc_},
        {hardware::cartridge::kCartMmioStart, hardware::cartridge::kCartMmioEnd, &cartridge_.arm7_mmio()},
        {hardware::cartridge::kRegRomData, hardware::cartridge::kRegRomDataEnd, &cartridge_.arm7_mmio()},
        {hardware::cartridge::kRegExMemCnt, hardware::cartridge::kRegExMemCntEnd, &cartridge_.arm7_mmio()},
        {hardware::spi::kSpiMmioStart, hardware::spi::kSpiMmioEnd, &arm7_spi_},
        {hardware::wifi::kWifiMmioStart, hardware::wifi::kWifiMmioEnd, &arm7_wifi_},
        {hardware::interrupts::InterruptController::kMmioStart, hardware::interrupts::InterruptController::kMmioEnd, &arm7_irq_},
        {hardware::interrupts::SystemControlMmio::kMmioStart, hardware::interrupts::SystemControlMmio::kMmioEnd, &arm7_sys_ctrl_},
        {input::kKeypadMmioStart, input::kKeypadMmioEnd, &keypad_.arm7_mmio()},
        {audio::kAudioMmioStart, audio::kAudioMmioEnd, &audio_},
    }};
    for (const auto& entry : reg7) { m7.register_device(entry); }
    ipc_.register_mmio(m9, m7);
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
        if (!request.path.empty()) {
            report_load_status(request.name, load_system_image_file(request));
        }
    }
}

void Emulator::reset() noexcept {
    scheduler_.reset();
    memory_.reset();
    arm9_irq_.reset(); arm7_irq_.reset();
    arm9_sys_ctrl_.reset(); arm7_sys_ctrl_.reset();
    arm9_timers_.reset(); arm7_timers_.reset();
    arm9_dma_.reset(); arm7_dma_.reset();
    ipc_.reset(); arm9_math_.reset();
    arm7_spi_.reset(); arm7_rtc_.reset(); arm7_wifi_.reset();
    cartridge_.reset(); gpu_.reset();
    keypad_.reset(); audio_.reset();
    arm9_hle_bios_.reset(); arm7_hle_bios_.reset();
    arm9_.reset(0xFFFF0000U, true);
    arm7_.reset(0x00000000U);
    arm9_.tcm().set_main_ram_fast_path(memory_.main_ram().mutable_data().data());
    arm9_.context().swi_handler = arm9_hle_bios_.make_swi_handler();
    arm7_.context().swi_handler = arm7_hle_bios_.make_swi_handler();
    arm9_hle_bios_.set_enabled(!memory_.bios9().is_loaded());
    arm7_hle_bios_.set_enabled(!memory_.bios7().is_loaded());
    arm9_cycle_debt_ = 0U; arm7_cycle_debt_ = 0U;
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
    return cartridge_.save_memory().attach_file(
        save_path, static_cast<storage::save_manager::SaveType>(type_hint));
}

Status Emulator::flush_save() {
    return cartridge_.save_memory().flush_if_dirty();
}

void Emulator::populate_post_bios_memory(const storage::rom_loader::NdsRom& rom) {
    const auto& hdr = rom.header();
    const u32 chip_id = rom.chip_id();
    const std::size_t header_copy = std::min<std::size_t>(kBootHeaderCopySize, rom.bytes().size());
    write_bytes_to_bus(arm9_bus_, kBootHeaderRamAddress, rom.bytes().subspan(0U, header_copy));

    for (const Address addr : {0x027FF800U, 0x027FF804U, 0x027FFC00U, 0x027FFC04U}) {
        arm9_bus_.write32(addr, chip_id);
    }
    struct Field16 { Address addr; u16 val; };
    const std::array<Field16, 10U> fields16 = {{
        {0x027FF808U, hdr.header_crc},      {0x027FF80AU, hdr.secure_area_crc},
        {0x027FF850U, u16{0x5835U}},        {0x027FF868U, u16{0x7FC0U}},
        {0x027FF874U, hdr.header_crc},      {0x027FF876U, hdr.secure_area_crc},
        {0x027FFC08U, hdr.header_crc},      {0x027FFC0AU, hdr.secure_area_crc},
        {0x027FFC10U, u16{0x5835U}},        {0x027FFC40U, u16{0x0001U}},
    }};
    for (const auto& f : fields16) {
        arm9_bus_.write16(f.addr, f.val);
    }
    const auto& fw = arm7_spi_.firmware().data();
    if (fw.size() >= 0x3FE70U) {
        write_bytes_to_bus(arm9_bus_, 0x027FFC80U, std::span<const u8>(fw.data() + 0x3FE00U, 0x70U));
    }
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
    if (!memory_.bios9().is_loaded()) { memory_.bios9().install_hle_stub(cpu::create_hle_bios9_stub()); }
    if (!memory_.bios7().is_loaded()) { memory_.bios7().install_hle_stub(cpu::create_hle_bios7_stub()); }

    memory_.shared_wram().set_wram_cnt(0x03U);
    const auto& hdr = rom->header();
    write_bytes_to_bus(arm9_bus_, hdr.arm9_ram_address, rom->arm9_binary());
    write_bytes_to_bus(arm7_bus_, hdr.arm7_ram_address, rom->arm7_binary());
    populate_post_bios_memory(*rom);
    arm9_sys_ctrl_.write8(hardware::interrupts::SystemControlMmio::kRegPostFlg, 1U);
    arm7_sys_ctrl_.write8(hardware::interrupts::SystemControlMmio::kRegPostFlg, 1U);

    arm9_.reset(hdr.arm9_entry_address, true);
    arm9_.cp15().restore_registers(0x00052078U, 0x0300000AU, 0x00000020U);
    arm9_.context().high_vectors = true;
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
        TickCount until_event = remaining;
        if (const auto next_tick = scheduler_.next_event_tick(); next_tick.has_value()) {
            const TickCount now = scheduler_.current_tick();
            until_event = (*next_tick > now) ? (*next_tick - now) : 1ULL;
        }
        if (arm9_.can_skip_halt() && arm7_.can_skip_halt()) {
            const TickCount jump = std::min(remaining, until_event);
            arm9_cycle_debt_ = 0U; arm7_cycle_debt_ = 0U;
            { primitives::ScopedProfile ps(&profiler_, primitives::ProfileCategory::Scheduler); scheduler_.advance_by(jump); }
            remaining -= jump;
            continue;
        }
        const TickCount slice = std::min({remaining, until_event, kMaxCpuSliceTicks});
        step_cpu_slice(slice);
        { primitives::ScopedProfile ps(&profiler_, primitives::ProfileCategory::Scheduler); scheduler_.advance_by(slice); }
        remaining -= slice;
    }
}

void Emulator::step_cpu_slice(TickCount slice_ticks) {
    if (arm9_.can_skip_halt()) {
        arm9_cycle_debt_ = 0U;
    } else {
        primitives::ScopedProfile p9(&profiler_, primitives::ProfileCategory::Arm9);
        arm9_cycle_debt_ = arm9_.run_slice(arm9_cycle_debt_ + slice_ticks);
    }
    if (arm7_.can_skip_halt()) {
        arm7_cycle_debt_ = 0U;
    } else {
        primitives::ScopedProfile p7(&profiler_, primitives::ProfileCategory::Arm7);
        arm7_cycle_debt_ = arm7_.run_slice_ticks(arm7_cycle_debt_ + slice_ticks);
    }
}

void Emulator::trigger_vblank() {
    arm9_irq_.request_irq(hardware::interrupts::IrqSource::VBlank); arm7_irq_.request_irq(hardware::interrupts::IrqSource::VBlank);
    arm9_dma_.trigger_vblank(); arm7_dma_.trigger_vblank();
}
void Emulator::trigger_hblank() {
    arm9_irq_.request_irq(hardware::interrupts::IrqSource::HBlank); arm7_irq_.request_irq(hardware::interrupts::IrqSource::HBlank);
    arm9_dma_.trigger_hblank();
}
void Emulator::trigger_gxfifo() {
    arm9_irq_.request_irq(hardware::interrupts::IrqSource::GeometryFifo); arm9_dma_.trigger_gxfifo();
}
void Emulator::set_touch_state(u16 x, u16 y, bool pressed) noexcept {
    arm7_spi_.touchscreen().set_touch_state(pressed, x, y); keypad_.set_button_pressed(input::DsButton::PenDown, pressed);
}
void Emulator::set_touch_pixel(u16 pixel_x, u16 pixel_y, bool pressed) noexcept {
    const auto adc = input::pixel_to_touch_adc(pixel_x, pixel_y); set_touch_state(adc.raw_x, adc.raw_y, pressed);
}
void Emulator::run_frame() { step_ticks(gpu::kTicksPerFrame); }
void Emulator::set_rtc_datetime(const hardware::rtc::RtcDateTime& dt) noexcept { arm7_rtc_.set_date_time(dt); }

}  // namespace elmasds::core
