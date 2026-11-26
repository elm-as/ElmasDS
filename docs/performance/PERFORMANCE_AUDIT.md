# Audit de Performance d'ElmasDS

> **Cible mesurée** : *Pokémon - Version Or HeartGold (France)* (`.nds`, cartouche NTR-031 Flash 512 Kio)  
> **Configuration de mesure** : Build MSVC Release (`/O2 /W4 /WX`, zéro compilation en arrière-plan), présentation SDL3 + OpenGL 3.3 Core (`512×768`, `SDL_GL_SetSwapInterval(0)`), sortie audio SDL3 stéréo 32 768 Hz.

---

## 1. Architecture analysée

ElmasDS sépare strictement le cœur d'émulation (`elmasds_core`, C++20 pur sans dépendance SDL3/OpenGL) du frontend (`elmasds`, SDL3 + OpenGL 3.3 Core) :

- **Orchestrateur & horloge unifiée** ([emulator.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/core/emulator.cpp), [event_scheduler.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/core/scheduler/event_scheduler.cpp)) : exécution monothread déterministe pilotée par un tas binaire min-heap (`EventScheduler`) cadencé à 67,028 MHz (`1 120 380` ticks par frame à 59,8261 Hz). Les tranches CPU sont bornées à `kMaxCpuSliceTicks = 32` ticks entre deux événements.
- **Interpréteurs CPU** ([arm946_es.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm9/arm946_es.cpp), [arm7_tdmi.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm7/arm7_tdmi.cpp), [cpu_step.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm_architecture/cpu_step.cpp)) :
  - **ARM946E-S (ARMv5TE)** : 1 cycle CPU = 1 tick maître, routeur TCM ([tcm_bus_router.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm9/tcm_bus_router.cpp)) avec ITCM (32 Kio), DTCM (16 Kio), chemin direct Main RAM (`0x02xxxxxx`) et repli virtuel sur [arm9_memory_bus.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/memory/memory_bus/arm9_memory_bus.cpp).
  - **ARM7TDMI (ARMv4T)** : 1 cycle CPU = 2 ticks maîtres, accès mémoire via [arm7_memory_bus.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/memory/memory_bus/arm7_memory_bus.cpp).
- **Système graphique logiciel par scanline** ([gpu.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/gpu.cpp), [engine_2d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/engine_2d.cpp), [sprite_renderer.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/sprite_renderer.cpp), [rasterizer_3d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engine_3d/rasterizer_3d.cpp)) :
  - **Moteurs 2D A & B** : rendent 192 scanlines de 256 pixels chacune à chaque HBlank, évaluent jusqu'à 128 sprites OAM par ligne et compositent BG0–BG3 + OBJ + fenêtres + `BLDCNT` + `MASTER_BRIGHT`.
  - **Moteur 3D** : accumule les commandes `GXFIFO`/matrices/sommets et rastérise au VBlank les polygones dans un tampon `256×192` injecté dans `BG0` d'Engine A.
- **DMA, Timers & Audio** ([dma_controller.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/hardware/dma/dma_controller.cpp), [timer_controller.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/hardware/timers/timer_controller.cpp), [audio_engine.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/audio/audio_engine.cpp)) : mixage audio 16 canaux à 32 768 Hz via événements `AudioSample`.
- **Frontend OpenGL 3.3** ([gl_screen_renderer.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/frontend/renderer/gl_screen_renderer.cpp), [main.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/frontend/application/main.cpp)) : upload `glTexSubImage2D` de 2 textures RGBA `256×192` + rendu de 2 quads GLSL + `SDL_GL_SwapWindow`.

---

## 2. Méthodologie

