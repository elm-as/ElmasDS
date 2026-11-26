# ElmasDS — Project Status

Status taxonomy:
- `NOT_STARTED`: Design not yet implemented.
- `IN_PROGRESS`: Active implementation or partial module.
- `IMPLEMENTED`: Code written and compiles without errors.
- `TESTED`: Unit and/or integration tests written and passing.
- `VERIFIED`: Validated against hardware specifications / reference suites and regression-checked.

## Phase 0 — Infrastructure

| Component | Status | Notes |
|---|---|---|
| CMake build & presets (Windows/Linux) | TESTED | Built cleanly on Windows MSVC 19.51 (`/W4 /WX`); Linux presets configured in CI |
| Dependency pinning (GoogleTest, SDL3) | TESTED | Pinned `googletest v1.16.0` and `SDL release-3.2.30` via FetchContent |
| Code quality configs (`.clang-format`, `.clang-tidy`) | IMPLEMENTED | C++20 strict, <=350 LOC, <=3 nesting/params |
| CI/CD workflow (GitHub Actions) | IMPLEMENTED | Windows + Linux GCC/Clang matrix defined in `.github/workflows/ci.yml` |
| Core primitives (`src/primitives/`) | TESTED | `fixed_width_types.hpp`, `bit_operations.hpp`, `status_result.hpp`, `diagnostic_log` |
| Deterministic `EventScheduler` | TESTED | FIFO tie-broken min-heap at 67.03 MHz ARM9 master ticks; unit tests passing |
| Frontend (`elmasds`) | TESTED | `DisplayLayout`, `GlScreenRenderer` (OpenGL 3.3 Core `GL_NEAREST`), SDL3 audio stream, drag-and-drop ROM loading, keyboard & stylus input |

## Phase 1 — ARM Processors

| Component | Status | Notes |
|---|---|---|
| ARM modes, CPSR/SPSR & banked `RegisterBank` | TESTED | All 7 modes, User/FIQ/IRQ/SVC/ABT/UND banks, MSR privilege masks |
| Condition codes & `BarrelShifter` | TESTED | All 16 conditions + immediate/register LSL/LSR/ASR/ROR/RRX edge cases |
| `CpuBusInterface` & `ExceptionVector` | TESTED | Abstract bus boundary, `fetch16`/`fetch32`, `CoprocessorInterface`, unaligned rules, Reset..FIQ + ARM9 HIVECS |
| ARM 32-bit decoder & executor (ARMv4T + ARMv5TE) | TESTED | ALU, MRS/MSR, MUL/MULL, CLZ/QADD/DSP mul, B/BL/BX/BLX, SWP, SWI, LDR/STR/LDRH/LDRD/LDM/STM |
| Thumb 16-bit decoder & executor | TESTED | All 19 Thumb formats + BL/BLX + PUSH/POP + STMIA/LDMIA |
| `Arm7Tdmi` & `Arm946Es` cores, CP15 & TCM | TESTED | `Arm7Tdmi`, `Arm946Es`, `Cp15SystemControl` (HIVECS/WFI/MPU/cache-clean), `TcmBusRouter` (32 KiB ITCM / 16 KiB DTCM), IRQ/FIQ |

## Phase 2 — Memory & Bus

| Component | Status | Notes |
|---|---|---|
| Address map & memory regions | TESTED | Main RAM (4 MiB), Shared WRAM (32 KiB + `WRAMCNT`), ARM7 WRAM (64 KiB), VRAM A-I (656 KiB + `VRAMCNT_*`), Palette (2 KiB), OAM (2 KiB), BIOS9/BIOS7 |
| Centralized `MemoryBus` | TESTED | `MemorySystem`, `Arm9MemoryBus`, `Arm7MemoryBus`, `MmioRouter`, ARM7 BIOS protection, waitstates, dual-CPU Shared WRAM + Main RAM integration |

## Phase 3 — Peripherals

| Component | Status | Notes |
|---|---|---|
| Interrupt controllers (ARM9 & ARM7) | TESTED | `IME`, `IE`, `IF` (W1C), `POSTFLG`/`HALTCNT`, CPU IRQ & halt-wake hooks |
| Timers (4 per CPU, cascade & prescalers) | TESTED | `TM0..TM3`, prescalers `1/64/256/1024`, count-up cascades, drift-free `EventScheduler` overflow scheduling |
| DMA controllers (ARM9 & ARM7) | TESTED | 4 channels per CPU, ARM9 `DMAxFILL`, 16/32-bit, Inc/Dec/Fixed/Reload, Repeat, Immediate/VBlank/HBlank/Cartridge/GXFIFO triggers |
| IPC FIFO & IPCSYNC | TESTED | Bidirectional ARM9 <-> ARM7 `IPCSYNC` 4-bit handshake + dual 16-entry `IPCFIFO` with empty/full/error flags & IRQs |
| ARM9 Hardware Math (`src/hardware/math/`) | TESTED | `DIVCNT`/`DIV_NUMER`/`DIV_DENOM`/`DIV_RESULT` (32/64-bit signed, overflow/zero) & `SQRTCNT`/`SQRT_PARAM`/`SQRT_RESULT` (32/64-bit) |
| SPI & RTC (ARM7) | TESTED | `SPICNT`/`SPIDATA` (Powerman, Firmware Flash, TSC2046 Touchscreen ADC) & Seiko S-3511A serial BCD RTC (`0x04000138`) |

