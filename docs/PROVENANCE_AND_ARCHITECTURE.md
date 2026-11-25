<p align="right">
  <strong>简体中文</strong> | <a href="PROVENANCE_AND_ARCHITECTURE_EN.md">English</a>
</p>

# 技术溯源、独立演进与开源合规白皮书
# Technical Provenance, Independent Development Trail & Compliance

本文档系统记录 **DST HyperSmooth 240FPS & Visual Studio** 的三项核心合规依据：
1. **技术来源**：每一项核心图形学、渲染管线、动画与系统优化模块所对应的公开学术论文、教材、行业演讲与规范；
2. **独立开发记录**：基于 Git 公开提交历史、架构演进决策（ADR）与可复现自动化测试套件的完整演进链条；
3. **授权记录**：第三方依赖库、MIT 适配代码、仓库自生成素材与游戏官方运行时资源的清晰权属边界（详见根目录 [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) 与 [`LICENSE`](../LICENSE)）。

---

## 一、技术来源（公开论文、教材、演讲 ↔ 仓库实现模块）

本项目的全部核心渲染管线与算法均建立在公开计算机图形学文献、标准图形 API 规范及经典算法理论之上。下表按模块列出精确对应关系：

### 1.1 原生图形渲染管线（C++20 / OpenGL ES 2.0–3.2 / WGL / DXGI）

| 仓库实现模块 | 核心技术职责 | 公开学术论文 / 教材 / 行业规范出处 |
| :--- | :--- | :--- |
| `native_plugin/world_shadow_engine.h` *(v2.9.x 待发布)* | 世界空间顶视正交 RGBA8 ShadowMask FBO（`GL_TEXTURE14`）、Texel-Snapped 像素对齐防抖、`GL_MAX` / `DEPTH16` 去重叠、高度自适应 9-Tap Vogel Disk 接触硬化半影、360° 侧光公告牌重定向、3 盏近场点光源投影、世界空间地面层（`y=0.04`）前向合成 | • Lance Williams, *"Casting Curved Shadows on Curved Surfaces"*, **SIGGRAPH 1978**（Shadow Mapping 经典模型）<br>• Randima Fernando, *"Percentage-Closer Soft Shadows (PCSS)"*, **SIGGRAPH 2005 Sketches**（NVIDIA 接触硬化软阴影）<br>• Tomas Akenine-Möller, Eric Haines, Naty Hoffman 等, *Real-Time Rendering (4th Edition)*, **Ch. 7 Shadows**（正交投影阴影、Texel Snapping 与半影滤波）<br>• Vogel Disk 螺旋采样与硬件 `GL_MAX` / Depth 去重 |
| `native_plugin/blue_noise_64.h` *(v2.9.x 待发布)* | 64×64 可平铺双通道蓝噪声查找表（`GL_TEXTURE15`，驱动阴影软化旋转去噪） | • Robert Ulichney, *"The Void-and-Cluster Method for Dither Array Generation"*, **SPIE 1993** |
| `native_plugin/water_reflection_engine.h` *(v2.9.x 待发布)* | 海岸门控 512×512 平面镜面反射 FBO（`GL_TEXTURE12`），按真实透视平面投影公式 `s = eyeY / (eyeY + h * lengthScale)` 在 `y=0` 水面成像并按视距深度排序 | • *Real-Time Rendering (4th Edition)* 经典 **Planar Reflection** 平面反射投影<br>• *OpenGL Programming Guide (Red Book)* 镜面平面反射模型 |
| [`native_plugin/sprite_batcher.h`](../native_plugin/sprite_batcher.h)<br>[`native_plugin/ui_batcher.h`](../native_plugin/ui_batcher.h)<br>[`native_plugin/vb_pool.h`](../native_plugin/vb_pool.h)<br>[`native_plugin/gl_state_cache.h`](../native_plugin/gl_state_cache.h) | 8 槽位多纹理 2D 精灵与 UI/字体 Uber-Batcher、AZDO 线性流式追加环与驱动页重命名（`GL_MAP_WRITE_BIT \| GL_MAP_INVALIDATE_BUFFER_BIT`）、32MB 双轨动态 VBO 池、状态与 Uniform 影子去重缓存 | • Cass Everitt, Graham Sellers, John McDonald, Tim Foley, *"Approaching Zero Driver Overhead in OpenGL (AZDO)"*, **GDC 2014**<br>• Khronos Group, *OpenGL ES 3.0 Specification*, **§6.3.2 Mapping and Unmapping Buffer Data**（消除 `GL_MAP_UNSYNCHRONIZED_BIT` 显存数据竞争） |
| [`native_plugin/native_gl_bridge.h`](../native_plugin/native_gl_bridge.h)<br>`native_plugin/dxgi_flip_present.h` *(v2.9.x 待发布)* | 原生 WGL OpenGL ES 2.0 硬件直通桥接、DXGI Flip Model 零拷呈现、ANGLE D3D9Ex `SetMaximumFrameLatency(1)` 与硬件 16× AF / MipLodBias | • Khronos Group, *OpenGL ES 2.0 / EGL 1.4 / WGL_EXT_create_context_es2_profile* 官方规范<br>• Microsoft Learn, *DXGI Flip Model* 与 *IDirect3DDevice9Ex::SetMaximumFrameLatency* 官方文档 |
| [`native_plugin/dinput8.cpp`](../native_plugin/dinput8.cpp)（HD-2D 世界着色器段） | 世界地形/海洋/立绘 GLSL ES 1.00 屏幕空间导数微法线、三波链式法则水波反光、0-Tap 接地接触 AO 与边缘轮廓光 | • Khronos `GL_OES_standard_derivatives` (`dFdx`/`dFdy`) 屏幕空间法线重建<br>• 经典 Blinn-Phong 高光与链式法则（Chain Rule）解析波浪法线 |

