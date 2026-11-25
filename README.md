# ElmasDS

**ElmasDS** is a Nintendo DS emulator written in strict C++20, designed with a focus on hardware fidelity, deterministic synchronization, strict modular isolation, and cross-platform support (Windows 10/11 and Linux).

> Current status: **Phases 0–9 Complete (`TESTED`)**. See [PROJECT_STATUS.md](docs/PROJECT_STATUS.md) for the full component verification matrix.

## Architecture Overview

- `elmasds_core`: Pure C++20 emulation engine with **zero dependency** on SDL3 or OpenGL.
  - `src/primitives/`: Fixed-width types, pure bit operations, explicit `Status`/`Result<T>` error types, structured diagnostics.
  - `src/core/`: `Emulator` coordinator and deterministic 67.027964 MHz `EventScheduler`.
  - `src/cpu/`: Shared ARMv4T/ARMv5TE architectural primitives (`arm_architecture/`), 32-bit ARM & 16-bit Thumb decoders/executors, `arm9/` (ARM946E-S with CP15 & 32 KiB ITCM / 16 KiB DTCM), `arm7/` (ARM7TDMI), and `bios_hle/` (HLE BIOS `SWI` handlers).
  - `src/memory/`: 4 MiB Main RAM, 32 KiB Shared WRAM (`WRAMCNT`), 64 KiB ARM7 WRAM, 9 VRAM banks A–I (656 KiB + `VRAMCNT_A..I`), 2 KiB Palette RAM, 2 KiB OAM, BIOS9/BIOS7, and per-CPU MMIO routers.
  - `src/hardware/`: Interrupt controllers (`IME`/`IE`/`IF`), 8 timers (`TM0..3` per CPU with cascade), 8 DMA channels (`DMA0..3` per CPU), `IPCSYNC`/`IPCFIFO`, SPI bus (Powerman, Firmware Flash, TSC2046 Touchscreen), RTC, and Cartridge controller.
  - `src/gpu/`: Scanline-accurate 2D engines A & B (`engines_2d/`), 3D Geometry Engine & Software Rasterizer (`engine_3d/`), `DISPSTAT`/`VCOUNT`/`POWCNT1` timing.
  - `src/input/` & `src/audio/`: `KEYINPUT`/`KEYCNT`/`EXTKEYIN`, customizable keymap with conflict checks & file persistence, and 16-channel 32,768 Hz stereo PCM/ADPCM/PSG/Noise mixer.
  - `src/storage/`: `.nds` ROM parser with CRC-16 validation, EEPROM/Flash/SRAM save persistence, firmware/BIOS loader, and `.ess` binary save states.
- `elmasds`: Minimalist SDL3 + OpenGL 3.3 Core frontend displaying the dual 256×192 screens with `GL_NEAREST` integer scaling, keyboard mapping, shortcuts, mouse-driven stylus touch, drag-and-drop ROM loading, and `SDL_AudioStream` output.

## Default Controls & Shortcuts

| Nintendo DS Input / Action | Default Key / Input |
|---|---|
| D-Pad (`Up` / `Down` / `Left` / `Right`) | Arrow Keys |
| Face Buttons (`A` / `B` / `X` / `Y`) | `L` / `K` / `I` / `J` |
| Shoulder Buttons (`L` / `R`) | `Q` / `E` |
| `Start` / `Select` | `Enter` / `Right Shift` |
| Touchscreen / Stylus | Left Mouse Button on Bottom Screen |
| Pause / Resume | `Space` |
| Reset Console | `Ctrl + R` |
| Save State / Load State (`.ess`) | `F5` / `F7` |
| Fullscreen Toggle | `F11` |
| Quit | `Escape` or `Alt + F4` |

Custom key bindings can be configured via `elmasds_input.cfg` in the working directory.

## Building and Running Tests

### Windows (MSVC)

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug --config Debug
ctest --preset windows-msvc-debug --output-on-failure
```

### Linux (GCC or Clang)

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug --output-on-failure
```

### Running the Application

```powershell
# Headless self-check
.\build\windows-msvc-debug\src\Debug\elmasds.exe --headless-check

# Boot an NDS ROM file directly (or drag-and-drop a .nds file onto the window)
.\build\windows-msvc-debug\src\Debug\elmasds.exe path\to\homebrew.nds
```

## Proprietary Files Notice

ElmasDS does **not** include Nintendo DS ROMs, BIOS dumps, or firmware files. Direct boot and HLE BIOS allow running unencrypted `.nds` binaries without proprietary BIOS dumps; users may optionally supply legally obtained `bios9.bin`, `bios7.bin`, and `firmware.bin` via `EmulatorConfig`.

## Documentation

- [Project Status](docs/PROJECT_STATUS.md)
- [Implementation Roadmap](docs/IMPLEMENTATION_ROADMAP.md)
- [Known Issues](docs/KNOWN_ISSUES.md)
- [Compatibility Matrix](docs/COMPATIBILITY.md)
- [Architecture Decision Records](docs/decisions/)
- [Contributing Guidelines](CONTRIBUTING.md)
