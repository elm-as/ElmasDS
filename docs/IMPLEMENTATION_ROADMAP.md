# ElmasDS — Implementation Roadmap

This roadmap tracks coherent, independently verifiable engineering milestones derived from Section 10 of the ElmasDS specification.

## Phase 0 — Infrastructure
- **Goal**: Reproducible C++20 build on Windows (MSVC) and Linux (GCC/Clang), pinned dependencies, test harness, core typed primitives, deterministic `EventScheduler`, minimal SDL3/OpenGL frontend shell.
- **Acceptance Criteria**:
  1. `elmasds_core` builds cleanly with zero SDL3/OpenGL dependencies.
  2. `elmasds` builds and supports `--headless-check` as well as opening an SDL3/OpenGL 3.3 window.
  3. `elmasds_tests` builds and passes 100% of unit tests via `ctest`.

## Phase 1 — ARM Processors
- **Unit 1.1**: Architectural base (`CpuMode`, `StatusRegister`, `RegisterBank`, `ConditionCodes`, `BarrelShifter`) + unit tests.
- **Unit 1.2**: ARMv4T/ARMv5TE ARM instruction decoding & ALU/Branch/Load-Store/PSR execution.
- **Unit 1.3**: Thumb instruction decoding & execution.
- **Unit 1.4**: ARM7TDMI & ARM946E-S CPU cores, exceptions, CP15, TCM, and reference instruction tests.

## Phase 2 — Memory & Bus
- **Unit 2.1**: Distinct physical memory regions (Main RAM 4 MiB, Shared WRAM 32 KiB, ARM7 WRAM 64 KiB, VRAM 656 KiB, Palette RAM, OAM, BIOS).
- **Unit 2.2**: Dual `MemoryBus` views (ARM9 & ARM7) with access width enforcement, unaligned rotation/faults, waitstates, and MMIO dispatch.

## Phase 3 — Peripherals
- Interrupt controllers, Timers, DMA, IPC FIFO/Sync, SPI, RTC — each driven by `EventScheduler` and tested both standalone and in dual-CPU integration tests.

## Phase 4 — Cartridge & Boot
- `.nds` header parser & integrity checks, ROM transfer controller, SRAM/EEPROM/Flash save persistence with deferred flush, firmware/BIOS loading and direct/firmware boot.

## Phase 5 — 2D Graphics
- Scanline-accurate PPU synchronized via `EventScheduler` (HBlank/VBlank/VCount), Engine A & Engine B backgrounds, sprites, palettes, windows, alpha blending, mosaic, and VRAM bank mapping.

## Phase 6 — Input & Audio
- `KEYINPUT` / `EXTKEYIN` MMIO binding, configurable keymap with conflict checks, touchscreen coordinate transform (`256x192` native), 16-channel PCM/ADPCM/PSG audio mixer feeding SDL3 audio stream independently of frontend frame pacing.

## Phase 7 — 3D Graphics
- Geometry command FIFO, matrix stack, vertex/polygon transformation, clipping, lighting, software rasterizer, depth buffer, alpha test, fog, texture mapping.

## Phase 8 & 9 — Compatibility, Profiling & Distribution
- Automated test suite, game compatibility matrix, targeted hot-path optimization after profiling, optional isolated JIT behind `CpuInterface`, Windows/Linux packaging.
