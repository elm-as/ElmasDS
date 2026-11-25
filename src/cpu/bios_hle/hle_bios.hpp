#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "hardware/interrupts/interrupt_controller.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief High-Level Emulation (HLE) of Nintendo DS ARM9 and ARM7 BIOS software interrupts (`SWI`).
 *
 * Responsibility:
 *   Executes standard Nintendo DS BIOS system calls (`DelayLoop`, `IntrWait`, `VBlankIntrWait`,
 *   `Div`, `CpuSet`, `CpuFastSet`, `Sqrt`, `GetCRC16`, `IsDebugger`, `BitUnPack`, `LZ77UnComp`,
 *   `HuffUnComp`, `RLUnComp`, `WaitByLoop`) when no physical BIOS image is loaded for the target
 *   CPU, allowing homebrew and direct-booted ROMs to execute without proprietary BIOS dumps.
 */
class HleBios final {
public:
    HleBios(hardware::interrupts::CpuKind cpu_kind,
            hardware::interrupts::InterruptController& irq_controller) noexcept;

    /**
     * @brief Returns a `SwiHandler` bound to this `HleBios` instance.
     */
    [[nodiscard]] SwiHandler make_swi_handler() noexcept;

    /**
     * @brief Enables or disables HLE SWI interception (disabled when a real BIOS image is loaded).
     */
    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool is_enabled() const noexcept { return enabled_; }

    /**
     * @brief Handles one `SWI` call on `core`; returns `true` if handled in HLE mode.
     */
    [[nodiscard]] bool handle_swi(ArmCoreContext& core, u8 swi_number);

private:
    void swi_wait_by_loop(ArmCoreContext& core) const noexcept;
    void swi_intr_wait(ArmCoreContext& core, bool discard_old, u32 wait_flags) noexcept;
    void swi_div(ArmCoreContext& core) const noexcept;
    void swi_cpu_set(ArmCoreContext& core) const;
    void swi_cpu_fast_set(ArmCoreContext& core) const;
    void swi_sqrt(ArmCoreContext& core) const noexcept;
    void swi_get_crc16(ArmCoreContext& core) const;
    void swi_bit_unpack(ArmCoreContext& core) const;
    void swi_lz77_uncomp(ArmCoreContext& core) const;
    void swi_rl_uncomp(ArmCoreContext& core) const;

    [[nodiscard]] Address bios_irq_Check_flag_address() const noexcept;

    hardware::interrupts::CpuKind cpu_kind_;
    hardware::interrupts::InterruptController& irq_;
    bool enabled_ = true;
};

}  // namespace elmasds::cpu