### 1.2 后处理着色器管线（`tools/build_shaders.py`）

| 仓库实现模块 | 核心技术职责 | 公开学术论文 / 行业标准 / 开源授权出处 |
| :--- | :--- | :--- |
| `hs_kawase_down.ksh`<br>`hs_kawase_up.ksh`<br>`hs_postprocess_bloom.ksh` | 5-Tap Karis 防闪烁加权下采样、多级 Dual-Kawase 辉光金字塔、Soft-Knee 阈值压缩、径向丁达尔光束 | • Marius Bjørge (ARM), *"Bandwidth-Efficient Rendering"*, **SIGGRAPH 2015**（Dual-Kawase Blur）<br>• Brian Karis (Epic Games), *"Next Generation Post Processing in Call of Duty: Advanced Warfare"*, **SIGGRAPH 2014**（Karis Average 防高光闪烁）<br>• Kenny Mitchell, *"Volumetric Light Scattering as a Post-Process"*, **GPU Gems 3 (2007)**（屏幕空间径向光束） |
| `hs_visual_final.ksh`（调色 / 抖动 / 锐化） | ACES 电影色调映射、ASC CDL（Slope/Offset/Power）、8-bit TPDF 抗色带抖动；YCoCg 双边自适应锐化、手绘墨线反卷积与 AURA 防过冲 | • Krzysztof Narkowicz, *"ACES Filmic Tone Mapping Curve"* (2015) / Academy ACES<br>• American Society of Cinematographers, *ASC Color Decision List (CDL)* 标准<br>• Mikkel Gjøl (Playdead *Inside*), *"Low Complexity, High Fidelity: The Rendering of INSIDE"*, **GDC 2016**（TPDF 三角概率分布抖动）<br>• C. Tomasi & R. Manduchi, *"Bilateral Filtering for Gray and Color Images"*, **ICCV 1998** & AMD FidelityFX CAS (2019)<br>• **MIT 合法适配署名**：YCoCg BCAS 锐化/墨线反卷积/AURA 及 v2.8.0 过渡期 `Contract v17` 蓝通道阴影回退适配自 [`DST-BCAS-Studio`](https://github.com/Nanmianyi/DST-BCAS-Studio)（Copyright 2026 Nanmianyi / 楠眠已，MIT License），详见 [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) |

### 1.3 240Hz 动画、物理手感与客户端引擎基础设施

| 仓库实现模块 | 核心技术职责 | 公开算法与技术出处 |
| :--- | :--- | :--- |
| `native_plugin/skeletal_fluid.h`<br>`native_plugin/world_pos_smooth.h`<br>`native_plugin/combat_impact.h` *(v2.9.x 待发布)* | 跨动作骨骼极分解融合（A1）、4 关键帧单调保形三次 Hermite 样条插值（A2 PCHIP）、主线程锁相快照时间轴平滑（PLL De-Jitter）、零输入延迟渲染层受击顿帧与欠阻尼弹簧挤压拉伸（B1–B4） | • F. N. Fritsch & R. E. Carlson, *"Monotone Piecewise Cubic Interpolation"*, **SIAM J. Numer. Anal. 17(2), 1980**（PCHIP 保形防过冲插值）<br>• 经典 2×2 极分解（Polar Decomposition）与欠阻尼二阶弹簧微分方程（Underdamped Harmonic Oscillator） |
| `native_plugin/foliage_field.h`<br>`workshop_mod/dst_smooth_highfps/hs_vollight.lua`<br>`workshop_mod/dst_smooth_highfps/hs_vfx.lua` *(v2.9.x 待发布)* | 白名单植被 GPU 顶点风场摇曳与角色拨草、客户端原生体积光与 `ShadeRenderer` 漂移云影/林间斑驳光影、纯客户端原生 GPU 粒子特效 | • 程序化双频正弦风场位移；仅调用科雷官方客户端 `ShadeRenderer` / `VFXEffect` API 与程序化生成云影贴图（`tools/build_cloud_tex.py`，v2.9.x 待发布） |
| [`native_plugin/fast_astar.h`](../native_plugin/fast_astar.h)<br>[`native_plugin/fast_json.h`](../native_plugin/fast_json.h)<br>[`native_plugin/fast_dumper.h`](../native_plugin/fast_dumper.h)<br>[`native_plugin/rpc_qos.h`](../native_plugin/rpc_qos.h)<br>[`native_plugin/async_save_commit.h`](../native_plugin/async_save_commit.h) | `O(N log N)` 扁平哈希+二叉堆 A\* 寻路、原生 RFC 8259 JSON 编解码、因果安全 3 通道 RPC QoS 队列、异步原子存盘队列 | • Hart, Nilsson & Raphael, *"A Formal Basis for the Heuristic Determination of Minimum Cost Paths"*, **IEEE TSSC 1968**（A\* 算法）<br>• IETF **RFC 8259** (JSON 规范) |

> **关键架构边界声明（纯技术事实）**：  
> 本项目 v2.9.x（待发布分支）的实时世界投影（`world_shadow_engine.h`）与水面倒影（`water_reflection_engine.h`）完全在原生 C++ OpenGL ES 2.0 IAT 渲染管线层通过独立离屏 FBO 实现，**不使用任何 Lua 实体复制、部件克隆或场景树复制技术**。

---

## 二、独立开发记录（Git 提交历史、架构演进决策与实机测试证据）

### 2.1 公开 Git 里程碑时间线

以下提交记录均可在仓库 Git 历史中通过 `git log` 直接核验：

| Commit | 日期 | 版本 | 核心演进内容 |
| :--- | :--- | :--- | :--- |
| `b735760` | 2026-11-21 | `v2.2.0` | 全世界 240FPS 骨骼补帧、抗卡顿分帧 GC、`libdeflate` 2.5× 极速加载、高帧率 UI 适配 |
| `00ef7d6` | 2026-11-21 | `v2.2.1` | 0ms Lua VM 热键直连桥接、`ALT_RENDERPATH` 防崩校验、第三方 Mod 兼容 |
| `1ff9e60` | 2026-11-22 | `v2.4.0` | RTSS 零争抢稳帧器、`mimalloc` 隔离 Lua 堆、`WorldPos240`、RakNet 零停顿事件唤醒、60Hz 握手协议 |
| `f11c48d` / `c8444df` | 2026-11-22 | `v2.4.1` | 萤火虫多粒子 FP16 解码与防拉丝、洞穴↔地面零重载资产保活、交互式改键 UI |
| `e084aff` | 2026-11-22 | `v2.5.0` | 玩家优先护眼画质底座（默认 100% 原版纯净、独立分项开关、0-Tap 世界着色器、16× AF、1.0× 光照图） |
| `d0faefe` / `1f3e57f` / `765f5ca` | 2026-11-23 | `v2.6.0.1` | 240Hz 粒子/动态光/镜头、制作栏零卡顿、实时侧栏微调、Uniform 影子缓存、负向 Stat 缓存、De-Jitter |
| `18d6cb6` | 2026-11-24 | `v2.8.0` | Native WGL ES2 硬件直通、32MB VBPool、8 槽位精灵/UI 合批器、因果 RPC QoS、异步存盘、80 旋钮工坊、`HS_EXT_V1` 开放着色器扩展标准 |
| *v2.9.x 演进线* | 2026-11-24 ~ 25 | `v2.9.0` – `v2.9.5` | **原生世界空间正交 ShadowMask FBO 取代旧版 Bloom 通道阴影**、多点光源投影、海岸门控平面水面倒影 FBO、白名单风场植被、DXGI Flip 呈现、Track A 流体骨骼 PCHIP、Track B 零震屏打击感、客户端体积光/流云与原生粒子引擎、AZDO 4MB UI 追加环显存防竞争修复、`OptionsScreen` 懒加载 |

### 2.2 关键架构演进决策记录（ADR）

- **ADR-1：为何在 v2.9.0 独立构建世界空间正交 ShadowMask FBO（`world_shadow_engine.h`，v2.9.x 待发布）**
  - v2.8.0 早期为兼顾零额外 Pass，依据 MIT 协议适配了 `DST-BCAS-Studio` 的 `Contract v17` Bloom 蓝通道阴影方案；但实机验证暴露出该方案的 5 个固有瓶颈（0.06 低量化精度、屏幕空间后处理裁切立绘脸部、无法吃进昼夜 ColourCube、与泛光解耦困难、侧光公告牌变薄）。
  - 因此在 v2.9.0 彻底重写为原生 GLES2 IAT 层的 **世界空间顶视正交 RGBA8 ShadowMask FBO**：在地面层之后、首个立绘实体之前于 `y=0.04` 合成，彻底根治上述全部瓶颈，同时在 Native OpenGL (`GL_MAX`) 与 ANGLE D3D9 (`DEPTH16`) 双后端通过实机像素回读测试。旧 Bloom 蓝通道路径仅保留为 FBO 极端故障时的 Fail-Closed 兜底。
- **ADR-2：为何升级 UI 合批器为 AZDO 4MB/256KB 线性追加环（[`ui_batcher.h`](../native_plugin/ui_batcher.h)）**
  - 主菜单与局内实机测试中发现，GLES 3.0 规范 §6.3.2 下在 offset=0 复用缓冲区时附加 `GL_MAP_UNSYNCHRONIZED_BIT` 会导致同帧高频 Flush 覆写 GPU 在途显存并引起闪烁。
  - 重构为线性无重叠偏移追加 + 绕回时纯 `GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT` 驱动页重命名，并修复渲染线程小缓冲影子捕获跨线程门控，彻底消除闪烁与 `GL_MAP_READ_BIT` 管线停顿。
- **ADR-3：为何推出 `HS_EXT_V1` 开放着色器标准与外部 Mod 礼让机制（[`docs/HS_EXT_V1_SPEC.md`](HS_EXT_V1_SPEC.md)）**
  - 确立“兼容标准，而非逐个硬编码兼容单个模组”的开放生态原则；并通过 `IsExternalBcasActive()` 检测机制，在玩家同时启用独立版 `DST-BCAS-Studio` 时主动让出后处理，尊重社区共存。

### 2.3 独立可复现自动化验证套件（工程原创铁证）

本仓库内置大规模独立自动化测试与真机 GPU 像素回读验证套件，任何人均可从源码一键复现验证：

| 测试套件 | 规模与覆盖内容 | 运行命令 |
| :--- | :--- | :--- |
| [`tools/test_redteam_client.cpp`](../tools/test_redteam_client.cpp) | **24 项（v2.8.0 已推送）/ 28 项（v2.9.x 待发布）** C++20 单元 + 真机 GPU 红队测试（W^X、RTSS 稳帧、GC、骨骼/粒子/世界坐标平滑、A\* 并发、Native WGL ES2、VBPool、SpriteBatcher、UiBatcher 等） | Zig C++20 编译后执行 `tools/test_redteam_client.exe` |
| `tools/test_world_shadow_gpu.cpp` *(v2.9.x 待发布)* | **双后端（Native OpenGL + ANGLE D3D9）真机 GPU FBO 验证**：World ShadowMask、多点光源、Planar Water Reflection、DXGI Flip、Mock-GL 状态机、双 Context 与并发存盘压测 | Zig C++20 编译后执行 `tools/test_world_shadow_gpu.exe` |
| [`tools/test_client_lua_v24.py`](../tools/test_client_lua_v24.py) | **18 项（v2.8.0 已推送）/ 26 项（v2.9.x 待发布）** 真实 64 位 DST Lua 5.1 虚拟机全链路测试 | `python tools/test_client_lua_v24.py` |
| [`tools/test_redteam_client.py`](../tools/test_redteam_client.py) | PE 6/6 导出检查、纯 UCRT 依赖校验、`System32` 无锁透传验证、**58/58** 客户端与 ANGLE 唯一 AOB 签名验证 | `python tools/test_redteam_client.py` |
| [`tools/test_spike_blackbox.py`](../tools/test_spike_blackbox.py) | 掉帧黑匣子（Spike Blackbox）环形缓冲与归因逻辑回归测试 | `python tools/test_spike_blackbox.py` |

---

## 三、授权与资源合规索引

所有第三方依赖库（`libdeflate v1.23` MIT、`mimalloc v2.1.7` MIT）、开源参考项目（`DST-BCAS-Studio` MIT、`DST-KeyBind-UI` MIT）、仓库源码独立生成的二进制资产（`.ksh` / `.tex` / `.xml` / `blue_noise_64.h`）以及科雷官方运行时资源不分发声明，已完整归档于：

- 完整授权台账：[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md)
- 项目主许可证：[`LICENSE`](../LICENSE)
