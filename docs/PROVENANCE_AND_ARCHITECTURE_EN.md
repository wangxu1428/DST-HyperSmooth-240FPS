<p align="right">
  <a href="PROVENANCE_AND_ARCHITECTURE.md">简体中文</a> | <strong>English</strong>
</p>

# Technical Provenance, Independent Development Trail & Licensing Compliance

This document provides a verifiable, three-pillar engineering provenance record for **DST HyperSmooth 240FPS & Visual Studio**:
1. **Technical Sources**: Peer-reviewed graphics papers, standard textbooks, public industry talks, and API specifications mapped to their exact modules in this repository.
2. **Independent Development Trail**: Public Git commit timeline, Architecture Decision Records (ADRs), and reproducible automated CPU/GPU test suites.
3. **Licensing & Asset Ledger**: Complete third-party dependency, open-source adaptation, generated asset, and runtime game resource boundaries (full legal ledger in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) and [`LICENSE`](../LICENSE)).

---

## 1. Technical Sources (Papers, Textbooks, Public Talks → Implementation Modules)

### 1.1 Native Rendering Pipeline (`native_plugin/`, C++20 / OpenGL ES 2.0–3.2 / WGL / DXGI)

| Module | Engineering Responsibility | Published Literature / Textbooks / Specifications |
| :--- | :--- | :--- |
| `native_plugin/world_shadow_engine.h` *(upcoming v2.9.x)* | World-Space Top-Down Orthographic RGBA8 ShadowMask FBO (`GL_TEXTURE14`), texel-snapped anti-shimmer bounds, `GL_MAX` / `DEPTH16` anti-overlap, height-adaptive 9-tap Vogel-disk contact-hardening penumbra, 360° side-light billboard reorientation, 3-light near-field point shadows, and world-space ground-plane (`y=0.04`) compositing before standing entities | • Lance Williams, *"Casting Curved Shadows on Curved Surfaces"*, **SIGGRAPH 1978** (Shadow Mapping)<br>• Randima Fernando, *"Percentage-Closer Soft Shadows (PCSS)"*, **SIGGRAPH 2005 Sketches** (Contact-hardening soft shadows)<br>• Tomas Akenine-Möller, Eric Haines, Naty Hoffman et al., *Real-Time Rendering (4th Ed.)*, **Ch. 7 Shadows** (Orthographic shadow projection, texel snapping, penumbra filtering) |
| `native_plugin/blue_noise_64.h` *(upcoming v2.9.x)* | Precomputed 64×64 toroidal 2-channel blue-noise table (`GL_TEXTURE15`) for per-pixel penumbra sample rotation | • Robert Ulichney, *"The Void-and-Cluster Method for Dither Array Generation"*, **SPIE 1993** |
| `native_plugin/water_reflection_engine.h` *(upcoming v2.9.x)* | Coastal-gated 512×512 RGBA8 + DEPTH16 Planar Water Reflection FBO (`GL_TEXTURE12`) using perspective planar mirror projection `s = eyeY / (eyeY + h * lengthScale)` across `y=0` | • *Real-Time Rendering (4th Ed.)*, **Planar Reflections**<br>• *OpenGL Programming Guide (Red Book)*, planar mirror projection |
| [`native_plugin/sprite_batcher.h`](../native_plugin/sprite_batcher.h)<br>[`native_plugin/ui_batcher.h`](../native_plugin/ui_batcher.h)<br>[`native_plugin/vb_pool.h`](../native_plugin/vb_pool.h)<br>[`native_plugin/gl_state_cache.h`](../native_plugin/gl_state_cache.h) | 8-slot multi-texture 2D Sprite & UI/Font Uber-Batchers, AZDO linear streaming append ring with lockless buffer orphaning (`GL_MAP_WRITE_BIT \| GL_MAP_INVALIDATE_BUFFER_BIT`), 32 MB dual-track VBO pool, and GL state/uniform shadow cache | • Cass Everitt, Graham Sellers, John McDonald, Tim Foley, *"Approaching Zero Driver Overhead in OpenGL (AZDO)"*, **GDC 2014**<br>• Khronos Group, *OpenGL ES 3.0 Specification*, **§6.3.2 Mapping and Unmapping Buffer Data** |
| [`native_plugin/native_gl_bridge.h`](../native_plugin/native_gl_bridge.h)<br>`native_plugin/dxgi_flip_present.h` *(upcoming v2.9.x)* | Native WGL OpenGL ES 2.0 bridge, DXGI Flip presentation path, ANGLE D3D9Ex `SetMaximumFrameLatency(1)`, hardware 16× AF unlock, and native `MipLodBias` | • Khronos Group, *OpenGL ES 2.0 / EGL 1.4 / WGL_EXT_create_context_es2_profile* Specifications<br>• Microsoft Learn, *DXGI Flip Model* & *IDirect3DDevice9Ex::SetMaximumFrameLatency* |
| [`native_plugin/dinput8.cpp`](../native_plugin/dinput8.cpp) (HD-2D world shader section) | Screen-space derivative micro-normals, 3-wave chain-rule ocean glints, 0-tap entity ground-contact AO, and directional rim lighting in GLSL ES 1.00 | • Khronos `GL_OES_standard_derivatives` (`dFdx`/`dFdy`)<br>• Standard Blinn-Phong specular & analytical chain-rule wave normals |

