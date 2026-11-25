#pragma once

#include "cpu/arm_architecture/arm_core_context.hpp"
#include "cpu/arm_architecture/bus_interface.hpp"
#include "cpu/arm_architecture/cpu_step.hpp"
#include "cpu/arm_architecture/register_bank.hpp"
#include "primitives/fixed_width_types.hpp"

namespace elmasds::cpu {

/**
 * @brief Concrete ARM7TDMI (ARMv4T) processor core driving Nintendo DS ARM7 execution.
 *
 * Responsibility:
 *   Owns the ARMv4T banked register file, execution context, interrupt line state, and cycle
 *   accumulator, stepping instructions and IRQ/FIQ dispatch through `CpuBusInterface`.
 * Dependencies:
 *   `RegisterBank`, `ArmCoreContext`, `CpuBusInterface`, `step_arm_core`.
 * Hardware notes:
 *   - No CP15 system control coprocessor (`context_.coprocessor == nullptr`; MCR/MRC trap).
 *   - `high_vectors` remains `false` (vectors at `0x00000000..0x0000001C`).
 */
class Arm7Tdmi {
public:
    /**
     * @brief Constructs an ARM7TDMI core bound to `bus` and initialized via `reset()`.
     * @param bus Memory bus interface used for instruction fetches and data transfers.
     */
    explicit Arm7Tdmi(CpuBusInterface& bus) noexcept;

    Arm7Tdmi(const Arm7Tdmi&) = delete;
    Arm7Tdmi& operator=(const Arm7Tdmi&) = delete;
    Arm7Tdmi(Arm7Tdmi&&) = delete;
    Arm7Tdmi& operator=(Arm7Tdmi&&) = delete;

    /**
     * @brief Resets registers to Supervisor mode (`I=1`, `F=1`, `T=0`), clears lines/cycles.
     * @param reset_pc Initial PC address (defaults to `0x00000000`).
     */
    void reset(Address reset_pc = 0U) noexcept;

    /**
     * @brief Executes one step (interrupt entry, halted idle tick, or ARM/Thumb instruction).
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

    /**
     * @brief Sets or clears the core halted (WFI) state.
     */
    void set_halted(bool halted) noexcept;

    [[nodiscard]] RegisterBank& registers() noexcept;
    [[nodiscard]] const RegisterBank& registers() const noexcept;

    [[nodiscard]] ArmCoreContext& context() noexcept;
    [[nodiscard]] const ArmCoreContext& context() const noexcept;

    [[nodiscard]] bool is_halted() const noexcept;
    [[nodiscard]] TickCount total_cycles() const noexcept;

private:
    RegisterBank registers_;
    ArmCoreContext context_;
    InterruptLines interrupts_{};
    TickCount total_cycles_ = 0U;
};

}  // namespace elmasds::cpu
