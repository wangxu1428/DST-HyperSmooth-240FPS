<p align="right">
  <strong>简体中文</strong> | <a href="README_EN.md">English</a>
</p>

# DST HyperSmooth 240FPS & FastLoad (v2.6.0)

> **《饥荒联机版》（Don't Starve Together 64-bit）纯客户端旗舰级体验增强套件**  
> **Client-Only Performance, Ultra-Low-Latency Input & Player-First Eye-Care Visual Suite for DST 64-bit**  
> 服务端套件（含 Windows `iphlpapi.dll` 与新增 **Linux x86_64 `libhypercore.so`**）请切换分支 👉 [`server-hypercore`](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/tree/server-hypercore)（[Linux 独立部署目录](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/tree/server-hypercore/server_hypercore_linux) | [Releases 下载](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases)）

---

## 核心定位：DST 三大体验支柱 × 玩家优先五铁律

我们将数十项 C++ 底层逆向与 Lua 引擎优化，收束为玩家打开游戏就能直观感受到的 **三大体验支柱**：

1. **画面流畅（Visual Smoothness）—— 让整个世界以 240Hz 连续呼吸**
   - 全场景 **240Hz 同相补帧闭环**：主世界/特效/UI 三通道 2D 骨骼、关延迟补偿世界坐标（含 **自适应 De-Jitter 网络抖动平滑** `g_optAdaptiveDeJitter`）、全引擎 VFX 与旧版粒子（雨雪/火星/法术）、移动火把/萤火虫动态光源、跟拍镜头与头顶对话气泡 (`FollowText`)、以及 `TrueScrollArea` / 聊天窗 / 小地图缩放。
   - **零争抢 RTSS 级内核稳帧 (`FrameLimiter::Wait`)** + **扩展版 GLES2 & Uniform 状态镜像缓存 (`gl_state_cache.h`)** + **只读 Mod/资源负向 Stat 缓存 (`fs_neg_cache.h`)** + **合成栏零卡顿 & `Placer:TestCanBuild` / `TriggerDeployHelpers` 同 tick 空间去重** + **单人/本地建房引擎级三件套（`FastAStar` 25× 寻路 + `FastDataDumper` 155MB/s 存档 + `libdeflate` 2.5× 极速加载）**。
2. **操作低延迟（Ultra-Low Latency）—— 走 A、切装、闪避零粘滞**
   - **D3D9Ex 硬件 1 帧渲染队列（DST 版 Reflex）**：将 ANGLE 后台帧队列从默认 `3` 帧压至 `1` 帧，砍掉 `8~33ms` 显示管线积压延迟。
   - **240Hz 边沿直发操控**：键盘 WASD 走位、`F` 攻走 A / `Ctrl+LMB` 强制攻击防漏、空格交互、急甩鼠标左右键即时解析，配合 **RakNet 零停顿事件唤醒**，彻底消除 `0~33.3ms` 逻辑刻等待与 `0~10ms` 发包滞留。
3. **画质提升（Player-First Eye-Care Visuals）—— 服务于眼睛，拒绝算法炸弹**
   - 严格遵守 **玩家优先五铁律**：
     1. **默认 100% 原版纯净**（安装即零额外视觉负担）；
     2. **绝不强迫开启任何后处理**（连 `BloomSampler` 都原封不动）；
     3. **全部 9 项精修独立开关**，支持局内侧边面板 (`c_hsmenu()`) 0ms 实时微调对比；
     4. **关闭后 100% 完整恢复原版表现**；
     5. **尊重 99% 饥荒玩家关闭「泛光/摇晃/失真/闪烁」的护眼习惯**（关泛光同样全效，绝不做刺眼爆白特效）。

---

## v2.6.0 超级大版本核心更新 / What's New in v2.6.0

### 1. 画面流畅与高分屏体验全面闭环
- **全引擎 240Hz 粒子与动态光源补帧**：新增 `ParticleManager::Update` + `VFXRenderCmd` + `EmitterRenderCmd` 与 `LightMap::GenerateRenderData` 底层 240Hz 连续插值，雨滴、雪花、火星、法术粒子与提灯/火把光圈彻底告别 30Hz 抽帧；关延迟补偿下 `WorldPos240` 新增自适应 De-Jitter 网络抖动平滑。
- **社区反馈多粒子骨骼专项修复 + 沃尔夫冈举哑铃丝滑修复**：
  - 针对**萤火虫、秋天飘落树叶、池塘波纹反光**升级为全局最近邻 + `2-opt` 反交叉匹配器，彻底消除萤火虫分裂成 5 个静止光点与落叶跨树拉丝。
  - 将共享层判定升级为严格 **「刚体骨骼层 vs 动态粒子层」双射分流**（`isParticleLayer`），全面解救角色 `arm_carry` 等 6 部件复合骨骼层在高速运动时的 48px 误杀（沃尔夫冈大力士举哑铃拒补 `22→0`、变身 `89→0`、全角色挥刀攻击 `59→3`，同时 `tree_leaf_fx` 防拉丝 100% 保留）。
  - 新增 **单次无缝自重播回绕插值**（`playMode == 0` 末帧即时重播识别），抹平举哑铃、砍树、挖矿等每轮动作末尾的 1 帧顿挫。
