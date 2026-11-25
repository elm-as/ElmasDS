# ADR-0004: Scanline-Based 2D Graphics Architecture

- **Status**: Accepted
- **Date**: 2026-11-25

## Context
The Nintendo DS features two independent 2D engines (Engine A and Engine B) operating at `256 x 192` pixels per screen, synchronized with a 263-scanline CRT/LCD timing model (`3072` master ticks HDraw + `1188` master ticks HBlank = `4260` ticks/line). Games frequently modify scroll registers, affine parameters, palettes, and windows mid-frame during HBlank DMA or HBlank IRQs.

## Decision
1. **Scanline Rendering at HBlank Start**:
   - `Gpu` schedules `EventType::ScanlineHBlank` (`+3072` ticks) and `EventType::ScanlineHDraw` (`+1188` ticks) on the central `EventScheduler`.
   - At the start of each visible HBlank (`VCOUNT` in `0..191`), `Engine2D::render_scanline` evaluates all enabled BG and OBJ layers for that scanline into `ARGB8888` (`0xFFRRGGBB`) scanline buffers before firing HBlank IRQs and ARM9 HBlank DMA.
2. **Two-Target Priority & Color Special Effects Compositor**:
   - Each pixel resolves the highest-priority (`0..3`) and second-highest-priority opaque fragments across `OBJ` and `BG0..3` (with `OBJ` winning ties against `BG`), masked by `WIN0`, `WIN1`, `OBJWIN`, and `WINOUT`.
   - Semi-transparent OBJs, alpha blending (`EVA`/`EVB`), brightness up/down (`EVY`), and per-engine `MASTER_BRIGHT` are computed purely in `color_effects.hpp`.
3. **3D BG0 Injection Boundary**:
   - `Engine2D` exposes `set_line_3d_buffer(std::span<const u32>)` so Phase 7's 3D rasterizer can feed a 256-pixel 3D scanline directly into Engine A's `BG0` priority/blending pipeline without coupling the 2D and 3D implementations.

## Consequences
- Mid-frame raster effects (HBlank scroll, affine splits, window tables) work naturally.
- `elmasds_core` produces ready-to-present `256 x 192` `ARGB8888` top and bottom framebuffers with zero dependency on OpenGL or SDL3.
