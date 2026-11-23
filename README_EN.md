<p align="right">
  <a href="README.md">简体中文</a> | <strong>English</strong>
</p>

# DST HyperSmooth 240FPS & FastLoad (v2.6.0)

> **Ceiling-Tier Client-Only Performance, Ultra-Low-Latency Input & Player-First Eye-Care Visual Suite for Don't Starve Together (64-bit)**  
> Looking for the dedicated server suite (Windows `iphlpapi.dll` & **Linux x86_64 `libhypercore.so`**)? Switch to branch 👉 [`server-hypercore`](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/tree/server-hypercore) ([Linux Standalone Guide](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/tree/server-hypercore/server_hypercore_linux) | [Releases](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases))

---

## Core Design: Three Experience Pillars × Five Player-First Iron Laws

Dozens of C++ engine hooks and Lua optimizations converge into **Three Player-Facing Pillars**:

1. **Visual Smoothness — Continuous 240Hz World Motion**
   - **Full-Scene 240Hz Co-Phase Interpolation**: Main-world / VFX / UI 3-channel 2D skeletal tweening, `WorldPos240` for zero-lag-comp movement (with **adaptive network De-Jitter smoothing** `g_optAdaptiveDeJitter`), engine VFX + legacy emitters (rain/snow/sparks/spells), moving dynamic lights, camera follow & `FollowText` bubbles, plus 240Hz `TrueScrollArea`, chat log, and minimap zoom.
   - **Zero-Contention RTSS-Grade Frame Pacer (`FrameLimiter::Wait`)** + **Extended GLES2 & Uniform Shadow State Cache (`gl_state_cache.h`)** + **Read-Only Mod/Data Negative Stat Cache (`fs_neg_cache.h`)** + **CraftingMenu Zero-Stall & Same-Tick `Placer:TestCanBuild` / `TriggerDeployHelpers` Deduplication** + **Singleplayer / Local-Host Engine Trio (`FastAStar` 25× + `FastDataDumper` 155MB/s + `libdeflate` 2.5× FastLoad)**.
2. **Ultra-Low Latency — Snappy Kiting, Gear Swaps & Dodges**
   - **D3D9Ex 1-Frame Hardware Queue (Reflex-Style)**: Cuts ANGLE's swap-chain back-queue from stock `3` frames down to `1` frame, shaving `8–33 ms` of pipeline lag.
   - **240Hz Edge-Triggered Input + RakNet Zero-Stall Wakeup**: Immediate WASD, `F`-attack / `Ctrl+LMB` force-attack flick-click rescue, space interact, and instant packet dispatch without waiting up to `33.3 ms` per sim tick or `0–10 ms` UDP stalls.
3. **Player-First Eye-Care Visuals — Clear Details Without Post-Process Blasts**
   - Strictly follows **Five Player-First Rules**:
     1. **100% Vanilla Pure by Default** (zero unwanted visual overhead out of the box);
     2. **Never forces post-processing ON** (even `BloomSampler` stays untouched);
     3. **9 independent visual switches** with 0ms live comparison in the side panel (`c_hsmenu()`);
     4. **100% bit-exact vanilla restore** when toggled off;
     5. **Full visual enhancement even with Bloom/ScreenShake/Distortion/Flash turned OFF**.

---

## What's New in v2.6.0

### 1. Complete 240Hz Smoothness & High-DPI UI Loop
- **Full-Engine 240Hz Particles & Dynamic Lights**: Hooks `ParticleManager::Update`, `VFXRenderCmd`, `EmitterRenderCmd`, and `LightMap::GenerateRenderData` for continuous 240Hz interpolation, paired with adaptive De-Jitter `WorldPos240` smoothing when lag compensation is off.
- **Multi-Particle & Rigid-Bone Matcher Fixes (Fireflies, Leaves, Ponds, Wolfgang Dumbbells)**:
  - Global nearest-neighbor + `2-opt` uncrossing for fireflies, autumn leaves, and pond reflections prevents split/frozen fireflies and cross-tree streaks.
  - Strict bijective split between particle layers (`isParticleLayer`) and rigid composite layers (`arm_carry`, etc.) eliminates `48px` false rejections during Wolfgang dumbbell lifts (`22→0`), transformations (`89→0`), and melee swings (`59→3`), while keeping `tree_leaf_fx` streak protection 100% intact.
  - Single-shot self-replay wrap interpolation eliminates the 1-frame hitch at the end of repeated work loops.
- **CraftingMenu Zero-Stall + Deploy Placer Dedup**: `CraftingMenuHUD:RebuildRecipes` upgraded to incremental $O(\text{slots})$ lookups with deferred rebuilds while hidden; **`Placer:TestCanBuild` & `TriggerDeployHelpers`** deduplicate redundant same-tick calls (`>0.25u` or tick change passes immediately); `TrueScrollArea` and `FollowText` run smoothly at 240Hz without breaking ShowMe/Insight.
- **Singleplayer / Local-Host Trio**: Built-in `O(N log N)` **`FastAStar`**, `155 MB/s` **`FastDataDumper`**, and **`libdeflate` `compress2`** automatically accelerate non-caves local saves and walled-base pathfinding.
- **Extended GLES2 + Uniform State Cache (`gl_state_cache.h`) & Negative Stat Cache (`fs_neg_cache.h`)**:
  - Filters redundant `glUniform*`, `glBindTexture`, `glUseProgram` (fail-closed linked check), blend, cull, depth, and scissor calls.
  - Caches read-only mod/data `stat`/`open` misses with epoch invalidation on file writes, deletes, renames, or directory creation.