- **合成栏/蓝图放置零卡顿 + 全滚动面板/头顶气泡 240Hz**：优化 `CraftingMenuHUD:RebuildRecipes` 为 $O(\text{slots})$ 增量查表并延迟隐藏面板重绘；新增 **`Placer:TestCanBuild` 与 `TriggerDeployHelpers` 同 tick 空间去重**；补齐 `TrueScrollArea` 与 `FollowText` 对话气泡 240Hz 同步（严守 ShowMe / Insight 零冲突红线）。
- **单人 / 无洞穴本地建房三件套**：主进程本地建房时自动启用客户端内置 **`O(N log N)` `FastAStar` 寻路**、**`155 MB/s` `FastDataDumper` 序列化**与 **`libdeflate` `compress2`**，大幅削减单人档清晨自动存档冻屏与巨型围墙基地卡顿。
- **扩展 GLES2 + Uniform 状态镜像缓存 (`gl_state_cache.h`) & 负向 Stat 缓存 (`fs_neg_cache.h`)**：
  - 在原有纹理/Program 缓存基础上扩展 Uniform 影子缓存与 Blend / Cull / Depth / Scissor 状态去重（含未链接 Program 的 fail-closed 保护），降低大基地渲染线程开销。
  - 新增只读 Mod/资源负向 Stat 缓存（含写/重命名/删/建目录 Epoch 失效保障），减少多 Mod 环境下对不存在贴图/脚本路径的重复磁盘探测。

### 2. 操作低延迟全面打通
- **D3D9Ex 1 帧低延迟队列（DST 版 Reflex）**：拦截 ANGLE `applyTextures` 现场设置 `SetMaximumFrameLatency(1)`（支持局内开关随时还原为原版 `3`）。
- **240Hz 战斗与交互低延迟**：支持关延迟补偿下 `Ctrl+LMB` 修饰键急甩防漏、走位取消攻击方向守卫、WASD/攻击边沿即时发包，全面兼容纯原版公共服与 `HyperCore Server` 60Hz 逻辑刻。

### 3. 画质精修与高分屏悬浮面板（已修复 2K/4K 右侧裁剪）
- **局内悬浮侧边微调面板 (`hs_panel.lua` / `c_hsmenu()`)**：不暂停游戏、不遮挡视野，采用双节点 `SCALEMODE_PROPORTIONAL` 虚坐标布局，修复 `2560×1440` / `4K` 高分屏下面板右侧被裁切问题，完整显示 4 组预设（「原版纯净 / 护眼高清 / 柔和绘本 / 自定义」）+ **9 项独立画质开关**（含 `Mip LOD -0.45` 与暗夜/洞穴 `31.5 LUT`）+ **0ms 无重载热切 `0.5× ⇄ 1.0×` 光照图 + 篝火 8-bit 防色带** + 一键恢复原版纯净。
- **旧版 `bin64\dinput8.dll` 智能更新提示条**：利用 `HyperSmooth_Version()` 自动校验工坊 Lua 与本地手工 DLL 版本；若工坊已升级但 `bin64\dinput8.dll` 仍停留在旧版（如导致玻璃雨崩溃的 `v2.2.0`），进世界轻量提示 1 次并在侧边面板顶部常驻醒目琥珀黄更新条。

### 4. 红队安全加固与 LuaJIT2 零冲突兼容
- **全量 R3.1 红队安全收口**：`ThreadFreezeGuard` 退出码严格白名单 Fail-Closed、`FastAStar` 64 位单调代数 `gen` 根除同槽位 ABA、多 OpenGL 上下文 Shader 溢出表查重与 `glGetProgramiv` 完整门控、无锁 `exchange(0)` 帧计数与 `>=500ms` 严重卡顿归因捕获、全部跳板 W^X 只读锁定 (`PAGE_EXECUTE_READ`)。
- **`DontStarveLuaJIT2` (工坊 `3444078585`) 自动兼容**：冷启动自动识别 `lua51DS.dll` / `Injector.dll` / 本地 `bin64\Winmm.dll` 代理并自动跳过 `mimalloc lua_newstate` 钩子，LuaJIT2 玩家开箱即用无需手改 `.ini`。
- **配套开源一键安装/更新工具（`tools/一键安装与更新.bat`）**：自动探测 Steam 多盘库 / WeGame 路径、防游戏占用、旧 DLL 自动备份、SHA256 逐字节校验与一键卸载恢复纯净原版。

---

## 实测性能数据 / Verified Benchmarks

测试环境：`Windows 10/11 x64 · Intel i7-12700KF · RTX 4060 · 32GB RAM · 240Hz · DST 64-bit Build 762172`

