#pragma once

#include "hardware/interrupts/interrupt_controller.hpp"
#include "hardware/interrupts/irq_source.hpp"
#include "memory/mmio/mmio_device.hpp"
#include "memory/mmio/mmio_router.hpp"
#include "primitives/fixed_width_types.hpp"

#include <array>
#include <cstddef>

namespace elmasds::hardware::ipc {

/// Capacity of each directional IPC FIFO queue (`16 x 32-bit` words).
inline constexpr std::size_t kIpcFifoCapacity = 16U;

/// Inclusive MMIO range for `IPCSYNC`, `IPCFIFOCNT`, and `IPCFIFOSEND`.
inline constexpr Address kIpcControlMmioStart = 0x04000180U;
inline constexpr Address kIpcControlMmioEnd = 0x0400018BU;

/// Inclusive MMIO range for `IPCFIFORECV`.
inline constexpr Address kIpcFifoRecvStart = 0x04100000U;
inline constexpr Address kIpcFifoRecvEnd = 0x04100003U;

/// Register base addresses.
inline constexpr Address kRegIpcSync = 0x04000180U;
inline constexpr Address kRegIpcFifoCnt = 0x04000184U;
inline constexpr Address kRegIpcFifoSend = 0x04000188U;
inline constexpr Address kRegIpcFifoRecv = 0x04100000U;

/// GBATEK `IPCFIFOCNT` (`0x04000184`) bitmasks.
inline constexpr u16 kFifoCntSendEmpty = 1U << 0U;
inline constexpr u16 kFifoCntSendFull = 1U << 1U;
inline constexpr u16 kFifoCntSendEmptyIrq = 1U << 2U;
inline constexpr u16 kFifoCntSendClear = 1U << 3U;
inline constexpr u16 kFifoCntRecvEmpty = 1U << 8U;
inline constexpr u16 kFifoCntRecvFull = 1U << 9U;
inline constexpr u16 kFifoCntRecvNotEmptyIrq = 1U << 10U;
inline constexpr u16 kFifoCntError = 1U << 14U;
inline constexpr u16 kFifoCntEnable = 1U << 15U;

/**
 * @brief Bidirectional ARM9 <-> ARM7 Inter-Processor Communication controller.
 *
 * Responsibility:
 *   Implements `IPCSYNC` (`0x04000180`) 4-bit handshake & IRQ 16, and dual 16-entry 32-bit
 *   `IPCFIFOCNT` (`0x04000184`), `IPCFIFOSEND` (`0x04000188`), and `IPCFIFORECV` (`0x04100000`)
 *   with GBATEK bit layout, W1C error latching, disabled peek/ignore rules, and edge-triggered
 *   `IpcSendEmpty` (IRQ 17) / `IpcRecvNotEmpty` (IRQ 18) dispatch.
 */
class IpcController final {
public:
    /**
     * @brief Constructs an IPC controller wired to ARM9 and ARM7 interrupt controllers.
     */
    IpcController(interrupts::InterruptController& arm9_irq,
                  interrupts::InterruptController& arm7_irq) noexcept;

    IpcController(const IpcController&) = delete;
    IpcController& operator=(const IpcController&) = delete;
    IpcController(IpcController&&) = delete;
    IpcController& operator=(IpcController&&) = delete;

    /**
     * @brief Clears both endpoints' sync registers, FIFOs, latches, and error flags.
     */
    void reset() noexcept;

    /**
     * @brief Registers both IPC MMIO ranges on the ARM9 and ARM7 MMIO routers.
     */
    void register_mmio(memory::MmioRouter& arm9_router, memory::MmioRouter& arm7_router);

    [[nodiscard]] memory::MmioDevice& arm9_mmio() noexcept;
    [[nodiscard]] memory::MmioDevice& arm7_mmio() noexcept;

    [[nodiscard]] std::size_t arm9_send_count() const noexcept;
    [[nodiscard]] std::size_t arm7_send_count() const noexcept;

private:
    struct IpcFifoQueue {
        std::array<u32, kIpcFifoCapacity> entries{};
        u8 head = 0U;
        u8 tail = 0U;
        u8 count = 0U;
        u32 last_word = 0U;

        [[nodiscard]] bool empty() const noexcept { return count == 0U; }
        [[nodiscard]] bool full() const noexcept { return count >= kIpcFifoCapacity; }
        void clear() noexcept;
        void push(u32 word) noexcept;
        u32 pop() noexcept;
        [[nodiscard]] u32 peek_front() const noexcept;
    };

    struct IpcEndpointState {
        u8 sync_data_out = 0U;
        bool sync_irq_enable = false;
        bool send_empty_irq_enable = false;
        bool recv_not_empty_irq_enable = false;
        bool fifo_error = false;
        bool fifo_enabled = false;
        bool prev_send_empty_irq_cond = false;
        bool prev_recv_not_empty_irq_cond = false;
    };

    class EndpointMmio final : public memory::MmioDevice {
    public:
        EndpointMmio(IpcController& controller, interrupts::CpuKind cpu) noexcept;

        [[nodiscard]] u8 read8(Address address) override;
        [[nodiscard]] u16 read16(Address address) override;
        [[nodiscard]] u32 read32(Address address) override;

        void write8(Address address, u8 value) override;
        void write16(Address address, u16 value) override;
        void write32(Address address, u32 value) override;

    private:
        IpcController& controller_;
        interrupts::CpuKind cpu_;
    };

    [[nodiscard]] u8 read_mmio_8(interrupts::CpuKind cpu, Address address);
    [[nodiscard]] u16 read_mmio_16(interrupts::CpuKind cpu, Address address);
    [[nodiscard]] u32 read_mmio_32(interrupts::CpuKind cpu, Address address);
    void write_mmio_8(interrupts::CpuKind cpu, Address address, u8 value);
    void write_mmio_16(interrupts::CpuKind cpu, Address address, u16 value);
    void write_mmio_32(interrupts::CpuKind cpu, Address address, u32 value);

    [[nodiscard]] u16 read_sync(interrupts::CpuKind cpu) const noexcept;
    void write_sync_high_byte(interrupts::CpuKind cpu, u8 value) noexcept;
    [[nodiscard]] u16 read_fifo_cnt(interrupts::CpuKind cpu) const noexcept;
    void write_fifo_cnt_low_byte(interrupts::CpuKind cpu, u8 value) noexcept;
    void write_fifo_cnt_high_byte(interrupts::CpuKind cpu, u8 value) noexcept;
    void push_fifo_send(interrupts::CpuKind cpu, u32 word) noexcept;
    [[nodiscard]] u32 pop_fifo_recv(interrupts::CpuKind cpu) noexcept;

    void update_fifo_irqs() noexcept;
    void update_side_fifo_irqs(interrupts::CpuKind cpu) noexcept;

    [[nodiscard]] static std::size_t cpu_index(interrupts::CpuKind cpu) noexcept {
        return (cpu == interrupts::CpuKind::Arm9) ? 0U : 1U;
    }

    [[nodiscard]] static std::size_t remote_index(interrupts::CpuKind cpu) noexcept {
        return (cpu == interrupts::CpuKind::Arm9) ? 1U : 0U;
    }

    std::array<interrupts::InterruptController*, 2U> irqs_;
    std::array<IpcEndpointState, 2U> endpoints_{};
    std::array<IpcFifoQueue, 2U> send_fifos_{};
    EndpointMmio arm9_mmio_;
    EndpointMmio arm7_mmio_;
};

}  // namespace elmasds::hardware::ipc