### 2. Ultra-Low-Latency Combat & Input
- **D3D9Ex 1-Frame Render Queue**: Sets `SetMaximumFrameLatency(1)` via ANGLE `applyTextures` and restores `3` cleanly when toggled off.
- **240Hz Flick-Click Rescue & Walk-Cancel Guard**: Reliably captures rapid `Ctrl+LMB` modifier clicks, movement direction cancels, and immediate WASD/attack RPC edges across both pure vanilla public servers and 60Hz `HyperCore Server` worlds.

### 3. Eye-Care Live Panel (`hs_panel.lua` High-DPI Fix) & DLL Update Banner
- **High-DPI `SCALEMODE_PROPORTIONAL` Side-Panel Fix**: Separated outer `ANCHOR_RIGHT` proportional scale node from the inner `HyperSmoothLivePanelBody` position container (`-232, -20`), eliminating right-edge clipping on `2560×1440` and `4K` displays (`[ON]/[OFF]`, `Custom`, `X`, and `Close Panel` stay completely visible). Includes 4 presets (`Vanilla / EyeHD / Story / Custom`), 9 independent toggles (with `Mip LOD -0.45` and `31.5 LUT` dark/cave clarity), 0ms `0.5× ⇄ 1.0×` LightMap swap, and 1-click Vanilla Restore.
- **Smart `bin64/dinput8.dll` Update Alert Strip**: Compares Workshop Lua version against `HyperSmooth_Version()` and alerts players if their manually installed `dinput8.dll` is outdated.

### 4. Red-Team Hardening & Automatic `DontStarveLuaJIT2` Compatibility
- Strict fail-closed `ThreadFreezeGuard`, 64-bit monotonic generation tags against `FastAStar` slot ABA, multi-context OpenGL program validation, lockless `exchange(0)` frame stats with `>=500ms` stall attribution, and `PAGE_EXECUTE_READ` W^X trampolines.
- Auto-detects **`DontStarveLuaJIT2` (Workshop `3444078585`)** (`lua51DS.dll` / `Injector.dll` / `Winmm.dll`) at cold start and skips `mimalloc lua_newstate` automatically.
- **Open-Source One-Click Installer (`tools/一键安装与更新.bat`)**: Auto-detects Steam/WeGame paths, guards against running game locks, backs up existing DLLs, verifies SHA256, and supports 1-click uninstall.

---

## Verified Benchmarks

Test rig: `Windows 10/11 x64 · Intel i7-12700KF · RTX 4060 · 32GB RAM · 240Hz · DST 64-bit Build 762172`

| Benchmark | Vanilla | HyperSmooth v2.6.0 | Measured Gain |
| :--- | :---: | :---: | :---: |
| `LOAD BE` asset decompression | `10.05s` | **`4.0s`** | **2.5× faster** (`libdeflate v1.23` + `rep stosb`) |
| Cave ⇄ Surface `SimReset` reload | Full unload & reload | **`0.0 MB` zero reload** | **Cross-reset asset preservation** |
| 240Hz visual interpolation | 30Hz stepped frames | **Anim + VFX + Lights + Cam + Bubbles 240Hz** | `300×18` matcher `292 → 67 µs` (**4.3× faster**) |
| RTSS-grade 240Hz frame pacer | Busy-spin (`100%` 1 core) | **`240.0 FPS` / `1%L 237.9+` / jitter `0.002ms`** | **~82% wait @ 0% CPU** |
| D3D9Ex render queue latency | Stock `3`-frame queue | **`1`-frame hardware Reflex queue** | **`8–33 ms` lower display-pipeline lag** |
| Singleplayer / local-host A\* | Vanilla $O(S^2 \log S)$ | **`FastAStar` $O(N \log N)$** | **25×+ faster** |
| Eye-care visual presets (Vanilla default) | 0.5× LightMap / 1× AF | **9 toggles · 0ms `1.0×` LightMap swap** | **RTX 4060 2K 240Hz `Busy ~2.6ms`, zero VRAM bloat** |

---

## Player Installation & Controls

1. **Subscribe to the Mod**: Subscribe to **HyperSmooth 240FPS & FastLoad** on Steam Workshop (or copy `workshop_mod/dst_smooth_highfps/` to `<DST>/mods/dst_smooth_highfps/`).
2. **Install / Update `bin64/dinput8.dll`** *(Workshop updates never update `bin64/dinput8.dll`)*:
   - **Recommended**: Download & extract `DST_HyperSmooth_v2.6.0_Release.zip`, close DST, and run **`一键安装与更新.bat`** (`[1]` Install/Update with SHA256 check, `[2]` 1-click Restore Vanilla).
   - **Manual**: Copy `bin64/dinput8.dll` into `<Don't Starve Together>/bin64/` alongside `dontstarve_steam_x64.exe`.
3. **In-Game Controls**: Press `c_hsmenu()` in console (or bind a hotkey/mouse side button in `ESC -> Settings -> Controls`) to open the live side panel. Useful diagnostic commands: `c_hsopt("off")`, `c_hsvisual()`, `c_hslatency()`, `c_hsbench()`.

---

## Safety & Open-Source Build

- Lockless passthrough of all 6 `System32\dinput8.dll` exports; strictly gated to `dontstarve_steam(_x64).exe` / `dontstarve_rail(_x64).exe`; W^X `PAGE_EXECUTE_READ` trampolines with zero runtime `.text` writes and fail-closed AOB checks.
- Build zero-dependency pure UCRT `dinput8.dll` from source with Zig (`x86_64-windows-gnu`, `-std=c++20 -O3`) using `tools/build_native.ps1`.
