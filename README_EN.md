<p align="right">
  <a href="README.md">简体中文</a> | <strong>English</strong>
</p>

# DST HyperShade — 240FPS & Visual Engine (formerly DST HyperSmooth)

> **Flagship Client-Only 240Hz Performance Engine, Native OpenGL Rendering Pipeline, Visual Closed-Loop (`HS1-`), and Open `HS_EXT_V1` Shader Extension Platform for Don't Starve Together (64-bit)**  
> Looking for the dedicated server suite (Windows `iphlpapi.dll` & Linux x86_64 `libhypercore.so`)? Switch to branch 👉 [`server-hypercore`](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/tree/server-hypercore) ([Releases](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases))
>
> *Brand upgrade note: Externally branded as **DST HyperShade (`HS`)**, while preserving 100% backward compatibility with the `dst_smooth_highfps` folder, existing `HS1-` share codes, `hypersmooth_slot_1..3.cfg` saves, and `HyperSmooth_*` native Lua APIs.*

---

## Four Core Pillars

1. **240Hz Visual Smoothness & Native Engine Acceleration**
   - **Full-Scene 240Hz Co-Phase Interpolation**: Main-world / VFX / UI 3-channel 2D skeletal tweening, `WorldPos240` (with adaptive network De-Jitter), engine VFX + legacy particle emitters, moving dynamic lights, co-phase dynamic ground shadows, camera follow, `FollowText` bubbles, and 240Hz scroll/minimap zoom.
   - **Native OpenGL ES2 Bridge + 32MB Dual-Track GPU VBPool + Multi-Texture 2D Sprite & UI Batching**: Auto-probes hardware OpenGL with clean D3D9 fallback, slashing render-thread overhead in mega-bases and boss bursts.
   - **RTSS-Grade Frame Pacer (`FrameLimiter::Wait`)** + **Async `libdeflate+Base64` Zero-Stall Save Pipeline** + **`FastJson` / `FastDataDumper` / `FastAStar` / `libdeflate` FastLoad** + **Read-Only Negative Stat Cache (`fs_neg_cache.h`)**.
2. **Ultra-Low-Latency Combat & Causal RPC QoS**
   - **Causal 3-Channel `RakNet::RPC4` QoS + Live Network Telemetry**: Isolates non-combat social/scrapbook traffic onto Ch1/Ch2 so all combat, movement, gear-swap, small ModRPC, and entity-bearing RPCs stay strictly on `Ch0 IMMEDIATE_PRIORITY`; live 500ms Ping & Packet-Loss readout in the HUD.
   - **D3D9Ex 1-Frame Hardware Queue + 240Hz Edge-Triggered Input & Flick-Click Rescue** + **RakNet Zero-Stall Event Wakeup**.