## Phase 4 — Cartridge & Boot

| Component | Status | Notes |
|---|---|---|
| `.nds` ROM loader (`src/storage/rom_loader/`) | TESTED | Header (`0x000..0x1FF`) parser, header/logo CRC-16 validation, ARM9/ARM7 binary slicing, chip ID |
| Save memory manager (`src/storage/save_manager/`) | TESTED | `EEPROM` (512B/8KB/64KB), `Flash` (256KB/512KB/1MB), `SRAM` (32KB), capacity auto-detection, deferred dirty disk flush |
| Cartridge controller (`src/hardware/cartridge/`) | TESTED | `EXMEMCNT`/`EXMEMSTAT` slot arbitration, `AUXSPICNT`/`AUXSPIDATA`, `ROMCTRL`, `0x00`/`0x90`/`0xB8`/`0xB7`/`0x9F`, Repeat-DMA loop & IRQ |
| Firmware/BIOS loader & Direct Boot (`src/core/emulator`) | TESTED | Optional external BIOS9/BIOS7/Firmware loading, post-BIOS RAM/CP15 setup, dual-CPU `EventScheduler` execution |

## Phase 5 — 2D Graphics Engines

| Component | Status | Notes |
|---|---|---|
| Display timing & `DISPSTAT`/`VCOUNT`/`POWCNT1` | TESTED | HDraw 3072 ticks + HBlank 1188 ticks = 4260 ticks/line, 263 lines/frame (1,120,380 ticks), VBlank/HBlank/VCount IRQs & DMAs, display swap |
| Engine A & Engine B (`src/gpu/engines_2d/`) | TESTED | Display Modes 0-2, BG Modes 0-5 (text 4/8bpp, affine, extended 16-bit tilemap, 8bpp/direct bitmaps), 128 OAM sprites, windows, priority compositor, alpha blend/fade/master brightness |

## Phase 6 — HLE BIOS, Input & Audio

| Component | Status | Notes |
|---|---|---|
| HLE BIOS (`src/cpu/bios_hle/`) | TESTED | Clean-room ARM9/ARM7 IRQ trampolines & `SWI` interception (`Div`, `Sqrt`, `CpuSet`, `CpuFastSet`, `GetCRC16`, `BitUnPack`, `LZ77UnComp`, `RLUnComp`, `IntrWait`, `VBlankIntrWait`, `WaitByLoop`) |
| Keypad & Key Mapping (`src/input/`) | TESTED | `KEYINPUT` (`0x04000130`), `KEYCNT` (`0x04000132` OR/AND IRQ), `EXTKEYIN` (`0x04000136`), Section 13.1 defaults, conflict checks, config file load/save, stylus ADC transform |
| 16-Channel Audio Engine (`src/audio/`) | TESTED | `SOUND0..15` (`0x04000400..0x040004FF`), `SOUNDCNT`, `SOUNDBIAS`, PCM8, PCM16, IMA-ADPCM, PSG square wave (ch 8-13), LFSR noise (ch 14-15), 32,768 Hz stereo mixer |

## Phase 7 — 3D Graphics Engine

| Component | Status | Notes |
|---|---|---|
| Geometry Engine (`src/gpu/engine_3d/engine_3d.cpp`) | TESTED | `DISP3DCNT`, `GXFIFO` & direct command ports, `GXSTAT`, `RAM_COUNT`, `CLIPMTX_RESULT`, `VECMTX_RESULT`, 1.19.12 fixed-point matrix stacks, vertex assembly, `SWAP_BUFFERS`, GXFIFO DMA/IRQ |
| Software 3D Rasterizer (`src/gpu/engine_3d/rasterizer_3d.cpp`) | TESTED | Barycentric triangle/quad rasterizer, depth test (`less` / `equal`), backface/frontface culling, vertex color interpolation, direct/paletted VRAM texture sampling, Engine A BG0 scanline feed |

## Phase 8 & 9 — Save States, Compatibility & Release

| Component | Status | Notes |
|---|---|---|
| Save States (`src/storage/save_states/`) | TESTED | Versioned `.ess` binary snapshots (magic `ELMASDSS`, ROM CRC verification, ARM9/ARM7 registers, CP15/TCM, Main RAM, WRAM, VRAM A-I, Palette, OAM, Keypad, Audio, GPU) |
| Frontend & Release Packaging | TESTED | SDL3 + OpenGL 3.3 Core renderer, `SDL_AudioStream` playback, drag-and-drop / CLI ROM boot, `--diag-frames` check, `F5`/`F7` save states, `Space` pause, `Ctrl+R` reset, `F11` fullscreen; 130/130 tests passing in Debug & Release |
