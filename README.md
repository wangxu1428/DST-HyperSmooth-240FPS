# DST HyperSmooth 240FPS & FastLoad (v2.2.0 · Final Ceiling Edition)

> **饥荒联机版（Don't Starve Together）纯客户端 240 帧骨骼补帧、全套高刷修复、反 GC 掉帧与全链路极速加载伴侣**  
> **Client-Only 240FPS 2D Skeletal Tweening, Complete High-FPS Fixes, Anti-Stutter GC & Full-Stack Fast-Load Suite for Don't Starve Together (64-bit)**

---

## 核心特性 / Key Features

本套件采用**「Steam 创意工坊纯客户端 Mod + 开源原生引擎伴侣插件 (`dinput8.dll`)」**双轨架构，**100% 纯客户端生效**，不改动任何服务端逻辑、不修改游戏硬盘原始可执行文件 (`dontstarve_steam_x64.exe`)，支持所有官方/公共联机服务器与单人档。

### 1. 原生引擎级优化 (`bin64/dinput8.dll` · Universal x86-64)
- **突破 60 帧锁定 + 混合精准限帧器 (Hybrid Frame Pacer)**  
  通过跨版本特征码 (AOB) 动态解除官方 60 FPS 硬编码上限（支持 `60 / 120 / 144 / 165 / 240 / 360 / 无限制`），并开启 2 字节混合限帧补丁（`Sleep(1) + 2ms` 精准自旋等待），兼顾极致帧时间平滑与低 CPU 功耗。
- **全实体真 240 帧三通道 2D 骨骼动画补帧 (True 240fps Full-World Skeletal Tweening)**  
  Hook 引擎底层 `Animation::GetFrame` 全部 3 条渲染管线（**主世界实体 + FollowSymbol 特效层 + UI/AnimNode**），直接锁定底层持久化 `AnimNode*`（解决每帧临时渲染指令地址漂移问题），采用 **8,192 槽位 4 路 LRU 追踪器 + 连续浮点 QPC 亚刻插值**，覆盖全屏所有玩家、怪物、Boss（梦魇疯猪/巨鹿/龙蝇等）、制作/跳跃/攻击及加速动画：
  - 使用 **2D 列向量极分解 (Polar Decomposition)** 对齐旋转角与缩放因子，消除线性混合导致的骨骼萎缩与剪切畸变。
  - 采用**两段式骨骼配对**（先精确配对 `(layer_hash, symbol_hash, frame_num)`，再对换图挥砍/手部/Boss 爪部执行 `(layer_hash, symbol_hash)` 矩阵平滑并在中点换图），搭配 **Boss 级 `420px / 135°` 位移旋转保护**与镜像翻转保护。
  - 局内随时按 **`F10`** 一键热开关，支持同屏 A/B 盲测对比！
- **平滑 240Hz Lua GC 调度器 & 反卡顿冻屏保护 (`Simulation::CollectGarbage` Hook)**  
  - 针对官方每帧固定 `1.0ms` GC 预算在 240Hz 下侵占 24% 帧时间的问题，按目标帧率动态缩放为 **`0.35ms`/帧**（`240 × 0.35ms = 84ms/s`，总回收吞吐比原版 60FPS 高出 **+40%**，而单帧最大 GC 尖峰压低 **3×**，显著拉高 1% Low 帧率！）。
  - 将官方激进的 `1.5×` Stop-The-World 全量 GC 触发线升级为紧急安全兜底（`max(2.5×, +256MB)`），彻底消除打 Boss / 大型基地时的 `60~200ms` 主线程瞬冻，同时保留防第三方漏内存 Mod 的 OOM 保护。
- **2.5 倍极速加载：`libdeflate v1.23` + 硬件 `rep stosb` 修复**  
  - 接管游戏陈旧的 2005 版 `zlib 1.2.3` + `minizip` 16KB 分片解压循环，替换为现代 **`libdeflate v1.23`** 整块极速解压（运行时按 CPUID 自动调度 AVX-512 / AVX2 / BMI2 / SSE2，兼容所有 64 位 x86-64 CPU）。
  - 修复 MSVC 2008 编译遗留的 `std::vector<uint8_t>::_Insert_n` 逐字节清零热点循环，原地替换为 CPU 硬件 `rep stosb` 指令。
- **系统级调度与磁盘预热**  
  开启 1ms Windows 高精度系统定时器 (`timeBeginPeriod(1)`)、提升客户端及本地自建服务端进程至 `HIGH_PRIORITY_CLASS`，并异步将 `data/databundles/*.zip` 预热进系统文件缓存。

