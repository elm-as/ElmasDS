# ElmasDS — Known Issues & Limitations

## Runtime Issues
- **Pokémon HeartGold In-Game Input & Save Persistence**: Halted-CPU idle-skip (`can_skip_halt()`), inline O(1) VRAM page table lookup, optimized 2D scanline renderer/compositor, 59.83 Hz frame pacing, bounded `SDL_AudioStream` queuing, `Flash512KB` + NTR-031 IR (`0x08`/`0x00`), `GeometryFifo` DMA immediate-start/re-entrancy guard, level-triggered `GXFIFO` IRQ evaluation on HBlank, and 3D texture formats (`1/2/3/4/6/7` + VRAM banks E/F/G) resolved the ~24 FPS stutter, post-Game-Freak-logo stall, and post-forest `0x20C2458` 3D intro freeze (sustaining **68.5 FPS** across 2500 frames with `1,229` rendered 3D polygons in Release testing). Interactive title/gameplay progression and save persistence remain to be verified.

## Architectural Limitations
- **Cartridge KEY1/KEY2 Blowfish encryption**: Only direct `.nds` boot (`Emulator::direct_boot()`) and unencrypted/decrypted cartridge reads (`0xB7`) are implemented. Full cold-boot from encrypted BIOS/firmware into encrypted `KEY1` commands is out of scope for direct-boot usage.
- **3D Rendering Pipeline Scope**: The software 3D rasterizer (`src/gpu/engine_3d/`) implements 1.19.12 fixed-point matrix stacks, clip/screen transformations, triangles/quads/strips, depth testing, backface/frontface culling, vertex colors, and direct/paletted texture mapping. Advanced 3D hardware effects (toon/highlight shading table, edge marking, fog table, shadow volumes, and 4x4 compressed texel format 5) are not yet emulated.
- **Audio Capture (`SNDCAP0/1`) & Microphone**: The 16 playback channels (PCM8, PCM16, IMA-ADPCM, PSG square wave, LFSR noise) and stereo 32,768 Hz mixer are emulated; audio capture back into RAM and microphone SPI input return silence.
- **Wi-Fi (IEEE 802.11b)**: Local/online wireless registers (`0x04800000`) are not emulated (explicitly excluded by Section 2.2 of the specification).
