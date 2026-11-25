# ADR-0001: Technology Stack and Build System

- **Status**: Accepted
- **Date**: 2026-11-25

## Context
ElmasDS targets Windows 10/11 and Linux on modest hardware (baseline: Intel Core i5-8350U, 16 GiB RAM, Intel UHD 620) with strict hardware accuracy, zero coupling between emulation core and presentation, and reproducible builds across MSVC, GCC, and Clang.

## Decision
1. **Language**: Strict C++20 (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF`) for deterministic memory layout, zero-overhead abstractions, `std::bit_cast` / `<bit>` / `<span>` / `<cstdint>`, and cross-platform portability.
2. **Build System**: CMake 3.24+ with `CMakePresets.json` covering Windows (MSVC) and Linux (GCC/Clang).
3. **Dependencies (Pinned via FetchContent)**:
   - **GoogleTest `v1.16.0`** for unit and integration testing (`elmasds_tests`).
   - **SDL3 `release-3.2.30`** for cross-platform windowing, keyboard/mouse input, and decoupled audio output, gated behind `ELMASDS_BUILD_FRONTEND` so `elmasds_core` remains strictly headless and library-agnostic.
4. **Graphics Presentation**: OpenGL 3.3 Core Profile loaded dynamically via SDL3 (`SDL_GL_GetProcAddress`) for minimal external dependency footprint and clean separation from the software/hardware-faithful 2D/3D DS rendering pipeline.

## Consequences
- `elmasds_core` compiles and runs unit tests even without graphics/audio libraries (`ELMASDS_BUILD_FRONTEND=OFF`).
- Dependency versions are explicitly locked in CMake.