| 优化项 / Benchmark | 原版 / Vanilla | HyperSmooth v2.6.0 | 实测收益 / Gain |
| :--- | :---: | :---: | :---: |
| 进图 `LOAD BE` 资产解压加载 | `10.05s` | **`4.0s`** | **提速 2.5×**（`libdeflate v1.23` + `rep stosb`） |
| 联机洞穴 ⇄ 地面折返资源重载 | 重复卸载并重载 | **`0.0 MB` 零重载** | **跨 `SimReset` 资产保活** |
| 全场景 240Hz 补帧覆盖 | 30Hz 骨骼/粒子/光源阶梯 | **骨骼 + VFX 粒子 + 光源 + 镜头 + 气泡全 240Hz** | 300×18 骨骼匹配 `292 → 67 µs` (**4.3× 提速**) |
| RTSS 240Hz 内核稳帧器 | 满核忙等 (`100%` 单核) | **`240.0 FPS` / `1%L 237.9+` / 抖动 `0.002ms`** | **~82% 等待 0% CPU** |
| D3D9Ex 渲染管线队列延迟 | 默认 `3` 帧队列 | **`1` 帧硬件 Reflex 队列** | **直降 `8~33ms` 端到端输入延迟** |
| 单人/本地建房巨型围墙寻路 | 原版 $O(S^2 \log S)$ | **`FastAStar` $O(N \log N)$** | **提速 25×+** |
| 护眼画质精修（默认原版纯净） | 原版 0.5× 光照/1×AF | **9 项独立开关 · 0ms 热切 `1.0×` 光照** | **RTX 4060 2K 240Hz `Busy ~2.6ms`，VRAM 零暴涨** |

---

## 玩家安装与更新指南 / Player Installation

### 第一步：订阅客户端 Mod
- **Steam 创意工坊**：订阅并启用 **HyperSmooth 240FPS & FastLoad**（或在本地将 `workshop_mod/dst_smooth_highfps` 复制至游戏 `mods/dst_smooth_highfps`）。

### 第二步：安装 / 更新 `bin64/dinput8.dll`（Steam 工坊不会自动更新 DLL，大版本升级请务必同步！）
- **方式 A（推荐 · 一键傻瓜安装/更新）**：
  1. 下载并**解压**最新版 `DST_HyperSmooth_v2.6.0_Release.zip`；
  2. 先关闭饥荒游戏，双击压缩包根目录的 **`一键安装与更新.bat`**，按提示回车即自动完成路径探测、旧版备份、新版覆盖与 SHA256 校验（卸载选 `2` 即可 100% 恢复原版）。
- **方式 B（手动安装）**：
  1. 将压缩包中的 `bin64/dinput8.dll` 复制到游戏根目录的 **`bin64/`**（与 `dontstarve_steam_x64.exe` 同目录），覆盖旧文件；
  2. 正常启动 64 位《饥荒联机版》，局内按 `c_hsmenu()` 看到顶部绿条 `已直连 bin64/dinput8.dll v2.6.0` 即代表全部就绪！

> **兼容提示**：
> - **LuaJIT2 玩家**：`v2.6.0` 已自动识别 `DontStarveLuaJIT2` 并避让内存分配器冲突，无需手动改 `.ini`（建议在 LuaJIT 配置中避免同时开启功能重叠的 `plugin_fps_render`）。
> - **双显卡笔记本**：若游戏内帧数停在 `150 FPS` 左右，在显卡控制面板切为 **独显直连** 即可跑满 `240+ FPS`。

---

## 局内调节与控制台指令 / In-Game Controls

- **`ESC -> 设置 -> 画面`**：直接调整官方 `刷新率`（`60~360 / 无限制`，全屏与窗口均已解锁）以及右列下方 `240帧骨骼补帧` / `帧率显示 HUD` / `画质增强`。
- **局内实时悬浮面板**：控制台输入 `c_hsmenu()`（或在 `ESC -> 设置 -> 控制` 底部录制任意快捷键/鼠标侧键）随时滑出右侧半透明微调面板。
- **常用诊断指令**：`c_hsopt("off")` 一键恢复原版、`c_hsvisual()` 查看画质与显存遥测、`c_hslatency()` 查看 D3D9Ex / 急甩防漏 / 合成表 / 本地建房加速指标、`c_hsbench()` 常驻耗时监测。

---

## 安全性与开源构建 / Safety & Build

- **零磁盘篡改与严格隔离**：完整无锁透传 `System32\dinput8.dll` 全部 6 个导出函数；严格按进程名隔离，仅在 `dontstarve_steam(_x64).exe` / `dontstarve_rail(_x64).exe` 生效；全部代码跳板执行 W^X 只读固化 (`PAGE_EXECUTE_READ`)，运行时零 `.text` 写入，跨版本 AOB 均具备 Fail-Closed 熔断保护。
- **纯开源构建**：使用 Zig (`x86_64-windows-gnu`, `-std=c++20 -O3`) 运行 `tools/build_native.ps1` 即可零警告编译零依赖纯 UCRT `dinput8.dll`。