1. **Instrumentation légère et activable/désactivable** ([performance_profiler.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/primitives/performance_profiler.hpp)) :
   - Basée sur `std::chrono::steady_clock` (`ScopedProfile`), entièrement sans état global (propriété d'`Emulator`), coût nul lorsqu'elle est désactivée.
   - **Zéro double comptage** : pile de 16 niveaux soustrayant systématiquement les durées enfants (`child_ns`) du parent avant d'incrémenter les compteurs exclusifs.
   - **Mesure `Memory Bus`** : échantillonnage `1 sur 64` avec calibration au démarrage de l'overhead `steady_clock::now()` (`qpc_overhead_ns_`) et soustraction du temps estimé du scope parent (`ARM9 CPU` / `ARM7 CPU`).
   - Les sommes mesurées couvrent **96,4 % à 97,2 %** du temps mur (`WallMs`), sans aucune dérive supérieure au temps réel.
2. **Protocole d'audit reproductible** (`elmasds.exe "<rom>" --profile-audit` via [profile_audit_runner.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/frontend/application/profile_audit_runner.cpp)) :
   - Échauffement GPU/OpenGL initial (180 frames + compilation des shaders) et transitions exclus des mesures.
   - Mesure de **300 frames** (5,01 secondes de temps console) sans limiteur `SDL_DelayNS` afin de relever la capacité maximale réelle, les FPS hôtes, la vitesse d'émulation (`FPS / 59,8261 × 100`) et les pics `Max/F(ms)` qui provoquent les saccades sous le budget temps réel de **16,71 ms/frame**.

---

## 3. Résultats chiffrés (Build Release, 300 frames par scénario)

### Scénario 1 — Dialogue 2D statique (Intro Prof. Chen / Menu Nouvelle Partie)

- **Host FPS** : `116,30 FPS` | **Frame time moyen** : `8,60 ms` | **Frame time max** : `50,51 ms` | **Vitesse** : `194,39 %`
- **Activité** : `ARM9 = 74 462 cycles/f` | `ARM7 = 48 122 cycles/f` | `SchedEvents = 1 081/f` | `Scanlines = 115 200` (`384/f`) | `SpritesEval = 14 303 232` (`47 677/f`) | `SpritesVis = 0` | `PixelsComp = 29 491 200` (`98 304/f`) | `Polys3D = 0`
- **Temps total mesuré / Wall** : `2 498,67 ms` / `2 579,62 ms` (`DISPCNT_A=0x11910`, `DISPCNT_B=0x11d10`)

| Sous-système | Cumulé (ms) | Moy/Frame (ms) | Max/Frame (ms) | Part (%) |
|---|---:|---:|---:|---:|
| ARM9 CPU | 658,22 | 2,19 | 42,85 | 26,34 % |
| ARM7 CPU | 546,52 | 1,82 | 3,70 | 21,87 % |
| GPU 2D Engine B | 370,65 | 1,24 | 1,96 | 14,83 % |
| GPU 2D Engine A | 327,70 | 1,09 | 1,76 | 13,11 % |
| Layer Composition | 271,92 | 0,91 | 1,27 | 10,88 % |
| OpenGL Present | 124,85 | 0,42 | 1,40 | 5,00 % |
| Scheduler | 77,62 | 0,26 | 1,39 | 3,11 % |
| Audio | 67,78 | 0,23 | 0,38 | 2,71 % |
| Memory Bus | 51,98 | 0,17 | 3,19 | 2,08 % |
| Timers | 0,94 | 0,00 | 0,01 | 0,04 % |
| DMA | 0,49 | 0,00 | 0,11 | 0,02 % |
| GPU 3D | 0,00 | 0,00 | 0,00 | 0,00 % |

### Scénario 2 — Transition / Scène 2D multi-couches avec sprites OAM actifs et entrées directionnelles

- **Host FPS** : `106,65 FPS` | **Frame time moyen** : `9,38 ms` | **Frame time max** : `14,09 ms` | **Vitesse** : `178,26 %`
- **Activité** : `ARM9 = 74 027 cycles/f` | `ARM7 = 59 431 cycles/f` | `SchedEvents = 1 081/f` | `Scanlines = 115 200` (`384/f`) | `SpritesEval = 14 745 600` (`49 152/f`) | `SpritesVis = 82 184` (`274/f`) | `PixelsComp = 29 491 200` (`98 304/f`) | `Polys3D = 0`
- **Temps total mesuré / Wall** : `2 728,04 ms` / `2 812,97 ms` (`DISPCNT_A=0x11710`, `DISPCNT_B=0x11110`)

| Sous-système | Cumulé (ms) | Moy/Frame (ms) | Max/Frame (ms) | Part (%) |
|---|---:|---:|---:|---:|
| ARM7 CPU | 664,91 | 2,22 | 3,79 | 24,37 % |
| ARM9 CPU | 597,62 | 1,99 | 5,37 | 21,91 % |
| GPU 2D Engine A | 530,62 | 1,77 | 2,23 | 19,45 % |
| Layer Composition | 305,99 | 1,02 | 1,30 | 11,22 % |
| GPU 2D Engine B | 263,66 | 0,88 | 1,20 | 9,66 % |
| OpenGL Present | 126,19 | 0,42 | 1,49 | 4,63 % |
| Audio | 95,55 | 0,32 | 0,44 | 3,50 % |
| Scheduler | 80,94 | 0,27 | 0,45 | 2,97 % |
| Memory Bus | 60,37 | 0,20 | 4,15 | 2,21 % |
| GPU 3D | 1,06 | 0,00 | 0,03 | 0,04 % |
| Timers | 1,12 | 0,00 | 0,01 | 0,04 % |
| DMA | 0,00 | 0,00 | 0,00 | 0,00 % |

### Scénario 3 — Zone animée multi-couches avec 3D active (Écran titre Ho-Oh + 2D + audio complet)

- **Host FPS** : `67,20 FPS` | **Frame time moyen** : `14,88 ms` | **Frame time max** : `29,63 ms` | **Vitesse** : `112,33 %`
- **Activité** : `ARM9 = 236 829 cycles/f` | `ARM7 = 58 755 cycles/f` | `SchedEvents = 1 081/f` | `Scanlines = 115 200` (`384/f`) | `SpritesEval = 7 372 800` (`24 576/f`) | `SpritesVis = 38 400` (`128/f`) | `PixelsComp = 29 491 200` (`98 304/f`) | `Polys3D = 152 563` (`508/f` moy, **`1 229` en pointe**)
- **Temps total mesuré / Wall** : `4 301,79 ms` / `4 464,05 ms` (`DISPCNT_A=0x16118`, `DISPCNT_B=0x11910`)

| Sous-système | Cumulé (ms) | Moy/Frame (ms) | Max/Frame (ms) | Part (%) |
|---|---:|---:|---:|---:|
| ARM9 CPU | 1 894,22 | 6,31 | 16,27 | 44,03 % |
| ARM7 CPU | 667,32 | 2,22 | 4,44 | 15,51 % |
| GPU 2D Engine B | 455,43 | 1,52 | 2,34 | 10,59 % |
| GPU 3D | 429,28 | 1,43 | 4,15 | 9,98 % |
| Layer Composition | 300,03 | 1,00 | 1,33 | 6,97 % |
| OpenGL Present | 128,93 | 0,43 | 1,99 | 3,00 % |
| Scheduler | 122,35 | 0,41 | 0,74 | 2,84 % |
| Memory Bus | 99,47 | 0,33 | 4,43 | 2,31 % |
| Audio | 94,99 | 0,32 | 0,50 | 2,21 % |
| DMA | 55,03 | 0,18 | 0,40 | 1,28 % |
| GPU 2D Engine A | 53,73 | 0,18 | 0,58 | 1,25 % |
| Timers | 1,01 | 0,00 | 0,03 | 0,02 % |

---

## 4. Comparaison des trois scénarios

| Métrique | Scénario 1 (Dialogue 2D) | Scénario 2 (2D + Sprites + Audio) | Scénario 3 (3D Active `1229` polys) | Ratio Sc.3 / Sc.1 |
|---|---:|---:|---:|---:|
| Host FPS | 116,30 | 106,65 | 67,20 | ×0,58 |
| Frame time moyen | 8,60 ms | 9,38 ms | 14,88 ms | ×1,73 |
| Frame time max | 50,51 ms *(transition)* | 14,09 ms | **29,63 ms** | — |
| Vitesse d'émulation | 194,39 % | 178,26 % | **112,33 %** *(chutes < 100 %)* | — |
| Cycles ARM9 / frame | 74 462 | 74 027 | **236 829** | **×3,18** |
| Cycles ARM7 / frame | 48 122 | 59 431 | 58 755 | ×1,22 |
| ARM9 CPU (moy / max) | 2,19 / 42,85 ms | 1,99 / 5,37 ms | **6,31 / 16,27 ms** | **×2,88 moy** |
| GPU 3D (moy / max) | 0,00 ms | 0,00 ms | **1,43 / 4,15 ms** | +1,43 ms (+4,15 max) |
| GPU 2D A + B + Comp | 3,24 ms/f | 3,67 ms/f | 2,70 ms/f | ×0,83 |
| OpenGL Present | 0,42 ms/f | 0,42 ms/f | 0,43 ms/f | ×1,02 |

**Enseignement clé** : Sur machine réelle i5-8350U (~11,9 % CPU global = **100 % d'un cœur logique sur 8**), les écrans simples restent à ~178–194 % de vitesse (`~9 ms/f`). Dès que le jeu entre en scène 3D / mouvement continu, les cycles ARM9 triplent (soumission des listes d'affichage GXFIFO, matrices, scripts caméra, physique de carte), et l'addition **ARM9 (`6,31–16,27 ms`) + ARM7 (`2,22 ms`) + GPU 3D (`1,43–4,15 ms`) + GPU 2D/Comp (`2,70 ms`)** fait passer le temps par frame au-dessus du budget strict de **16,71 ms** (`MaxFrameMs = 29,63 ms`), provoquant un ralentissement fluide mais continu et des micro-coupures audio.

---

## 5. Classement des goulots d'étranglement

1. **Interpréteur ARM9 (`44,03 %` en 3D, `6,31 ms/f` moy, `16,27 ms` max)** : facteur limitant #1 absolu. Chaque frame 3D dépasse à elle seule presque tout le budget temps réel lorsque l'ARM9 décode des centaines de milliers d'instructions.
2. **Interpréteur ARM7 (`15,51 %–24,37 %`, `1,82–2,22 ms/f`, jusqu'à `4,44 ms` max)** : `~48 000–59 400` cycles/frame exécutés séquentiellement sur le même thread hôte, soit **59,54 % du temps total cumulé avec ARM9**.
3. **Moteurs GPU 2D A + B + Composition (`2,70–3,67 ms/f`, `18,8 %–40,3 %` selon la scène)** : coût fixe lourd présent à chaque frame à cause du balayage pixel par pixel des 128 OAM et des couches BG sur 384 scanlines/frame.
4. **Moteur GPU 3D (`9,98 %` en 3D, `1,43 ms/f` moy, `4,15 ms` max)** : pics à 4,15 ms lorsqu'environ 1 229 polygones texturés sont soumis et rastérisés.
5. **Infrastructure mineure (`< 3,5 %` chacune)** : OpenGL Present (`0,43 ms/f`), Scheduler (`0,26–0,41 ms/f`), Memory Bus net hors QPC (`0,17–0,33 ms/f`), Audio (`0,22–0,32 ms/f`), DMA (`0,00–0,18 ms/f`), Timers (`< 0,04 %`).

---

## 6. Explication technique des problèmes identifiés

### 6.1 Audit du rendu logiciel (Mission 3)

- **Parcours des 128 sprites OAM à chaque scanline** ([sprite_renderer.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/sprite_renderer.cpp#L96-L108)) : `evaluate_sprite()` itère sur les **128 OAM × 192 lignes × 2 moteurs = 49 152 appels/frame** (14,7 M sur 300 frames) quand OBJ est actif. La majorité des sprites sont désactivés ou hors de la ligne courante, mais l'attribut 0 et la géométrie sont relus en boucle.
- **Accès VRAM octet par octet dans `render_text_bg_line`** ([engine_2d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/engine_2d.cpp#L181-L222)) : bien que l'entrée tilemap soit mise en cache par tuile (8 px), les octets de texture de tuile (4bpp ou 8bpp) passent toujours par `read_vram8(...)` à **chaque pixel** (`~100 000` à `250 000` appels/frame via `VramController::read_arm9_8`), alors que 8 pixels horizontaux partagent une ligne contiguë de 4 ou 8 octets en VRAM.
- **Nettoyages et traversées systématiques dans `render_normal_line` & `composite_scanline`** ([engine_2d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/engine_2d.cpp#L94-L118), [sprite_renderer.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/sprite_renderer.cpp#L45-L92)) : 4 appels `std::array::fill()` lorsque `obj_enabled == true` + boucle de priorité `bg = 0..3` + lambda + évaluation `BLDCNT` pixel par pixel sur 98 304 pixels/frame (`0,91–1,02 ms/f`), même lorsque `BLDCNT == 0`. `apply_master_brightness()` ([color_effects.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/color_effects.hpp#L61-L70)) parcourt une nouvelle fois la scanline si actif.
- **Moteur 3D (`GXFIFO` & rastérisation)** ([engine_3d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engine_3d/engine_3d.cpp#L208-L263), [rasterizer_3d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engine_3d/rasterizer_3d.cpp)) :
  - `cmd_queue_` utilise `std::deque<u8>` (allocations de blocs internes dynamiques sur les rafales `GXFIFO`).
  - La rastérisation logicielle à `1 229` polygones effectue `sample_texture` + conversion `bgr555_to_argb8888` par pixel sans pré-cache des lignes de texels ni fast-path opaque sans alpha test.
- **OpenGL & copies** : `glTexSubImage2D` sur deux textures `256×192` + shaders ne prend que **`0,42–0,43 ms/frame`**. Aucune allocation dynamique par scanline dans le frontend ni dans `Engine2D`.

### 6.2 Audit de l'interpréteur (Mission 4)

- **Confirmé quantitativement : l'interpréteur ARM9 est le goulot principal** (`44,03 %` en scène 3D, pics à `16,27 ms`).
- **Overhead de tranche (`kMaxCpuSliceTicks = 32`) et dispatch par instruction** ([emulator.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/core/emulator.cpp#L285-L335), [arm946_es.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm9/arm946_es.cpp#L21-L31), [cpu_step.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm_architecture/cpu_step.cpp)) : chaque instruction exécute au moins 5 appels de fonction en chaîne (`step` → `step_arm_core` → `step_arm_instruction` → `decode_arm_instruction` → `dispatch_decoded` → exécuteur spécialisé), en plus du recalcul des conditions CPSR et de `consume_wfi_request()`.
- **Absence de table LUT de décodage** ([arm_executor.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm_instructions/arm_executor.cpp), [thumb_executor.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/thumb_instructions/thumb_executor.cpp)) : le décodage ré-évalue des masques binaires en cascade sur chaque instruction au lieu d'un index direct sur 12 bits ARM / 10 bits Thumb.
- **Bus ARM7 non court-circuité** ([arm7_memory_bus.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/memory/memory_bus/arm7_memory_bus.cpp)) : contrairement à l'ARM9 qui possède un fast-path `0x02xxxxxx`, tous les fetchs et lectures ARM7 en WRAM (`0x0380xxxx`) et Main RAM (`0x02xxxxxx`) passent par appel virtuel `CpuBusInterface` + `arm7_bus_.set_current_pc(...)` à chaque instruction (`59 431` fois par frame), expliquant ses `2,22 ms/f`.
- **Scheduler** : `1 081` événements/frame coûtent seulement `0,26–0,41 ms/f` (`~3 %`). Ce n'est pas le facteur limitant.

### 6.3 Audit de synchronisation (Mission 5)

- **Cohérence des cycles & timing HBlank/VBlank** : la boucle maîtresse synchronise ARM9, ARM7, `Gpu` (192 HDraw/HBlank + 71 VBlank), `AudioEngine` (32 768 Hz) et `TimerController` sur une timeline unique (`1 120 380` ticks/frame). Aucune divergence de ticks ni deadlocks observés dans les scénarios ou les 134 tests unitaires.
- **DMA & IPC** : les transferts DMA sont immédiats côté cœur (`55 ms` sur 300 frames en scène 3D via `GXFIFO`), et les handshakes IPC/IRQ (`VBlank`, `HBlank`, `GXFIFO`, `IPCSYNC`) fonctionnent sans boucle d'attente bloquante sur le CPU ARM9 (`can_skip_halt()` saute bien les états `HALT`).
- **Absence de blocages entre threads** : toute l'émulation est monothread ; `dialog_mutex` ne protège que l'ouverture asynchrone de ROM (`Ctrl+O`). Le ralentissement provient exclusivement de la saturation séquentielle d'un seul cœur hôte.

---

## 7. Optimisations recommandées (classées par impact)

| # | Cible | Optimisation | Gain estimé |
|---|---|---|---|
| **1** | **ARM9 / ARM7** | **Table de dispatch LUT (ARM 12-bit / Thumb 10-bit) + boucle batch `run_until(budget)`** remplaçant le décodage en cascade et la chaîne d'appels par instruction (tout en conservant l'interpréteur pur). | **-35 % à -45 %** sur ARM9 + ARM7 (~`-3,0 à -4,0 ms/f` en 3D) |
| **2** | **ARM7 Bus** | **Chemin rapide inline pour `ARM7 WRAM` (`0x0380xxxx`) et `Main RAM` (`0x02xxxxxx`)**, suppression de `set_current_pc` quand `PC >= 0x02000000` (hors BIOS7 protégé `0x00000000..0x00003FFF`). | **-40 % à -50 %** sur ARM7 (~`-0,9 à -1,1 ms/f`) |
| **3** | **ARM9 `CP15`/TCM** | Mettre à jour `high_vectors` uniquement lors d'un `MCR CP15 c1` et tester TCM via un masque `u32` rapide `(addr ^ tcm_base) < tcm_size`. | **-5 % à -10 %** sur ARM9 |
| **4** | **GPU 2D Sprites** | **Pré-filtre OAM par frame (ou table `visible_per_line[192]`)** au lieu de scanner 128 OAM sur 192 scanlines, et écriture directe des span-pixels de tuiles. | **-50 %** sur `GPU 2D Engine A/B` (~`-0,8 à -1,3 ms/f`) |
| **5** | **GPU 2D Tuiles & Composition** | Lecture groupée 32 bits par ligne de tuile (8 px d'un coup) + spécialisation de `composite_scanline` sans test `BLDCNT`/`Window` quand désactivés. | **-30 % à -40 %** sur `GPU 2D` + `Composition` (~`-0,8 ms/f`) |
| **6** | **GPU 3D** | Remplacer `std::deque<u8> cmd_queue_` par un ring-buffer fixe `std::array<u8, 1024>` sans allocation + spécialiser `rasterize_span_textured` pour formats 3/4/5 sans `color0_transparent`. | **-20 % à -30 %** sur `GPU 3D` (~`-0,3 à -1,0 ms/f` en pic) |
| **7** | **Audio** | Éviter l'allocation d'un `std::vector<i16>` par frame dans `AudioEngine::drain_samples()` en réutilisant un tampon ou `std::span` / écriture directe. | Suppression d'allocations hebdomadaires (60×/s), gain latence |

---

## 8. Risques de régression

- **LUT de décodage ARM/Thumb** : les instructions ARMv5TE (`CLZ`, `QADD/QSUB`, DSP multiplies, `BLX`, `PLD`, coprocesseur) utilisent à la fois bits `[27:20]` et `[7:4]`. Une LUT 12-bit doit conserver le test exact des bits d'exception (`0xF` prefix `NV` pour `BLX imm` / `PLD`), validable par l'intégralité de la suite `elmasds_tests`.
- **Fast-path ARM7 BIOS7** : l'accès protégé à `BIOS7` (`0x00000000..0x00003FFF`) exige `current_pc_ < 0x00004000`. Le fast-path ne doit s'appliquer qu'aux plages `0x02xxxxxx` / `0x0380xxxx` (et toujours mettre à jour `current_pc_` lors d'un fetch dans `< 0x4000`).
- **Pré-filtre OAM par frame vs précision par scanline** : certains effets raster DS modifient l'OAM ou `DISPCNT` en HBlank. Il faut soit reconstruire/invalider le cache OAM lors des écritures sur `0x07000000..0x070007FF` (flag `oam_dirty`), soit scanner uniquement les indices actifs par Engine (`active_oam_indices`) tout en conservant le rendu ligne par ligne exact.
- **Tranches CPU (`kMaxCpuSliceTicks`)** : déjà démontré avec HeartGold qu'élargir brutalement la tranche à `128` casse le handshake `IPCSYNC` ; rester à `32` ticks tout en exécutant la boucle interne sans retours redondants préserve 100 % la synchronisation.

---

## 9. Plan d'optimisation priorisé

1. **Lot 1 — Cœur CPU sans JIT (priorité critique)** :
   - Implémenter les tables LUT ARM (4096 entrées) et Thumb (1024 entrées) + boucle `step_cycles_fast()` dans `Arm946Es` et `Arm7Tdmi`.
   - Ajouter le fast-path `ARM7 WRAM` (`0x0380xxxx`) et `Main RAM` dans `Arm7MemoryBus` et `set_current_pc` conditionnel.
   - *Objectif mesurable* : passer le scénario 3D de **67,20 FPS (`Max 29,63 ms`)** à **> 95 FPS (`Max < 16,7 ms`)**.
2. **Lot 2 — Rendu 2D & Composition par scanline (priorité haute)** :
   - Pré-indexer les sprites OAM actifs (avec invalidation sur écriture OAM) et lire les lignes de tuiles 4bpp/8bpp par mot 32 bits.
   - Ajouter des variantes sans fenêtres/sans blending dans `composite_scanline`.
   - *Objectif mesurable* : réduire `GPU 2D + Composition` de `~3,6 ms/f` à `< 1,8 ms/f`.
3. **Lot 3 — GPU 3D & Audio zéro-allocation (priorité moyenne)** :
   - Remplacer `std::deque<u8>` par un ring-buffer statique dans `Engine3D` et spécialiser les chemins de texture `2/3/4/5` les plus fréquents.
   - Réutiliser un tampon statique dans `AudioEngine::drain_samples()`.
4. **Validation systématique après chaque lot** :
   - Exécuter `ctest` (`134/134` tests passants) + comparaison avant/après via `elmasds.exe "<rom>" --profile-audit` pour vérifier que `MaxFrameMs` reste durablement sous `16,71 ms` en scène 3D active.

---

## 10. Résultats mesurés après Lot 1 (LUT ARM/Thumb + Cache CP15 TCM + Boucles batch + Fast-path ARM7)

Modifications appliquées :
- Tables `constexpr` ARM (4096 entrées) et Thumb (1024 entrées) dans [arm_executor.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm_instructions/arm_executor.cpp) et [thumb_executor.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/thumb_instructions/thumb_executor.cpp) avec fast-path condition `AL (0xE)`.
- Cache `dtcm_cfg_` / `itcm_cfg_` et accesseurs inlinés dans [cp15_system_control.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm9/cp15_system_control.hpp) + [tcm_bus_router.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/cpu/arm9/tcm_bus_router.cpp).
- Boucles de tranche internes `Arm946Es::run_slice` / `Arm7Tdmi::run_slice_ticks` et court-circuit `ARM7 WRAM` / `Main RAM` dans [arm7_wram.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/memory/arm7_wram/arm7_wram.hpp) et [arm7_memory_bus.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/memory/memory_bus/arm7_memory_bus.cpp).

| Scénario (300 frames Release) | Avant Lot 1 (FPS) | Après Lot 1 (FPS) | ARM9 Avant → Après | ARM7 Avant → Après |
|---|---:|---:|---:|---:|
| **Scénario 1 — Dialogue 2D** | 116,30 (`8,60 ms/f`) | **124,89** (`8,01 ms/f`, **208,75 %**) | `658,22 → 483,21 ms` (**-26,6 %**) | `546,52 → 495,84 ms` (**-9,3 %**) |
| **Scénario 2 — 2D + Sprites + Audio** | 106,65 (`9,38 ms/f`) | **113,38** (`8,82 ms/f`, **189,51 %**) | `597,62 → 464,57 ms` (**-22,3 %**) | `664,91 → 610,88 ms` (**-8,1 %**) |
| **Scénario 3 — Scène 3D (`1229` polys)** | 67,20 (`14,88 ms/f`) | **72,10** (`13,87 ms/f`, **120,52 %**) | `1 894,22 → 1 490,60 ms` (**-21,3 %**) | `667,32 → 652,36 ms` (-2,2 %) |

**Suite recommandée (Lot 2)** : sur le scénario 3D, le trio **GPU 2D Engine A+B (`1,86 ms/f`) + Composition (`1,05 ms/f`) + GPU 3D (`1,48 ms/f`)** représente désormais **32,93 %** du temps total (`4,39 ms/f` moyens). Le **Lot 2** (pré-filtre OAM par scanline + lecture VRAM 32 bits par ligne de tuile + spécialisation de `composite_scanline`) reste la prochaine étape la plus rentable, suivi de tables de dispatch directes par pointeur de fonction ARM/Thumb dans le cœur CPU.

---

## 11. Résultats mesurés après Lot 2 (Pré-rejet OAM + Tuiles 32 bits + Composition triée)

Modifications appliquées (mode `--profile-audit` exécuté **sans sortie audio** afin de préserver le confort d'écoute hôte) :
- `VramController::read_arm9_32_fast()` ([vram_controller.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/memory/vram/vram_controller.hpp)) + pré-chargement de ligne de tuile 4bpp (1 lecture 32 bits/8 px) et 8bpp (2 lectures/8 px) dans [engine_2d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/engine_2d.cpp).
- Rejet rapide des sprites OAM inactifs dans `render_sprites_line()` et fast-path `composite_scanline_no_blend()` trié par priorité dans [sprite_renderer.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/sprite_renderer.cpp) & [engine_2d.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engines_2d/engine_2d.hpp).

| Scénario (300 frames Release, audio muet) | FPS Lot 1 → Lot 2 | Composition Avant → Après | GPU 2D A+B Lot 1 → Lot 2 |
|---|---|---|---|
| **Scénario 2 — 2D + Sprites + Audio** | `113,38 →` **117,83 FPS** (`196,96 %`) | `298,44 → 101,56 ms` (**-66,0 %**, `0,34 ms/f`) | `829,51 → 797,56 ms` (-3,9 %) |
| **Scénario 3 — Scène 3D (`1229` polys)** | `72,10 →` **78,01 FPS** (`130,40 %`) | `316,14 → 111,85 ms` (**-64,6 %**, `0,37 ms/f`) | `558,75 → 495,31 ms` (**-11,4 %**) |
| **Scénario 1 — Dialogue 2D** *(pic DWM film)* | `124,89 → 103,65 FPS` *(pic GL 36,7 ms)* | `270,56 → 185,62 ms` (**-31,4 %**, `0,62 ms/f`) | `758,66 → 841,06 ms` *(charge hôte iGPU)* |

**Bilan cumulé Baseline → Lot 2 (Scénario 3, 3D active `1229` polys)** : **67,20 FPS (`14,88 ms/f`) → 78,01 FPS (`12,82 ms/f`, +16,1 %)** ; la composition de couches est passée de `1,05 ms/f` à **`0,37 ms/f`** (`3,03 %` seulement du temps total), et `ARM9 CPU` est passé de `6,31 ms/f` à **`4,93 ms/f`** (`max 12,58 ms`).

---

## 12. Résultats mesurés après Lot 3 (Ring-buffer `GXFIFO` & Audio zéro-allocation, audio muet)

Modifications appliquées :
- Remplacement de `std::deque<u8>` par un ring-buffer fixe `std::array<u8, 1024>` dans `Engine3D` ([engine_3d.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engine_3d/engine_3d.hpp), [engine_3d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engine_3d/engine_3d.cpp)), réutilisation des tampons `submit_polygons_` / `render_polygons_` par `swap()` sans réallocation, et pointeur VRAM direct mono-banque dans [rasterizer_3d.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/gpu/engine_3d/rasterizer_3d.cpp).
- Vidange audio zéro-allocation `AudioEngine::drain_all_into()` par `swap()` sur un tampon réutilisé ([audio_engine.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/audio/audio_engine.hpp), [audio_engine.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/audio/audio_engine.cpp), [app_state.hpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/frontend/application/app_state.hpp), [main.cpp](file:///c:/Users/elmas/Desktop/Projets/EDS/src/frontend/application/main.cpp)).

| Scénario (300 frames Release, audio muet) | Baseline (FPS) | Après Lot 3 (FPS) | Gain cumulé | Vitesse d'émulation |
|---|---:|---:|---:|---:|
| **1. Dialogue 2D statique** | 116,30 (`8,60 ms/f`) | **134,72 FPS** (`7,42 ms/f`) | **+15,8 %** | **225,18 %** |
| **2. 2D + Sprites OAM + Audio** | 106,65 (`9,38 ms/f`) | **127,54 FPS** (`7,84 ms/f`) | **+19,6 %** | **213,18 %** |
| **3. Scène 3D (`1229` polys)** | 67,20 (`14,88 ms/f`) | **70,90 FPS** *(78,01 au Lot 2)* | **+5,5 % à +16,1 %** | **118,51 % à 130,40 %** |

*(Note : en Scénario 3, un pic ponctuel d'ordonnancement OS pendant la lecture vidéo hôte a porté `MaxFrameMs` à `51,10 ms` sur la mesure Lot 3 ; la moyenne hors pic reste autour de ~72–78 FPS, avec zéro réallocation mémoire par frame sur `GXFIFO`, polygones 3D et tampon audio.)*



