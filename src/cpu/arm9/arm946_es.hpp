#pragma once

#include "cpu/arm9/cp15_system_control.hpp"
#include "cpu/arm9/tcm_bus_router.hpp"
#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_architecture/bus_interface.hpp"
#include "cpu/arm_architecture/cpu_step.hpp"
#include "cpu/arm_architecture/register_bank.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Concrete ARM946E-S (ARMv5TE) processor core driving Nintendo DS ARM9 execution.
 *
 * Responsibility:
 *   Owns the ARMv5TE banked register file, CP15 System Control Coprocessor, 32 KiB ITCM / 16 KiB
 *   DTCM bus router, execution context, interrupt line state, and cycle accumulator.
 * Dependencies:
 *   `RegisterBank`, `Cp15SystemControl`, `TcmBusRouter`, `ArmCoreContext`, `step_arm_core`.
 * Hardware notes:
 *   - Routes exception vectors to `0xFFFF0000..0xFFFF001C` when CP15 `HIVECS` (bit 13) is set.
 *   - Enters low-power halt state on `MCR p15, 0, Rd, c7, c0, 4` (`Wait for Interrupt`) when no
 *     interrupt line is already asserted, waking when IRQ or FIQ asserts.
 */
class Arm946Es final {
public:
    /**
     * @brief Constructs an ARM946E-S core routing non-TCM accesses through `external_bus`.
     * @param external_bus External memory bus interface underlying ITCM/DTCM routing.
     */
    explicit Arm946Es(CpuBusInterface& external_bus) noexcept;

    Arm946Es(const Arm946Es&) = delete;
    Arm946Es& operator=(const Arm946Es&) = delete;
    Arm946Es(Arm946Es&&) = delete;
    Arm946Es& operator=(Arm946Es&&) = delete;

    /**
     * @brief Resets registers, CP15, TCM storage, interrupt lines, and cycle counter.
     * @param reset_pc Initial PC address (defaults to `0x00000000`).
     * @param high_vectors Initial CP15 `HIVECS` bit 13 state.
     */
    void reset(Address reset_pc = 0U, bool high_vectors = false) noexcept;

    /**
     * @brief Executes one step (interrupt entry, halted idle tick, or ARMv5TE/Thumb instruction).
     * @return Cycles consumed by the step.
     */
    u32 step();

    /**
     * @brief Advances the core until at least `cycle_budget` cycles have elapsed.
     * @param cycle_budget Minimum number of cycles to execute (`0` performs no steps).
     * @return Actual cycles elapsed during this call.
     */
    u64 run_cycles(u32 cycle_budget);

    /**
     * @brief Sets the external active-high IRQ input line level.
     */
    void set_irq_line(bool asserted) noexcept;

    /**
     * @brief Sets the external active-high FIQ input line level.
     */
    void set_fiq_line(bool asserted) noexcept;

    /**
     * @brief Sets whether an enabled IRQ (`IE & IF != 0`) is pending to wake the core from halt.
     */
    void set_wake_pending(bool pending) noexcept;

    [[nodiscard]] RegisterBank& registers() noexcept;
    [[nodiscard]] const RegisterBank& registers() const noexcept;

    [[nodiscard]] Cp15SystemControl& cp15() noexcept;
    [[nodiscard]] const Cp15SystemControl& cp15() const noexcept;

    [[nodiscard]] TcmBusRouter& tcm() noexcept { return tcm_bus_; }
    [[nodiscard]] const TcmBusRouter& tcm() const noexcept { return tcm_bus_; }

    [[nodiscard]] ArmCoreContext& context() noexcept;
    [[nodiscard]] const ArmCoreContext& context() const noexcept;

    [[nodiscard]] bool is_halted() const noexcept;
    [[nodiscard]] TickCount total_cycles() const noexcept;

private:
    RegisterBank registers_;
    Cp15SystemControl cp15_;
    TcmBusRouter tcm_bus_;
    ArmCoreContext context_;
    InterruptLines interrupts_{};
    TickCount total_cycles_ = 0U;
};

}  // namespace elmasds::cpu