3. **117-Knob Real-Time Visual & Lighting Studio · Built-In 6-Preset Gallery · Transactional `HS1-` Ecosystem (100% Vanilla Pure by Default)**
   - **Built-In 6-Preset Curated Gallery (100% Original HyperShade Tuning — Zero Third-Party Presets)**: 1-click **Presets** drawer from the top bar or Simple mode (or `c_hspreset()`), featuring **Vanilla Pure / Eye-Care HD / Cinematic Storybook / Crisp Daylight / Cozy Firelight / Misty Canopy**—each live-editable with 1-click Undo. **Vanilla Pure strictly keeps Klei's stock `0.25x` BloomSampler untouched so Magiluminescence, torches & fireflies match unmodded DST 1:1 with zero added visual effects.**
   - **Floor 1 (Single-Pass Post-FX Studio)**: BCAS Pro bilateral sharpening, hand-drawn ink deconvolution, AURA anti-overshoot, hi-DPI sprite/ground micro-detail, cinema grading, ASC CDL (Slope/Offset/Power), multi-level glow pyramid & god rays, animated film grain, 8-bit TPDF dithering, and independent reversible vanilla filter overrides (sanity tint, distortion, snow cap, sandstorm dust).
   - **Floors 2 & 3 (Native C++ Independent World-Space Rendering Pipeline)**:
     - **World-Space ShadowMask FBO + 360-Deg Top-3 Point-Light Shadows**: Radial shadows around campfires/firepits/lanterns, weather & season fade, full-moon blue shift, dusk firelight gate, low-clutter height filter, lightning/flash shadows, and auto-restoring vanilla oval shadow suppression;
     - **Coastal-Only True Planar Water Reflections (M3) + PBR Ocean Optics + Volumetric Sunbeams & Fog**: 0.00ms inland/caves planar reflections, 3-octave analytic PBR waves/sky reflection/Mie sun glint, forest/cave & optional all-turf base 3D sunbeams, drifting cloud/canopy shades, and lightmap-lit ground/edge mist;
     - **Tag-Whitelisted Foliage Wind-Sway & Wake (M4) + Zero-Shake Hit-Feel & Native VFX Particles**: Rigid-building-safe GPU foliage sway & walk parting, zero-camera-shake hit-stop/lunge/ground-physics sparks, rising fire embers, ambient motes/spores & turf footsteps;
     - **Crash Detective + Safe Dual-Monitor Cursor Lock (M1 + M2)**: Bilingual `hypersmooth_crash_report.txt` with module+RVA stack & Lua Mod blame; opt-in `ClipCursor` auto-releasing on Pause/Console/Alt+Tab/Crash.
   - **Transactional `HS1-` Share Codes & Human-Readable JSON Preset Library (`hypersmooth_presets.json`)**: Compact CRC-16 share codes with optional UTF-8 titles and Win32 1-click clipboard copy; strict **Validate → Atomic Apply → Persist** transaction guarantees corrupt/truncated/out-of-range codes never overwrite live settings, plus **1-click Undo** and 3 personal custom slots. *(`HS1-` guarantees deterministic parameter-state reproduction; final rendered pixels vary with GPU driver, resolution, game scene, and render backend.)*
   - **Visual Closed-Loop (Zero External Tools — Built into `dinput8.dll`)**:
     - Copy any reference screenshot or image file (`Win+Shift+S` / `Ctrl+C` / ReShade / other games) and click **Match Clipboard** (or run `c_hslook()`): Windows native clipboard (`CF_DIBV5/CF_DIB/CF_HDROP`) + OS WIC decode/downscale on a background thread (paired with non-blocking `glBlitFramebuffer + PBO + glFenceSync` capture on Native OpenGL), automatically separating **General Look Matching** (cross-scene Oklab statistical transfer) from **Same-Scene Exact Matching** (trimmed Robust Huber optimization) with explicit per-feature confidence scores;
     - Maps strictly through the **Capability Whitelist** onto parameters stably owned and controlled by HyperShade's own pipeline, emitting a standard editable, undoable `HS1-` share code;
     - Does not read, parse, or copy third-party mod shaders, assets, or code—extracting only visual statistical features from user-supplied images to fit HyperShade's own presets, substantially reducing IP risk (not a substitute for formal legal advice).
4. **Open `HS_EXT_V1` Standard Shader Extension Platform + Hardware GPU Profiler**
   - **"Compatible with a Standard, not with every individual mod."** Third-party authors write ONLY pure GLSL math (`vec4 hs_main(vec2 uv, vec4 color)`), pack & validate with [tools/hs_pack_shader.py](tools/hs_pack_shader.py), and register declaratively via `GLOBAL.HyperSmooth.RegisterPlugin(spec)` to get:
     - 6 standardized deterministic pipeline stages (`PRE_BLOOM → BLOOM → POST_BLOOM → COLOR_GRADE → POST_SHARPEN → FINAL_FX`), multi-pass `subpasses`, and `exclusive_group` arbitration;
     - Shared zero-duplicate World Context Uniform Bus (`HS_CTX_TIME/SUN/ENV/CAM`);
     - Auto-generated **Tab 8 (`Ext`) live sliders**, 1-click Master Bypass, and per-plugin `pcall` fault-isolation circuit breaker;
     - **Zero-stall 4-frame async `GL_TIME_ELAPSED` hardware GPU cost telemetry (`ms`)** in `dinput8.dll` and automatic `hypersmooth_presets.json` persistence.
   - See [docs/HS_EXT_V1_SPEC.md](docs/HS_EXT_V1_SPEC.md) and the standalone reference plugin mod [sdk/example_hs_crt_vintage/](sdk/example_hs_crt_vintage/).

---

## Automatic Official Language Detection (ZH / EN)

- Automatically matches official DST locale (`zh`/`zhr`/`zht` → Chinese; English & other locales → English) across `modinfo.lua`, Simple/Pro modes, the 6-Preset Gallery, 117 parameter sliders, share & look-match modal, Tab 8 extensions, keybinds, and HUD.
- Supports live zero-restart language switching in-game via `c_hshud("zh" | "en" | "auto")`.

---

## Player Installation & Controls

1. **Install Client Mod**: Subscribe to **DST HyperShade — 240FPS & Visual Engine (formerly HyperSmooth)** on Steam Workshop (or copy `mods/dst_smooth_highfps/` from the release zip into `<DST>/mods/dst_smooth_highfps/`).
2. **Install / Update `bin64/dinput8.dll`** *(Workshop updates never update `bin64/dinput8.dll`; DNH companion `dnh_github` auto-update is also supported)*:
   - **Recommended**: Extract `DST_HyperShade_v2.9.7-beta1_Release.zip`, close DST, and run **`一键安装与更新.bat`** (`[1]` Install/Update with SHA-256 verification, `[2]` 1-click Restore Vanilla).
   - **Manual**: Copy `bin64/dinput8.dll` into `<Don't Starve Together>/bin64/` alongside `dontstarve_steam_x64.exe`.
