# ElmasDS — Known Issues & Limitations

## Runtime Issues
- **Performance / Frame Stutter & Choppy Audio on Commercial ROMs**: The pure scalar CPU interpreter loop and per-tick scheduling overhead cause frame-pacing stutter and choppy `SDL_AudioStream` playback under load on commercial titles (observed on *Pokémon - Version Or HeartGold (France)*).
- **Post-Intro Freeze in Pokémon HeartGold**: Direct boot + HLE BIOS advances past NitroSDK init to the `"Developed by GAME FREAK inc."` intro screen, then freezes/stalls shortly afterward; further hardware/timing investigation is pending.

## Architectural Limitations
- **Cartridge KEY1/KEY2 Blowfish encryption**: Only direct `.nds` boot (`Emulator::direct_boot()`) and unencrypted/decrypted cartridge reads (`0xB7`) are implemented. Full cold-boot from encrypted BIOS/firmware into encrypted `KEY1` commands is out of scope for direct-boot usage.
- **3D Rendering Pipeline Scope**: The software 3D rasterizer (`src/gpu/engine_3d/`) implements 1.19.12 fixed-point matrix stacks, clip/screen transformations, triangles/quads/strips, depth testing, backface/frontface culling, vertex colors, and direct/paletted texture mapping. Advanced 3D hardware effects (toon/highlight shading table, edge marking, fog table, shadow volumes, and 4x4 compressed texel format 5) are not yet emulated.
- **Audio Capture (`SNDCAP0/1`) & Microphone**: The 16 playback channels (PCM8, PCM16, IMA-ADPCM, PSG square wave, LFSR noise) and stereo 32,768 Hz mixer are emulated; audio capture back into RAM and microphone SPI input return silence.
- **Wi-Fi (IEEE 802.11b)**: Local/online wireless registers (`0x04800000`) are not emulated (explicitly excluded by Section 2.2 of the specification).