### 2. 纯客户端 Lua Mod (`mods/dst_smooth_highfps/`)
- **全套原版高刷新率 Bug 修复**：
  - `FollowCamera`：60Hz 等效精确指数阻尼，消除高刷镜头手感偏移。
  - `TrueScrollList` & `ChatScrollList`：修复制作栏、模组列表、服务器列表、皮肤、图鉴、聊天窗硬编码 `Lerp(..., 0.25)` 在 240Hz 下滚轮 **4 倍过快** 的问题。
  - `MapScreen`：修复小地图按键缩放硬编码 `1/60` 在 240Hz 下 **4 倍过快** 的问题。
  - `DeployHelper`：修复灭火器/避雷针/发电机等建筑放置辅助圈按帧递减导致 240Hz 闪烁的问题。
- **240Hz 零冗余 CPU & GC 减负**：
  - `DistanceFade`：将树冠/洞穴柱体透明淡出改为零分配标量点积，消除 240Hz 下每秒数千次 `Vector3` 临时表分配。
  - `Input:OnUpdate`：静止帧无损缓存鼠标实体拾取结果（鼠标移动/镜头移动/界面切换 0ms 零延迟刷新；静止时跳过 ~75% 冗余 C++ 实体扫描与表分配）。
  - `HoverText`：对悬停提示文本与颜色加脏检查，跳过 99% 无效表分配与 C++ `TextWidget` 重排（100% 兼容 ShowMe / Insight）。
- **联机上下洞穴零卸载秒切**：纯客户端保留地面 (`forest`) 与洞穴 (`cave`) 原版资产常驻内存，联机反复上下洞穴零重复解压；退回主界面自动安全释放（建议 >= 8GB 内存）。
- **模组与网络等待全清空**：缓存未启用模组 `modinfo` 免重复读盘、跳过 Steam 热门模组榜请求 (`QueryTopMods`)、跳过主界面 MOTD 远程海报阻塞等待（不影响登录与联机）。
- **轻量右上角 FPS/帧时间 HUD & 局内热键联动**：`F9` 切帧率、`F10` 开关骨骼补帧，通过持久化桥接文件与 `dinput8.dll` 毫秒级热联动。

---

## 实测性能数据 / Verified Benchmarks

测试环境：`Windows 10 Pro x64 · Intel i7-12700KF · RTX 4060 · 32GB RAM · 240Hz 显示器 · DST 64-bit Build 762172`

| 优化项 / Benchmark | 原版 / Vanilla | HyperSmooth v2.1.0 | 实测收益 / Gain |
| :--- | :---: | :---: | :---: |
| 进图 `LOAD BE` 资产解压加载 | `10.05s` | **`4.0s`** | **提速 2.5×** |
| `std::vector::_Insert_n` 清零热点 | `2.86s` | **`0.69s`** | **提速 4.1×** |
| Zip 解压吞吐 (`libdeflate v1.23`) | ~330 MB/s | **`830+ MB/s`** (`12,960/12,960` 100% 命中) | **3.32 GB 仅约 4 秒** |
| 联机洞穴 ⇄ 地面折返资源重载 | 重复卸载并重载 | **`0.0 MB` 零重载** | **纯客户端跳过重读** |
| 2D 骨骼动画实际渲染帧率 | 锁定 30Hz | **真 240Hz 三通道连续补帧** | **主世界/特效/UI 全覆盖** |
| 单帧增量 Lua GC 最大尖峰 | `1.00ms` + 突发 `60~200ms` 全量冻屏 | **`0.35ms`** + 紧急兜底防冻屏 | **尖峰压低 3×，总吞吐 +40%** |

---

## 玩家安装指南 / Player Installation

### 第一步：安装客户端 Mod
- **方式 A（Steam 创意工坊）**：直接在 Steam 创意工坊订阅并启用 **HyperSmooth 240FPS & FastLoad**。
- **方式 B（本地解压安装）**：将压缩包中的 `mods/dst_smooth_highfps` 文件夹复制到游戏根目录的 `mods/` 下（例如 `...\Steam\steamapps\common\Don't Starve Together\mods\dst_smooth_highfps`），进入游戏在「客户端模组」中勾选启用。

