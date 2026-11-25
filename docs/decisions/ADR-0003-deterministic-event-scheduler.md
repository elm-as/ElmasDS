# ADR-0003: Deterministic Event Scheduler

- **Status**: Accepted
- **Date**: 2026-11-25

## Context
The Nintendo DS contains two coupled CPUs running at different clock frequencies:
- **ARM946E-S**: 67.027964 MHz (2× bus clock of 33.513982 MHz)
- **ARM7TDMI**: 33.513982 MHz (1× bus clock)
Plus scanline timings (HBlank, VBlank, VCount), 8 hardware timers, DMA channels, audio sampling (~32.768 kHz), and cartridge/SPI transfers. Running two CPU threads independently introduces non-deterministic races and breaks hardware timing dependencies (e.g., IPC synchronization, mid-scanline raster effects, DMA triggers).

## Decision
Use a centralized, deterministic `EventScheduler` operating on a unified master 64-bit cycle counter at the base bus clock (**33.513982 MHz**, where 1 ARM7 cycle = 1 master cycle, and 2 ARM9 cycles = 1 master cycle — or 67.027964 MHz master tick where 1 ARM9 cycle = 1 tick and 1 ARM7 cycle = 2 ticks).
Choosing **ARM9 clock ticks (67.027964 MHz, i.e., `1 ARM9 cycle = 1 tick`, `1 ARM7 cycle = 2 ticks`)** avoids fractional cycles for both CPUs!
- Events are stored in a min-heap ordered by `(trigger_tick, sequence_id)` for strict FIFO tie-breaking when two events land on the same tick.
- Each scheduled event carries a typed `EventType`, `trigger_tick`, `sequence_id`, and `Callback` (non-allocating or fixed-capacity callback wrapper).

## Consequences
- 100% deterministic execution across runs and platforms.
- Easy to serialize scheduler tick and pending events in future Save States (Phase 8).
- Allows starting simple and accurate, then optimizing hot paths (e.g., cycle batching) without changing subsystem contracts.
