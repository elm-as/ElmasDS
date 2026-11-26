#pragma once

#include "cpu/arm9/cp15_system_control.hpp"
#include "cpu/arm_architecture/arm_core_context.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "primitives/fixed_width_types.hpp"

#include <vector>

namespace elmasds::cpu {

/**
 * @brief High-Level Emulation (HLE) of Nintendo DS ARM9 and ARM7 BIOS software interrupts (`SWI`).
 *
 * Responsibility:
 *   Executes standard Nintendo DS BIOS system calls (`WaitByLoop`, `IntrWait`, `VBlankIntrWait`,
 *   `Halt`, `Div`, `CpuSet`, `CpuFastSet`, `Sqrt`, `GetCRC16`, `IsDebugger`, `BitUnPack`,
 *   `LZ77UnComp`, `RLUnComp`) when no physical BIOS image is loaded for the target CPU.
 */
class HleBios final {
public:
    HleBios(hardware::interrupts::CpuKind cpu_kind,
            hardware::interrupts::InterruptController& irq_controller,
            const Cp15SystemControl* cp15 = nullptr) noexcept;

    void reset() noexcept { intr_wait_active_ = false; }

    [[nodiscard]] SwiHandler make_swi_handler() noexcept;

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool is_enabled() const noexcept { return enabled_; }

    /**
     * @brief Handles one `SWI` call on `core`; returns `SwiOutcome`.
     */
    [[nodiscard]] SwiOutcome handle_swi(ArmCoreContext& core, u8 swi_number);

private:
    void swi_wait_by_loop(ArmCoreContext& core) const noexcept;
    [[nodiscard]] SwiOutcome swi_intr_wait(ArmCoreContext& core,
                                           bool discard_old,
                                           u32 wait_flags) noexcept;
    void swi_div(ArmCoreContext& core) const noexcept;
    void swi_cpu_set(ArmCoreContext& core) const;
    void swi_cpu_fast_set(ArmCoreContext& core) const;
    void swi_sqrt(ArmCoreContext& core) const noexcept;
    void swi_get_crc16(ArmCoreContext& core) const;
    void swi_bit_unpack(ArmCoreContext& core) const;
    void swi_lz77_uncomp(ArmCoreContext& core) const;
    void swi_rl_uncomp(ArmCoreContext& core) const;

    [[nodiscard]] Address bios_irq_check_flag_address() const noexcept;

    hardware::interrupts::CpuKind cpu_kind_;
    hardware::interrupts::InterruptController& irq_;
    const Cp15SystemControl* cp15_ = nullptr;
    bool enabled_ = true;
    bool intr_wait_active_ = false;
};

/**
 * @brief Generates a clean-room 4 KiB ARM9 BIOS image containing only the HLE IRQ vector trampoline.
 */
[[nodiscard]] std::vector<u8> create_hle_bios9_stub();

/**
 * @brief Generates a clean-room 16 KiB ARM7 BIOS image containing only the HLE IRQ vector trampoline.
 */
[[nodiscard]] std::vector<u8> create_hle_bios7_stub();

}  // namespace elmasds::cpu
