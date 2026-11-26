# ElmasDS — Compatibility Matrix

Per Section 11.4 of the specification, no title is marked compatible based solely on booting; Boot, Graphics, Audio, Input, and Save must each be independently verified.

| Title / Test Target | Region | Boot | Graphics | Audio | Input | Save | Notes |
|---|---|---|---|---|---|---|---|
| Synthetic Dual-CPU IPC & Direct Boot Suite | ALL | TESTED | TESTED | TESTED | TESTED | TESTED | Verified in `EmulatorIntegrationTest` (`tests/integration/test_direct_boot_and_dual_cpu.cpp`) |
| Synthetic 2D PPU Suite (Text/Affine/OBJ/Blend/VRAM) | ALL | TESTED | TESTED | N/A | N/A | N/A | Verified in `GpuTimingTest` & `Gpu2DRendererTest` (`tests/gpu/test_gpu_2d.cpp`) |
| Synthetic 3D GX & Rasterizer Suite | ALL | TESTED | TESTED | N/A | N/A | N/A | Verified in `Gpu3DTest` (`tests/gpu/test_gpu_3d.cpp`) |
| Synthetic HLE BIOS, Input & Audio Suite | ALL | TESTED | N/A | TESTED | TESTED | TESTED | Verified in `tests/input/test_input_and_hle_bios.cpp` & `tests/audio/test_audio_engine.cpp` |
| Synthetic EEPROM / Flash / SRAM & Save State Suite | ALL | TESTED | TESTED | TESTED | TESTED | TESTED | Verified in `tests/storage/test_rom_and_save.cpp` & `tests/storage/test_save_states.cpp` |
| Pokémon - Version Or HeartGold (France) | EUR (FR) | IN_PROGRESS | IN_PROGRESS | IN_PROGRESS | NOT_STARTED | NOT_STARTED | Direct boot + HLE BIOS + 512 KiB Flash / NTR-031 IR + `GXFIFO` level IRQ/DMA & 3D texture decoding (`1/2/3/4/6/7`, banks E/F/G) clears the post-forest 3D intro (`3D_polys=1229`) and sustains **68.5 FPS** across 2500 frames in Release; title/gameplay input & save persistence remain to be verified interactively |
