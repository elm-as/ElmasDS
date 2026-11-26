#include "frontend/application/diag_runner.hpp"

#include "core/emulator.hpp"
#include "hardware/cartridge/cartridge_controller.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "storage/save_manager/save_memory.hpp"

#include <algorithm>
#include <array>
#include <iostream>

namespace elmasds::frontend {
namespace {

[[nodiscard]] std::ptrdiff_t count_non_white(std::span<const u32> px) noexcept {
    return std::count_if(px.begin(), px.end(), [](u32 c) { return c != 0xFFFFFFFFU; });
}

[[nodiscard]] bool is_checkpoint_frame(int frame) noexcept {
    static constexpr std::array<int, 8U> kCheckpoints = {10, 20, 30, 40, 50, 60, 120, 240};
    return std::find(kCheckpoints.begin(), kCheckpoints.end(), frame) != kCheckpoints.end();
}

void print_checkpoint(core::Emulator& emu, int frame) {
    auto& bus9 = emu.arm9().context().bus;
    auto& bus7 = emu.arm7().context().bus;
    std::cout << std::dec << "F=" << frame << std::hex << std::uppercase
              << " PC9=0x" << emu.arm9_pc()
              << " PC7=0x" << emu.arm7_pc()
              << " DISPCNT_A=0x" << emu.gpu().regs_a().dispcnt()
              << " EXMEM=0x" << bus9.read16(hardware::cartridge::kRegExMemCnt)
              << " AUXSPI7=0x" << bus7.read16(hardware::cartridge::kRegAuxSpiCnt)
              << std::dec
              << " top_px=" << count_non_white(emu.top_screen())
              << " bot_px=" << count_non_white(emu.bottom_screen()) << '\n';
}

void print_timers(cpu::CpuBusInterface& bus, const char* label) {
    std::cout << label << "=[";
    for (u32 i = 0U; i < 4U; ++i) {
        if (i > 0U) {
            std::cout << ',';
        }
        std::cout << "0x" << bus.read16(0x04000102U + (i * 4U));
    }
    std::cout << ']';
}

void print_final_summary(core::Emulator& emu, TickCount arm9_delta, TickCount arm7_delta) {
    auto& bus9 = emu.arm9().context().bus;
    auto& bus7 = emu.arm7().context().bus;
    const Address pc9 = emu.arm9_pc();
    const Address pc7 = emu.arm7_pc();
    const Address dtcm_base = emu.arm9().cp15().dtcm_config().base_address;

    std::cout << std::hex << std::uppercase;
    std::cout << "ARM9 PC=0x" << pc9 << " LR=0x"
              << emu.arm9().registers().read(cpu::RegisterIndex::Lr) << " CPSR=0x"
              << emu.arm9().registers().cpsr().raw() << " halted=" << emu.is_arm9_halted()
              << std::dec << " delta_cycles_10f=" << arm9_delta << std::hex
              << " op=0x" << bus9.read32(pc9 & ~3U)
              << " IE=0x" << bus9.read32(hardware::interrupts::InterruptController::kRegIe)
              << " IF=0x" << bus9.read32(hardware::interrupts::InterruptController::kRegIf)
              << " IME=" << bus9.read32(hardware::interrupts::InterruptController::kRegIme)
              << " DTCM_IRQ=0x" << bus9.read32(dtcm_base + 0x3FF8U) << '\n';

    std::cout << "ARM7 PC=0x" << pc7 << " LR=0x"
              << emu.arm7().registers().read(cpu::RegisterIndex::Lr) << " CPSR=0x"
              << emu.arm7().registers().cpsr().raw() << " halted=" << emu.is_arm7_halted()
              << std::dec << " delta_cycles_10f=" << arm7_delta << std::hex
              << " op=0x" << bus7.read32(pc7 & ~3U)
              << " IE=0x" << bus7.read32(hardware::interrupts::InterruptController::kRegIe)
              << " IF=0x" << bus7.read32(hardware::interrupts::InterruptController::kRegIf)
              << " IME=" << bus7.read32(hardware::interrupts::InterruptController::kRegIme)
              << " BIOS7_IRQ=0x" << bus7.read32(0x0380FFF8U) << '\n';

    std::cout << "DISPCNT_A=0x" << emu.gpu().regs_a().dispcnt()
              << " DISPCNT_B=0x" << emu.gpu().regs_b().dispcnt()
              << " POWCNT1=0x" << emu.gpu().powcnt1().value()
              << " DISPSTAT9=0x" << bus9.read16(gpu::kRegDispStat)
              << " DISPSTAT7=0x" << bus7.read16(gpu::kRegDispStat)
              << std::dec << " VCOUNT=" << emu.gpu().vcount() << std::hex
              << " GXSTAT=0x" << emu.gpu().engine_3d().gxstat()
              << " DISP3DCNT=0x" << emu.gpu().engine_3d().disp3dcnt()
              << std::dec << " 3D_polys=" << emu.gpu().engine_3d().rendered_polygon_count()
              << std::hex << " ROMCTRL=0x" << bus9.read32(hardware::cartridge::kRegRomCtrl)
              << " AUXSPICNT=0x" << bus9.read16(hardware::cartridge::kRegAuxSpiCnt)
              << " EXMEMCNT=0x" << bus9.read16(hardware::cartridge::kRegExMemCnt) << '\n';

    std::cout << "ROMCTRL7=0x" << bus7.read32(hardware::cartridge::kRegRomCtrl)
              << " AUXSPICNT7=0x" << bus7.read16(hardware::cartridge::kRegAuxSpiCnt)
              << " SPICNT7=0x" << bus7.read16(0x040001C0U)
              << " IPCSYNC9=0x" << bus9.read16(0x04000180U)
              << " IPCFIFO9=0x" << bus9.read16(0x04000184U)
              << " IPCSYNC7=0x" << bus7.read16(0x04000180U)
              << " IPCFIFO7=0x" << bus7.read16(0x04000184U) << '\n';

    std::cout << "MBRIGHT_A=0x" << bus9.read16(0x0400006CU)
              << " MBRIGHT_B=0x" << bus9.read16(0x0400106CU)
              << " BLDCNT_A=0x" << bus9.read16(0x04000050U) << ' ';
    print_timers(bus9, "TM9");
    std::cout << ' ';
    print_timers(bus7, "TM7");
    std::cout << " DMA9=[";
    for (u32 i = 0U; i < 4U; ++i) {
        if (i > 0U) { std::cout << ','; }
        std::cout << "0x" << bus9.read16(0x040000BAU + (i * 12U));
    }
    std::cout << "]\nR0..5=[";
    for (u32 r = 0U; r < 6U; ++r) {
        if (r > 0U) { std::cout << ','; }
        std::cout << "0x" << emu.arm9().registers().read(static_cast<cpu::RegisterIndex>(r));
    }
    std::cout << "] CODE9=[";
    const Address base_pc = (pc9 & ~3U) - 0x18U;
    for (u32 w = 0U; w < 12U; ++w) {
        if (w > 0U) { std::cout << ' '; }
        std::cout << "0x" << bus9.read32(base_pc + (w * 4U));
    }
    std::cout << ']' << std::dec << '\n';

    std::cout << "Top non-white pixels: " << count_non_white(emu.top_screen()) << "/"
              << emu.top_screen().size()
              << " | Bottom non-white pixels: " << count_non_white(emu.bottom_screen()) << "/"
              << emu.bottom_screen().size() << '\n';
}

}  // namespace

int run_diag_frames(const std::filesystem::path& rom_path, int frames) {
    core::Emulator emu;
    if (const auto s = emu.load_rom_file(rom_path); s.is_err()) {
        std::cerr << "ROM load failed: " << s.error().message << '\n';
        return 1;
    }
    if (const auto s = emu.direct_boot(); s.is_err()) {
        std::cerr << "Direct boot failed: " << s.error().message << '\n';
        return 1;
    }
    emu.cartridge().save_memory().configure(storage::save_manager::SaveType::Flash512KB);

    const int warmup = std::max(0, frames - 10);
    TickCount arm9_before = 0U;
    TickCount arm7_before = 0U;
    for (int i = 0; i < frames; ++i) {
        if (i == warmup) {
            arm9_before = emu.arm9().total_cycles();
            arm7_before = emu.arm7().total_cycles();
        }
        emu.run_frame();
        if (is_checkpoint_frame(i + 1) && (i + 1) < frames) {
            print_checkpoint(emu, i + 1);
        }
    }

    print_final_summary(emu, emu.arm9().total_cycles() - arm9_before,
                        emu.arm7().total_cycles() - arm7_before);
    return 0;
}

}  // namespace elmasds::frontend