3. **In-Game Controls**:
   - `c_hsmenu()` (or bind any hotkey/mouse button in `ESC -> Settings -> Controls`) to open the live Visual & Lighting Studio (Simple / Pro / Preset Gallery).
   - `c_hspreset()` / `c_hspreset(1..6 | "id")` to switch across the 6 built-in curated presets; `c_hsshare("Title")` / `c_hsshare("HS1-...")` for transactional share codes; `c_hslook("clipboard"|"general"|"exact"|"frame"|"caps"|"undo")` for the Visual Closed-Loop; `c_hsjson("save"|"load"|"reload", "Name")` for batch JSON preset management; `c_hsext()` for `HS_EXT_V1` extension diagnostics & GPU timing; `c_hsclip()` / `c_hsopt("off")` / `c_hsvisual()` / `c_hslatency()`.

---

## Safety & Open-Source Build

- Lockless passthrough of all 6 `System32\dinput8.dll` exports; process-gated strictly to `dontstarve_steam(_x64).exe` / `dontstarve_rail(_x64).exe`; W^X `PAGE_EXECUTE_READ` trampolines, 61/61 fail-closed AOBs, and null-string `glShaderSource` driver crash protection.
- Build from source with Zig (`x86_64-windows-gnu`, `-std=c++20 -O3`) via `tools/build_native.ps1`, rebuild Klei `.ksh` shaders via `tools/build_shaders.py`, and run full verification with `tools/test_redteam_client.py` + `tools/test_redteam_client.exe` (41/41) + `tools/test_world_shadow_gpu.exe` (13/13 Native GL + ANGLE D3D9) + `tools/test_gpu_clarity.exe` (4/4 GPU Compute Clarity) + `tools/test_client_lua_v24.py` (35/35 real DST Lua 5.1 VM tests, including 3,141-widget monkey test with 0 errors).

---

## Technical Provenance, Architecture Evolution & Licensing

This repository maintains an explicit three-pillar provenance and licensing record:

- **Technical Sources & Independent Development Trail**: [`docs/PROVENANCE_AND_ARCHITECTURE_EN.md`](docs/PROVENANCE_AND_ARCHITECTURE_EN.md) ([简体中文](docs/PROVENANCE_AND_ARCHITECTURE.md))
  - Maps every native rendering, post-processing, batching, and animation module (`world_shadow_engine.h`, `water_reflection_engine.h`, `blue_noise_64.h`, `sprite_batcher.h`, `ui_batcher.h`, `skeletal_fluid.h`, `build_shaders.py`, etc.) to its peer-reviewed papers, textbooks, public talks, and Khronos/Microsoft specifications, alongside our public Git timeline, Architecture Decision Records (ADRs), and 700 KB+ automated CPU/GPU test suites (zero Lua entity/part cloning for shadows or reflections).
- **v4.0 Architecture Roadmap & Clean-Room Research Volumes (ZH)**:
  - [`docs/V4_0_ARCHITECTURE_ROADMAP.md`](docs/V4_0_ARCHITECTURE_ROADMAP.md) — Empirically calibrated v4.0 staged roadmap (Phase 1 persistent-coherent PBO upload ring & dual-thread hitch sampler, Phase 2 driver-sync elimination & SIMD batching, Phase 3+ retained scene mirror, decoupled presentation & next-gen lighting);
  - [`docs/research/REF_VOL1_FACTORIO_FFF.md`](docs/research/REF_VOL1_FACTORIO_FFF.md) — Research Vol.1: Factorio FFF memory latency, read-only parallel render prep, ghost extrapolation guard bands & toroidal scrolling buffers;
  - [`docs/research/REF_VOL2_4_SODIUM_FOLIA_POE2.md`](docs/research/REF_VOL2_4_SODIUM_FOLIA_POE2.md) — Research Vol.2–4: Clean-room architectural notes on Sodium/Iris, Folia/Minestom, and Path of Exile 2 Radiance Cascades mapped to DST.
- **Third-Party Licensing & Asset Provenance Ledger**: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) & [`LICENSE`](LICENSE)
  - Statically linked libraries: `libdeflate v1.23` (MIT), `mimalloc v2.1.7` (MIT);
  - MIT open-source attributions & scope boundaries: `DST-BCAS-Studio` (Copyright 2026 Nanmianyi, MIT License, with automatic runtime coexistence yield via `IsExternalBcasActive()`), `DST-KeyBind-UI` (rtk0c / liolok, MIT License);
  - Every distributed `.ksh`, `.tex`, `.xml`, and blue-noise lookup table is deterministically generated from source scripts inside this repository, with zero redistribution of Klei Entertainment game assets.