### 第二步：安装原生引擎伴侣插件（解锁 240 帧、骨骼补帧、反 GC 掉帧与 2.5 倍加载）
1. 前往本仓库 **[Releases 页面](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases)** 下载最新版 `DST_HyperSmooth_v2.2.0_Release.zip`（或单独下载 `dinput8.dll`）。
2. 将压缩包中的 `bin64/dinput8.dll` 复制到游戏根目录的 **`bin64/`** 文件夹内（与 `dontstarve_steam_x64.exe` 同目录）。
3. 正常从 Steam 启动 64 位《饥荒联机版》即可自动生效！
4. 首次启动后，插件会在 `bin64/` 下自动生成配置文件 `dst_fps.ini` 和运行日志 `dst_fps_plugin.log`（仅在加载完成时输出一条精简汇总，无持续刷屏）。

> **如何卸载 / Uninstall**  
> 直接删除 `bin64/dinput8.dll`（及生成的 `dst_fps.ini`、`dst_fps_plugin.log`）并在游戏内取消勾选 Mod 即可 100% 恢复原版，不留任何残留。

---

## 局内热键 / In-Game Hotkeys

| 默认热键 | 功能说明 | 可自定义选项 |
| :---: | :--- | :--- |
| **`F9`** | 局内循环切换目标帧率 (`60 -> 120 -> 144 -> 240 -> UNLIMITED`) | Mod 设置中可改为 `F8 / F7 / 禁用` |
| **`F10`** | 局内实时开关 **真 240 帧骨骼动画补帧**（同屏 A/B 对比神器） | Mod 设置中可改为 `F11 / 禁用` |

---

## 原生插件配置参考 (`bin64/dst_fps.ini`)

若安装了 Steam 创意工坊 Mod，游戏内 Mod 设置与 `F9`/`F10` 热键会实时覆盖目标帧率与骨骼补帧开关；若单独使用 `dinput8.dll`，可直接编辑 `bin64/dst_fps.ini`（保存后 250ms 内热生效）：

```ini
[FPS]
target_fps=240

[OPTIMIZE]
high_res_timer=1
boost_priority=1
prewarm_bundles=1
hybrid_frame_pacer=1
fast_vector_fill=1
fast_libdeflate_zip=1
fast_zlib_inflate=1
smooth_skeletal_anim=1
smooth_lua_gc=1
```

---

## 进阶说明与已知引擎特性 / Technical Notes

- **物理实体位移**：经反汇编验证，DST 内置 Bullet 物理引擎 (`btDiscreteDynamicsWorld`) 与网络 `Transform` 插值均已在 `WallUpdate` 原生按渲染帧率（240Hz）插值，无需外部重复插值。
- **为何未强制开启 `texture_streaming = true`**：经反汇编验证，该隐藏项会将纹理上传延后至后台线程，可能导致快速跑图/刷 Boss 时出现短暂白块或贴图弹出；在 `LOAD BE` 已压至 4 秒的前提下，为保证画面零闪烁不强制开启。
- **避免重复订阅同类功能 Mod**：若同时启用两份功能重叠的 Mod（如多份 Global Positions），会额外拖慢进图并增加内存开销，建议保持精简。

---

## 作者发布指南 / Steam Workshop Upload Steps

1. 确保 `mods/dst_smooth_highfps/` 内完整包含 4 个文件：`modinfo.lua`、`modmain.lua`、`modicon.tex`、`modicon.xml`。
2. 在 Steam 库 -> 工具中启动 **Don't Starve Mod Tools**，打开 **Mod Uploader**。
3. 选择本地目录 `mods/dst_smooth_highfps`，预览图上传本发布包根目录自带的 **`workshop_preview.png`** (`512x512`)。
4. 在工坊简介中附上 `bin64/dinput8.dll` 的下载链接（例如 GitHub Release / 网盘链接），即可供所有玩家一键搭配使用！

---

## 安全性与兼容性声明 / Safety & Compatibility

- **零磁盘篡改**：`dinput8.dll` 仅为标准透明 DirectInput8 代理（仅导出 `DirectInput8Create` 并转发至 `System32\dinput8.dll`），不 Hook D3D9/OpenGL 虚表（100% 兼容 Steam 覆盖层 / RTSS / NVIDIA 滤镜），所有优化均在进程内存中通过精确特征码匹配动态生效。
- **跨更新安全**：若未来游戏更新导致某条特征码变化，对应模块会自动安全跳过而不崩溃；通用 x86-64 编译，SIMD 由 `libdeflate` 运行时动态检测。
- **纯客户端合规**：不修改任何联机同步数据、判定逻辑或服务端行为。