### 1.2 Post-Process Shader Pipeline (`tools/build_shaders.py`)

| Artifact | Engineering Responsibility | Published Literature / Standards / MIT Attribution |
| :--- | :--- | :--- |
| `hs_kawase_down.ksh`<br>`hs_kawase_up.ksh`<br>`hs_postprocess_bloom.ksh` | 5-tap Karis-weighted downsample, multi-level Dual-Kawase glow pyramid, soft-knee threshold compression, radial crepuscular sun shafts | • Marius Bjørge (ARM), *"Bandwidth-Efficient Rendering"*, **SIGGRAPH 2015** (Dual-Kawase Blur)<br>• Brian Karis (Epic Games), *"Next Generation Post Processing in Call of Duty: Advanced Warfare"*, **SIGGRAPH 2014** (Karis Average)<br>• Kenny Mitchell, *"Volumetric Light Scattering as a Post-Process"*, **GPU Gems 3 (2007)** |
| `hs_visual_final.ksh` | ACES filmic tone mapping, ASC CDL (Slope/Offset/Power), 8-bit TPDF dithering; YCoCg bilateral adaptive sharpening, ink deconvolution, and AURA anti-overshoot | • Krzysztof Narkowicz, *"ACES Filmic Tone Mapping Curve"* (2015) / Academy ACES<br>• American Society of Cinematographers, *ASC Color Decision List (CDL)*<br>• Mikkel Gjøl (Playdead *Inside*), *"Low Complexity, High Fidelity: The Rendering of INSIDE"*, **GDC 2016** (TPDF Dithering)<br>• C. Tomasi & R. Manduchi, *"Bilateral Filtering for Gray and Color Images"*, **ICCV 1998** & AMD FidelityFX CAS (2019)<br>• **MIT Attribution**: YCoCg BCAS sharpening / ink deconvolution / AURA and legacy v2.8.0 `Contract v17` blue-channel shadow fallback adapted from [`DST-BCAS-Studio`](https://github.com/Nanmianyi/DST-BCAS-Studio) (Copyright 2026 Nanmianyi, MIT License); see [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) |

### 1.3 240Hz Animation, Combat Presentation & Engine Infrastructure

| Module | Engineering Responsibility | Published Algorithms & Specifications |
| :--- | :--- | :--- |
| `native_plugin/skeletal_fluid.h`<br>`native_plugin/world_pos_smooth.h`<br>`native_plugin/combat_impact.h` *(upcoming v2.9.x)* | Cross-clip polar bone crossfading (A1), 4-keyframe C1 monotone PCHIP spline interpolation (A2), main-thread PLL snapshot-timeline smoothing, render-only zero-input-lag visual hit-stop & underdamped spring squash-and-stretch (B1–B4) | • F. N. Fritsch & R. E. Carlson, *"Monotone Piecewise Cubic Interpolation"*, **SIAM J. Numer. Anal. 17(2), 1980** (PCHIP)<br>• Standard 2×2 polar decomposition & underdamped second-order harmonic oscillator |
| `native_plugin/foliage_field.h`<br>`workshop_mod/dst_smooth_highfps/hs_vollight.lua`<br>`workshop_mod/dst_smooth_highfps/hs_vfx.lua` *(upcoming v2.9.x)* | Whitelist GPU vertex wind & player-parting field, client-only 3D volumetric light rays & native `ShadeRenderer` drifting cloud/canopy shade, pure client-side GPU VFX particles | • Dual-frequency procedural vertex wind displacement; invokes official client `ShadeRenderer` / `VFXEffect` APIs with procedurally generated `tools/build_cloud_tex.py` *(upcoming v2.9.x)* cloud mask |
| [`native_plugin/fast_astar.h`](../native_plugin/fast_astar.h)<br>[`native_plugin/fast_json.h`](../native_plugin/fast_json.h)<br>[`native_plugin/fast_dumper.h`](../native_plugin/fast_dumper.h)<br>[`native_plugin/rpc_qos.h`](../native_plugin/rpc_qos.h)<br>[`native_plugin/async_save_commit.h`](../native_plugin/async_save_commit.h) | `O(N log N)` flat-hash + binary-heap A\* pathfinding, native RFC 8259 JSON encoder/decoder, causal 3-channel RPC QoS classifier, async atomic save-commit queue | • Hart, Nilsson & Raphael, *"A Formal Basis for the Heuristic Determination of Minimum Cost Paths"*, **IEEE TSSC 1968** (A\*)<br>• IETF **RFC 8259** (JSON) |

> **Architectural Boundary Statement**:  
> The upcoming v2.9.x real-time ground shadow engine (`world_shadow_engine.h`) and planar water reflection engine (`water_reflection_engine.h`) operate exclusively via dedicated off-screen FBOs inside the native C++ OpenGL ES 2.0 IAT layer. This codebase contains **zero Lua entity duplication or sprite/part cloning** for shadows or reflections.

---

## 2. Independent Development Trail (Git History, ADRs & Reproducible Test Suites)

### 2.1 Public Git Milestone History

Verifiable directly via `git log`:

| Commit | Date | Release | Key Milestones |
| :--- | :--- | :--- | :--- |
| `b735760` | 2026-11-21 | `v2.2.0` | Full-world 240FPS skeletal tweening, anti-stutter GC pacer, `libdeflate` 2.5× FastLoad, high-FPS UI fixes |
| `00ef7d6` | 2026-11-21 | `v2.2.1` | 0ms Lua VM hotkey bridge, `ALT_RENDERPATH` crash guard, third-party mod compatibility |
| `1ff9e60` | 2026-11-22 | `v2.4.0` | RTSS waitable-timer frame pacer, `mimalloc` Lua heap, `WorldPos240`, RakNet zero-stall wakeup, 60Hz handshake |
| `f11c48d` / `c8444df` | 2026-11-22 | `v2.4.1` | Firefly FP16 multi-particle matcher, cross-`SimReset` zero-reload cave asset cache, interactive any-key UI |
| `e084aff` | 2026-11-22 | `v2.5.0` | Player-First visual fidelity foundation (100% vanilla default, independent opt-in toggles, 0-tap world shaders, 16× AF, 1.0× LightMap) |
| `d0faefe` / `1f3e57f` / `765f5ca` | 2026-11-23 | `v2.6.0.1` | 240Hz particles/lights/camera, zero-hitch crafting menu, live sidebar, GL uniform shadow cache, FS negative stat cache, De-Jitter |
| `18d6cb6` | 2026-11-24 | `v2.8.0` | Native WGL ES2 bridge, 32 MB VBPool, 8-slot Sprite/UI batchers, causal RPC QoS, async save, 80-knob Visual Studio, `HS_EXT_V1` shader platform |
| *v2.9.x evolution* | 2026-11-24 – 25 | `v2.9.0` – `v2.9.5` | **Independent native World-Space ShadowMask FBO replacing legacy Bloom-channel shadows**, multi-light point shadows, coastal planar water reflections, whitelist foliage wind field, DXGI Flip present, Track A PCHIP fluid skeletal engine, Track B zero-shake hit-feel, client volumetric lighting & native GPU VFX, AZDO 4 MB UI append ring (GLES 3.0 §6.3.2 VRAM race fix), lazy `OptionsScreen` controls |

### 2.2 Architecture Decision Records (ADRs)

- **ADR-1 (v2.9.0): Migrating from v2.8.0 MIT-Adapted Bloom Blue-Channel Shadows to Native World-Space ShadowMask FBO**
  - In v2.8.0, shadows temporarily used the MIT-licensed `Contract v17` Bloom blue-channel separation from `DST-BCAS-Studio`. Live testing highlighted five structural constraints of any screen-space Bloom-channel encoding (low 0.06 blue-channel quantization, foreground entity clipping, bypassing vanilla `ColourCube` LUT tinting, bloom coupling, and thin side-light silhouettes).
  - In v2.9.0, we engineered an independent native C++ **World-Space Top-Down Orthographic RGBA8 ShadowMask FBO** (`world_shadow_engine.h`, upcoming v2.9.x) composited at `y=0.04` after terrain and strictly before standing entities, verified pixel-exact on both Native OpenGL (`GL_MAX`) and ANGLE D3D9 (`DEPTH16`). The legacy Bloom blue-channel code remains solely as a fail-closed fallback if FBO creation fails.
- **ADR-2: AZDO Linear Streaming Append Ring in `ui_batcher.h`**
  - Live debugging proved that combining `GL_MAP_UNSYNCHRONIZED_BIT` with offset-0 reuse across 4 ring slots violated GLES 3.0 §6.3.2 when >4 flushes occurred per frame, overwriting in-flight GPU VRAM. Replaced with a 4 MB / 256 KB linear disjoint append ring using pure `GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT` driver orphaning on wrap-around, plus fixed cross-thread RenderThread small-VBO shadow capture to eliminate `GL_MAP_READ_BIT` stalls.
- **ADR-3: Open `HS_EXT_V1` Standard & Runtime Coexistence**
  - Formalized [`docs/HS_EXT_V1_SPEC.md`](HS_EXT_V1_SPEC.md) so third-party shader mods interoperate via a documented standard, and implemented `IsExternalBcasActive()` in [`modmain.lua`](../workshop_mod/dst_smooth_highfps/modmain.lua) to yield cleanly whenever standalone `DST-BCAS-Studio` is co-loaded.

### 2.3 Independent Reproducible Verification Suites

Comprehensive automated tests and live-GPU FBO pixel-readback harnesses live inside `tools/`:

| Suite | Coverage | Command |
| :--- | :--- | :--- |
| [`tools/test_redteam_client.cpp`](../tools/test_redteam_client.cpp) | **24 (published v2.8.0) / 28 (upcoming v2.9.x)** C++20 unit & live-GPU tests (W^X, RTSS pacer, GC, skeletal/VFX/world-pos smoothing, A\* concurrency, Native WGL ES2, VBPool, SpriteBatcher, UiBatcher, etc.) | Compile with Zig C++20 & run `tools/test_redteam_client.exe` |
| `tools/test_world_shadow_gpu.cpp` *(upcoming v2.9.x)* | **Dual-backend (Native OpenGL + ANGLE D3D9) live-GPU FBO verification** for World ShadowMask, Multi-Light Point Shadows, Planar Water Reflections, DXGI Flip, Mock-GL state machine, dual-context, and concurrent save stress | Compile with Zig C++20 & run `tools/test_world_shadow_gpu.exe` |
| [`tools/test_client_lua_v24.py`](../tools/test_client_lua_v24.py) | **18 (published v2.8.0) / 26 (upcoming v2.9.x)** end-to-end tests executed inside the real 64-bit DST Lua 5.1 VM | `python tools/test_client_lua_v24.py` |
| [`tools/test_redteam_client.py`](../tools/test_redteam_client.py) | PE 6/6 export check, pure UCRT import check, lockless `System32` passthrough, and **58/58** unique client & ANGLE AOB verification | `python tools/test_redteam_client.py` |
| [`tools/test_spike_blackbox.py`](../tools/test_spike_blackbox.py) | Spike Blackbox ring-buffer & root-cause classification regression tests | `python tools/test_spike_blackbox.py` |

---

## 3. Third-Party Licensing & Asset Provenance Ledger

Complete copyright notices, license terms, generator scripts for every `.ksh`/`.tex`/`.xml` asset, and runtime Klei asset boundaries are maintained in:
- [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)
- [`LICENSE`](../LICENSE)
