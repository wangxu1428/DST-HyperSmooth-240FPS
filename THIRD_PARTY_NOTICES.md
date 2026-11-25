# Third-Party Notices, Licensing & Asset Provenance Ledger
# 第三方依赖、开源适配与资源授权台账

This document records the exact licensing, copyright holders, integration scope, and origin of every third-party library, open-source algorithm reference, generated binary asset, and runtime game resource used by **DST HyperSmooth 240FPS & Visual Studio** (licensed under the MIT License; see [LICENSE](LICENSE)).

本文档记录 **DST HyperSmooth 240FPS & Visual Studio**（本项目遵循 MIT 许可证，详见 [LICENSE](LICENSE)）所涉及的全部第三方依赖库、开源算法参考、仓库生成素材与游戏运行时资源的精确许可证、版权方、引用范围与隔离边界。

---

## 1. Statically Linked C/C++ Third-Party Libraries / 静态链接 C/C++ 第三方依赖库

| Library / 库名 | Version / 版本 | Copyright Holder / 版权方 | License / 许可证 | Upstream License & Local Build Path / 上游协议与本地构建目录 | Usage Scope / 使用范围 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **libdeflate** | v1.23 | Copyright 2016 Eric Biggers; Copyright 2024 Google LLC | **MIT License** | [Upstream `COPYING`](https://github.com/ebiggers/libdeflate/blob/master/COPYING) (local build: `third_party/libdeflate-1.23/COPYING`) | Statically compiled into `dinput8.dll` for SIMD-accelerated ZIP/zlib decompression (`unzReadCurrentFile`, `uncompress`) and save compression (`compress2`, `klei_save_codec.h`). |
| **mimalloc** | v2.1.7 | Copyright (c) 2018-2021 Microsoft Corporation, Daan Leijen | **MIT License** | [Upstream `LICENSE`](https://github.com/microsoft/mimalloc/blob/master/LICENSE) (local build: `third_party/mimalloc-2.1.7/LICENSE`) | Statically compiled into `dinput8.dll` as an opt-in hermetic lockless heap allocator for the DST Lua 5.1 VM (`lua_newstate`); auto-yields when `DontStarveLuaJIT2` is detected. |

---

## 2. Third-Party Open-Source Code & Design Adaptations / 第三方开源代码与设计适配

| Upstream Project / 上游项目 | Copyright Holder / 版权方 | License / 许可证 | Upstream URL / 上游地址 | Exact Scope & Boundary / 精确引用范围与边界 |
| :--- | :--- | :--- | :--- | :--- |
| **DST-BCAS-Studio** | Copyright 2026 Nanmianyi (楠眠已) | **MIT License** | https://github.com/Nanmianyi/DST-BCAS-Studio | 1. **Post-process shader formulas** in [`tools/build_shaders.py`](tools/build_shaders.py): YCoCg Bilateral Adaptive Sharpening (BCAS), asymmetric ink deconvolution, AURA anti-overshoot envelope, cinematic ACES grading layout, and identity-LUT ColourCube scaling.<br>2. **Legacy v2.8.0 Bloom blue-channel shadow separation (`Contract v17`)** in [`tools/build_shaders.py`](tools/build_shaders.py) and [`native_plugin/dinput8.cpp`](native_plugin/dinput8.cpp): originally adapted in v2.8.0 and superseded in upcoming v2.9.x by this project's independent native C++ World-Space Orthographic ShadowMask FBO (`native_plugin/world_shadow_engine.h`, upcoming v2.9.x); retained solely as a fail-closed fallback if FBO initialization fails.<br>3. **Runtime Coexistence**: [`workshop_mod/dst_smooth_highfps/modmain.lua`](workshop_mod/dst_smooth_highfps/modmain.lua) (`IsExternalBcasActive()`) detects standalone `DST-BCAS-Studio` and automatically yields post-processing to avoid conflicts. |
| **DST-KeyBind-UI** | rtk0c / liolok | **MIT License** | https://github.com/liolok/DST-KeyBind-UI<br>https://github.com/rtk0c/dont-starve-mods/tree/master/KeybindMagic | Interaction design inspiration for the click-to-record hotkey widgets in [`workshop_mod/dst_smooth_highfps/keybind.lua`](workshop_mod/dst_smooth_highfps/keybind.lua) (Mod Config & `ESC -> Settings -> Controls`), independently re-implemented and uniquely namespaced for conflict-free coexistence with upstream keybind mods. |

---

## 3. Repository-Generated Assets & Shaders / 仓库源码独立生成的资产与着色器

All packaged `.tex`, `.xml`, `.ksh`, and lookup-table assets distributed with this mod are **100% deterministically generated from source scripts inside this repository** — zero third-party textures, sprites, or precompiled binaries are copied or redistributed:

本模组打包分发的全部 `.tex`、`.xml`、`.ksh` 及噪声查找表均由**本仓库内源码脚本 100% 确定性生成**，不包含任何第三方贴图、美术素材或来源不明的二进制文件：

| Artifact / 产物 | Generator Source / 生成脚本与源码 | License / 许可归属 |
| :--- | :--- | :--- |
| `workshop_mod/dst_smooth_highfps/shaders/hs_kawase_down.ksh`<br>`workshop_mod/dst_smooth_highfps/shaders/hs_kawase_up.ksh`<br>`workshop_mod/dst_smooth_highfps/shaders/hs_postprocess_bloom.ksh`<br>`workshop_mod/dst_smooth_highfps/shaders/hs_visual_final.ksh` | [`tools/build_shaders.py`](tools/build_shaders.py) | Project MIT + MIT notices in Section 2 |
| `workshop_mod/dst_smooth_highfps/images/hs_cloud_shade.tex` *(upcoming v2.9.x)* | `tools/build_cloud_tex.py` *(upcoming v2.9.x, procedural multi-octave noise in Klei KTEX DXT5 format)* | Project MIT (100% original procedural asset) |
| `workshop_mod/dst_smooth_highfps/modicon.tex`<br>`workshop_mod/dst_smooth_highfps/modicon.xml`<br>`workshop_preview.png` | [`tools/build_modicon.py`](tools/build_modicon.py) | Project MIT (original cover asset) |
| `native_plugin/blue_noise_64.h` *(upcoming v2.9.x)* | Precomputed 64×64 Toroidal Void-and-Cluster Blue Noise table (Ulichney 1993 algorithm) | Project MIT (mathematical lookup table) |
| `sdk/example_hs_crt_vintage/shaders/hs_ext_vintage_crt.ksh` | [`tools/hs_pack_shader.py`](tools/hs_pack_shader.py) + [`sdk/example_hs_crt_vintage/src/vintage_crt.frag`](sdk/example_hs_crt_vintage/src/vintage_crt.frag) | Project MIT (SDK sample shader) |

---

## 4. Don't Starve Together Runtime Game Assets / 《饥荒联机版》官方运行时资源边界

Certain client-side Lua modules (such as [`workshop_mod/dst_smooth_highfps/hs_panel.lua`](workshop_mod/dst_smooth_highfps/hs_panel.lua), [`keybind.lua`](workshop_mod/dst_smooth_highfps/keybind.lua), and upcoming v2.9.x `hs_vollight.lua` / `hs_vfx.lua`) reference built-in Klei game asset paths (e.g., `anim/lightrays.zip`, `images/tree.tex`, `images/global.xml`, `images/global_redux.xml`, `fx/*.tex`) strictly at runtime inside the player's own local copy of *Don't Starve Together*.

- **Zero redistribution**: No Klei Entertainment copyrighted textures, animations, audio, or script archives are bundled inside this repository or its releases.
- All Klei asset paths remain the property of Klei Entertainment Inc. and are invoked solely through official *Don't Starve Together* modding APIs.

---

## 5. Related Provenance Documents / 关联溯源文档

- Full Technical Provenance, Literature Mapping & Independent Evolution Record (Chinese): [`docs/PROVENANCE_AND_ARCHITECTURE.md`](docs/PROVENANCE_AND_ARCHITECTURE.md)
- Full Technical Provenance, Literature Mapping & Independent Evolution Record (English): [`docs/PROVENANCE_AND_ARCHITECTURE_EN.md`](docs/PROVENANCE_AND_ARCHITECTURE_EN.md)
- Root License: [`LICENSE`](LICENSE)
