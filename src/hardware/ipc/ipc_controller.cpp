#include "hardware/ipc/ipc_controller.hpp"

namespace elmasds::hardware::ipc {

void IpcController::IpcFifoQueue::clear() noexcept {
    head = 0U;
    tail = 0U;
    count = 0U;
    last_word = 0U;
}

void IpcController::IpcFifoQueue::push(u32 word) noexcept {
    entries[tail] = word;
    tail = static_cast<u8>((tail + 1U) % kIpcFifoCapacity);
    ++count;
}

u32 IpcController::IpcFifoQueue::pop() noexcept {
    const u32 word = entries[head];
    head = static_cast<u8>((head + 1U) % kIpcFifoCapacity);
    --count;
    last_word = word;
    return word;
}

u32 IpcController::IpcFifoQueue::peek_front() const noexcept {
    return empty() ? last_word : entries[head];
}

IpcController::EndpointMmio::EndpointMmio(IpcController& controller,
                                          interrupts::CpuKind cpu) noexcept
    : controller_(controller), cpu_(cpu) {}

u8 IpcController::EndpointMmio::read8(Address address) {
    return controller_.read_mmio_8(cpu_, address);
}

u16 IpcController::EndpointMmio::read16(Address address) {
    return controller_.read_mmio_16(cpu_, address);
}

u32 IpcController::EndpointMmio::read32(Address address) {
    return controller_.read_mmio_32(cpu_, address);
}

void IpcController::EndpointMmio::write8(Address address, u8 value) {
    controller_.write_mmio_8(cpu_, address, value);
}

void IpcController::EndpointMmio::write16(Address address, u16 value) {
    controller_.write_mmio_16(cpu_, address, value);
}

void IpcController::EndpointMmio::write32(Address address, u32 value) {
    controller_.write_mmio_32(cpu_, address, value);
}

IpcController::IpcController(interrupts::InterruptController& arm9_irq,
                             interrupts::InterruptController& arm7_irq) noexcept
    : irqs_{&arm9_irq, &arm7_irq},
      arm9_mmio_(*this, interrupts::CpuKind::Arm9),
      arm7_mmio_(*this, interrupts::CpuKind::Arm7) {}

void IpcController::reset() noexcept {
    endpoints_[0U] = IpcEndpointState{};
    endpoints_[1U] = IpcEndpointState{};
    send_fifos_[0U] = IpcFifoQueue{};
    send_fifos_[1U] = IpcFifoQueue{};
}

void IpcController::register_mmio(memory::MmioRouter& arm9_router,
                                  memory::MmioRouter& arm7_router) {
    arm9_router.register_device({kIpcControlMmioStart, kIpcControlMmioEnd, &arm9_mmio_});
    arm9_router.register_device({kIpcFifoRecvStart, kIpcFifoRecvEnd, &arm9_mmio_});
    arm7_router.register_device({kIpcControlMmioStart, kIpcControlMmioEnd, &arm7_mmio_});
    arm7_router.register_device({kIpcFifoRecvStart, kIpcFifoRecvEnd, &arm7_mmio_});
}

memory::MmioDevice& IpcController::arm9_mmio() noexcept {
    return arm9_mmio_;
}

memory::MmioDevice& IpcController::arm7_mmio() noexcept {
    return arm7_mmio_;
}

std::size_t IpcController::arm9_send_count() const noexcept {
    return send_fifos_[0U].count;
}

std::size_t IpcController::arm7_send_count() const noexcept {
    return send_fifos_[1U].count;
}

u8 IpcController::read_mmio_8(interrupts::CpuKind cpu, Address address) {
    if (address >= kIpcFifoRecvStart && address <= kIpcFifoRecvEnd) {
        const u32 word = pop_fifo_recv(cpu);
        return static_cast<u8>((word >> ((address & 3U) * 8U)) & 0xFFU);
    }
    const Address aligned = address & ~1U;
    if (aligned == kRegIpcSync) {
        const u16 sync_val = read_sync(cpu);
        return static_cast<u8>((sync_val >> ((address & 1U) * 8U)) & 0xFFU);
    }
    if (aligned == kRegIpcFifoCnt) {
        const u16 cnt_val = read_fifo_cnt(cpu);
        return static_cast<u8>((cnt_val >> ((address & 1U) * 8U)) & 0xFFU);
    }
    return 0U;
}

u16 IpcController::read_mmio_16(interrupts::CpuKind cpu, Address address) {
    const Address aligned = address & ~1U;
    if (aligned >= kIpcFifoRecvStart && aligned <= kIpcFifoRecvEnd) {
        const u32 word = pop_fifo_recv(cpu);
        return static_cast<u16>((word >> ((aligned & 2U) * 8U)) & 0xFFFFU);
    }
    if (aligned == kRegIpcSync) {
        return read_sync(cpu);
    }
    if (aligned == kRegIpcFifoCnt) {
        return read_fifo_cnt(cpu);
    }
    return 0U;
}

u32 IpcController::read_mmio_32(interrupts::CpuKind cpu, Address address) {
    const Address aligned = address & ~3U;
    if (aligned == kRegIpcFifoRecv) {
        return pop_fifo_recv(cpu);
    }
    if (aligned == kRegIpcSync) {
        return static_cast<u32>(read_sync(cpu));
    }
    if (aligned == kRegIpcFifoCnt) {
        return static_cast<u32>(read_fifo_cnt(cpu));
    }
    return 0U;
}

void IpcController::write_mmio_8(interrupts::CpuKind cpu, Address address, u8 value) {
    if (address >= kRegIpcFifoSend && address <= kIpcControlMmioEnd) {
        const u32 shifted = static_cast<u32>(value) << ((address & 3U) * 8U);
        push_fifo_send(cpu, shifted);
        return;
    }
    if (address == (kRegIpcSync + 1U)) {
        write_sync_high_byte(cpu, value);
        return;
    }
    if (address == kRegIpcFifoCnt) {
        write_fifo_cnt_low_byte(cpu, value);
        update_fifo_irqs();
        return;
    }
    if (address == (kRegIpcFifoCnt + 1U)) {
        write_fifo_cnt_high_byte(cpu, value);
        update_fifo_irqs();
    }
}

void IpcController::write_mmio_16(interrupts::CpuKind cpu, Address address, u16 value) {
    const Address aligned = address & ~1U;
    if (aligned >= kRegIpcFifoSend && aligned <= kIpcControlMmioEnd) {
        const u32 shifted = static_cast<u32>(value) << ((aligned & 2U) * 8U);
        push_fifo_send(cpu, shifted);
        return;
    }
    if (aligned == kRegIpcSync) {
        write_sync_high_byte(cpu, static_cast<u8>((value >> 8U) & 0xFFU));
        return;
    }
    if (aligned == kRegIpcFifoCnt) {
        write_fifo_cnt_low_byte(cpu, static_cast<u8>(value & 0xFFU));
        write_fifo_cnt_high_byte(cpu, static_cast<u8>((value >> 8U) & 0xFFU));
        update_fifo_irqs();
    }
}

void IpcController::write_mmio_32(interrupts::CpuKind cpu, Address address, u32 value) {
    const Address aligned = address & ~3U;
    if (aligned == kRegIpcFifoSend) {
        push_fifo_send(cpu, value);
        return;
    }
    if (aligned == kRegIpcSync) {
        write_sync_high_byte(cpu, static_cast<u8>((value >> 8U) & 0xFFU));
        return;
    }
    if (aligned == kRegIpcFifoCnt) {
        write_fifo_cnt_low_byte(cpu, static_cast<u8>(value & 0xFFU));
        write_fifo_cnt_high_byte(cpu, static_cast<u8>((value >> 8U) & 0xFFU));
        update_fifo_irqs();
    }
}

u16 IpcController::read_sync(interrupts::CpuKind cpu) const noexcept {
    const IpcEndpointState& local = endpoints_[cpu_index(cpu)];
    const IpcEndpointState& remote = endpoints_[remote_index(cpu)];
    u16 value = static_cast<u16>(remote.sync_data_out & 0x0FU);
    value = static_cast<u16>(value | (static_cast<u16>(local.sync_data_out & 0x0FU) << 8U));
    if (local.sync_irq_enable) {
        value = static_cast<u16>(value | (1U << 14U));
    }
    return value;
}

void IpcController::write_sync_high_byte(interrupts::CpuKind cpu, u8 value) noexcept {
    const std::size_t local_idx = cpu_index(cpu);
    const std::size_t remote_idx = remote_index(cpu);
    endpoints_[local_idx].sync_data_out = static_cast<u8>(value & 0x0FU);
    endpoints_[local_idx].sync_irq_enable = (value & 0x40U) != 0U;

    if ((value & 0x20U) != 0U && endpoints_[remote_idx].sync_irq_enable) {
        irqs_[remote_idx]->request_irq(interrupts::IrqSource::IpcSync);
    }
}

u16 IpcController::read_fifo_cnt(interrupts::CpuKind cpu) const noexcept {
    const IpcEndpointState& local = endpoints_[cpu_index(cpu)];
    const IpcFifoQueue& send_fifo = send_fifos_[cpu_index(cpu)];
    const IpcFifoQueue& recv_fifo = send_fifos_[remote_index(cpu)];

    u16 value = 0U;
    value = static_cast<u16>(value | (send_fifo.empty() ? kFifoCntSendEmpty : 0U));
    value = static_cast<u16>(value | (send_fifo.full() ? kFifoCntSendFull : 0U));
    value = static_cast<u16>(value | (local.send_empty_irq_enable ? kFifoCntSendEmptyIrq : 0U));
    value = static_cast<u16>(value | (recv_fifo.empty() ? kFifoCntRecvEmpty : 0U));
    value = static_cast<u16>(value | (recv_fifo.full() ? kFifoCntRecvFull : 0U));
    value =
        static_cast<u16>(value | (local.recv_not_empty_irq_enable ? kFifoCntRecvNotEmptyIrq : 0U));
    value = static_cast<u16>(value | (local.fifo_error ? kFifoCntError : 0U));
    value = static_cast<u16>(value | (local.fifo_enabled ? kFifoCntEnable : 0U));
    return value;
}

void IpcController::write_fifo_cnt_low_byte(interrupts::CpuKind cpu, u8 value) noexcept {
    const std::size_t idx = cpu_index(cpu);
    endpoints_[idx].send_empty_irq_enable = (value & 0x04U) != 0U;
    if ((value & 0x08U) != 0U) {
        send_fifos_[idx].clear();
    }
}

void IpcController::write_fifo_cnt_high_byte(interrupts::CpuKind cpu, u8 value) noexcept {
    const std::size_t idx = cpu_index(cpu);
    endpoints_[idx].recv_not_empty_irq_enable = (value & 0x04U) != 0U;
    if ((value & 0x40U) != 0U) {
        endpoints_[idx].fifo_error = false;
    }
    endpoints_[idx].fifo_enabled = (value & 0x80U) != 0U;
}

void IpcController::push_fifo_send(interrupts::CpuKind cpu, u32 word) noexcept {
    const std::size_t idx = cpu_index(cpu);
    IpcEndpointState& local = endpoints_[idx];
    if (!local.fifo_enabled) {
        return;
    }
    IpcFifoQueue& send_fifo = send_fifos_[idx];
    if (send_fifo.full()) {
        local.fifo_error = true;
        return;
    }
    send_fifo.push(word);
    update_fifo_irqs();
}

u32 IpcController::pop_fifo_recv(interrupts::CpuKind cpu) noexcept {
    IpcEndpointState& local = endpoints_[cpu_index(cpu)];
    IpcFifoQueue& recv_fifo = send_fifos_[remote_index(cpu)];
    if (!local.fifo_enabled) {
        return recv_fifo.peek_front();
    }
    if (recv_fifo.empty()) {
        local.fifo_error = true;
        return recv_fifo.last_word;
    }
    const u32 word = recv_fifo.pop();
    update_fifo_irqs();
    return word;
}

void IpcController::update_fifo_irqs() noexcept {
    update_side_fifo_irqs(interrupts::CpuKind::Arm9);
    update_side_fifo_irqs(interrupts::CpuKind::Arm7);
}

void IpcController::update_side_fifo_irqs(interrupts::CpuKind cpu) noexcept {
    const std::size_t idx = cpu_index(cpu);
    IpcEndpointState& side = endpoints_[idx];
    const IpcFifoQueue& send_fifo = send_fifos_[idx];
    const IpcFifoQueue& recv_fifo = send_fifos_[remote_index(cpu)];

    const bool send_cond = side.send_empty_irq_enable && send_fifo.empty();
    if (send_cond && !side.prev_send_empty_irq_cond) {
        irqs_[idx]->request_irq(interrupts::IrqSource::IpcSendEmpty);
    }
    side.prev_send_empty_irq_cond = send_cond;

    const bool recv_cond = side.recv_not_empty_irq_enable && !recv_fifo.empty();
    if (recv_cond && !side.prev_recv_not_empty_irq_cond) {
        irqs_[idx]->request_irq(interrupts::IrqSource::IpcRecvNotEmpty);
    }
    side.prev_recv_not_empty_irq_cond = recv_cond;
}

}  // namespace elmasds::hardware::ipc
