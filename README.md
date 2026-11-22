# DST HyperSmooth 240FPS & FastLoad (v2.4.1 · RTSS Frame Pacer + Boss Anti-Hitch + Any-Key Binding + Dual-Side 60Hz Ready)

> **饥荒联机版（Don't Starve Together）纯客户端 240 帧骨骼与位移补帧、零争抢 RTSS 级 1% Low 稳帧引擎、Boss 并发防卡顿、真·全按键自由录制绑定与可选双端 60Hz 逻辑刻伴侣**  
> **Client-Only 240FPS Skeletal & World-Pos Tweening, Zero-Contention RTSS Frame Pacer, Boss-Swarm Anti-Hitch, Interactive Any-Key Binding UI & Auto-Negotiated 60Hz SimTick for Don't Starve Together (64-bit)**

### v2.4.1 社区反馈专项修复 & 真·全按键自由录制 / Community Hotfix & True Any-Key Binding
- **彻底修复萤火虫光效连成直线（及全游戏多粒子同名骨骼动画防拉线）**：针对 `fireflies.zip`、`swarm_loop` 等同帧包含多个同名 `(layer, symbol)` 光点/火花/孢子的骨骼动画，升级为 **最近邻空间距离匹配 + `usedF1` 互斥占位 + `<=48px` 粒子防拉线熔断**（唯一键角色/Boss 骨骼仍保持 $O(1)$ 直连），彻底杜绝消失光点被跨群拉成直线！
- **修复上下洞穴保活跨 `SimReset` 失效问题 + 角色资产同步保活**：Hook `GLOBAL.SimReset` 将保活列表随 `instanceparameters` 跨 Lua VM 重建持久化传递，并在 `BACKEND -> BACKEND` 联机切洞穴期间同时保活 `forest`/`cave` 与 `Settings.loaded_characters`（退回主菜单全部干净卸载释放，零双重卸载）。注：首次下洞仍需加载洞穴资源，且克雷切层必经 `TheSim:Reset()` 重建脚本与分片重连，复返耗时可显著缩短但非 0 秒瞬切。
- **真·全按键自由录制绑定 UI（`keybind.lua` · 对标《热键：李皓奇》）**：启用模组并点击「应用」后，Mod 配置页与 **`ESC -> 设置 -> 控制` 页面最下方** 均升级为点击录制按钮，直接按下**键盘任意键**或点击**鼠标中键 / 侧键 4 / 侧键 5** 即刻绑定（点 `×` 一键解绑），支持绑定「循环切帧」「开关 240 帧骨骼补帧」「开关帧率 HUD」三项（默认全禁用零冲突）。
- **双显卡笔记本排查提示**：若笔记本游戏内帧数停在 `150 FPS` 左右，请在显卡控制面板/电脑管家中切换为 **独显直连**（绕开核显 Optimus 帧拷贝瓶颈）即可跑满 `240+ FPS`。

### v2.4.0 1% Low 稳帧革命 · 关延迟补偿 240Hz 丝滑 · Boss 并发防卡顿 & 双端 60Hz / Zero-Contention Pacing, Lag-Comp-OFF 240Hz Interp, Boss-Swarm Anti-Hitch & Dual-Side 60Hz
- **【A1】零争抢 RTSS 级帧时间控制器 (`FrameLimiter::Wait` 底层接管 · 1% Low 专项优化)**：
  - 彻底取代克雷原版粗糙的 `Sleep(1)` + 满核自旋死循环：改用 Win10/11 高精度内核等待计时器（`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`）+ 自适应唤醒过冲 EMA（`[500, 1950] µs` 针对 Win10 过冲专项防抖 + 屏蔽 `<450 µs` 微睡眠）+ 绝对截止期调度（卡顿后立即对齐不追帧连爆）+ `_mm_pause()` 退让收尾。
  - **约 85%~90% 等待时间保持 0% CPU 占用**，将 CPU 核与内存带宽完整让给 GPU 驱动（ANGLE `libglesv2.dll`）、音频与网络线程；实测 240Hz 稳帧器 **`Avg = 240.0 FPS`、`1% Low = 221.6~239.1 FPS`、`P99 = 4.27ms`、帧间抖动 `0.023ms`**（较旧版过冲抖动压低 **6.8×**）。
- **【A2】关延迟补偿 240Hz 丝滑位移 (`WorldPos240`) + 零等待网络操控 (`RakNet Zero-Stall` + `240Hz WASD RPC`)**：
  - 针对关闭延迟补偿（`Lag Compensation = OFF`）时远程客户端缺少本地 `locomotor` 插值、实体位置按 30Hz/60Hz 阶梯跳动锯齿的问题，新增底层 **240Hz 世界坐标连续插值**（精准追踪 `AnimNode` 变换与瞬移/传送/上船熔断保护），在保留 100% 精确服务器判定的同时画面丝滑无锯齿。
  - **RakNet 事件驱动零停顿唤醒**：移除客户端 `SendBuffered` / `SendBufferedList` 的优先级拦截，消除非 `IMMEDIATE` 级游戏操作 RPC 在 `WaitOnEvent(10)` 中白等的 `0~10ms` 延迟（零轮询、0% 空闲 CPU）。
  - **240Hz 即时 WASD 发包（仅关延迟补偿远程客户端生效）**：在 `PlayerController:OnControl` 键盘按下/抬起边沿即时触发 `RemoteDirectWalking` / `RemoteStopWalking`（由 `self.remote_vector` 天然去重、零重复 RPC、自动旁路手柄摇杆与所有原版保护状态），削减 `8~33ms` 按键起停等待，100% 兼容纯原版公共服。
- **【A3】Boss 并发防卡顿五件套（天体 3 阶段陨石 / 龙蝇岩浆虫 / 蜂后蜜蜂群 / 织影者小虫全覆盖）**：
  - **5× 提速零卡顿骨骼管线**：3 级矩阵插值快路径（静止 / 纯平移 / `<3.5°` 小角近似 / 极分解）+ 栈上 $O(N)$ L1 哈希骨骼配对（`i7-12700KF` 300 实体 × 18 骨骼实测 `292 µs -> 67 µs`，**提速 4.3×~5.1×**）+ L1 热槽位缓存 + 2 级极端并发降级护栏（极端同屏优先减负远景旋转矩阵，**永不关闭 240Hz 世界坐标插值**）。
  - **5 条客户端 `SpatialGrid::Query` 全路径零拷贝**：将服务端验证过的零拷贝只读空间查询完整移植至客户端全部 5 条只读入口（`FindEntities` 缓存/直查/注册 3 条 + `LightWatcher` 亮度/RGB 2 条），全部独立 AOB Fail-Closed 校验，彻底消除每帧海量的单元格 `std::vector` 堆分配风暴。
  - **`RenderThread` 异步纹理上传时间片限流 (`ProcessAsyncQueueResults`)**：局内激战遇到 Boss 召唤物按需上传 DXT5 纹理爆发时，自动按 **`1.2ms`/帧**（每帧至少完成 1 个任务）分帧消化，彻底消除 `20~60ms` 渲染线程单帧冻屏；切图/读图加载期间自动全速无限制。
  - **并发平滑避让 GC (`FastSmoothCollectGarbage`) + 独立无锁 `mimalloc v2.1.7` Lua 堆**：在 `240 -> 150 FPS` 并发掉帧区间以连续平滑 `0.30~1.60ms` 预算消除旧版延后爆发锯齿，并搭配 `thread_local` 批量计账的静态 `mimalloc v2.1.7` 堆，彻底消灭全局锁争抢与 Full-GC 冻屏。
  - **无锁后台 `[Hitch]` 归因遥测**：仅在真实局内发生 `>25ms` 异常帧时由后台线程每 10 秒最多落盘 1 条各子系统耗时明细（`anim / gc / tex / spatial`），主线程与渲染线程零磁盘 I/O。
- **【A4】零分配 C++ 512 帧环形 `1% Low / P99` 实时 HUD & 纯被动双端 60Hz 自动协商**：
  - 右上角 HUD 直接读取 C++ 底层 `HyperSmooth_GetFrameStats()`，零 Lua 表分配实时展示 **`240 FPS | 1%L 232 (4.2ms)`**（接入双端 60Hz 服务端时自动显示 **`[60Hz]`** 徽标）。
  - 默认严格保持原版 **30Hz**，且 `dinput8.dll` 在每次重建 Lua VM（返回主菜单 / 切换服务器 / 上下洞穴）时 **无条件重置为 30Hz**；仅在连接开启 `opt_sim_60hz` 的 **[DST HyperCore Server v1.2.0](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/tree/server-hypercore)** 时通过 `hypercore_sim60` 标签与 `HyperCoreProto` RPC 自动握手切至双端 60Hz（同步解锁 4/4 子系统时钟与客户端 `D=0` 无节流收包）。

### v2.3.0 红队安全加固 & 原生画面设置直控 / Security Hardening & Native Graphics UI
- **原生接管 `ESC -> 设置 -> 画面 -> 刷新率`（零多余控件、零键位冲突）**：
  - 直接复活克雷原版 `画面 -> 刷新率` 下拉项，**全屏 / 窗口 / 无边框全部解除置灰可用**，原生支持 `60 / 120 / 144 / 165 / 240 / 360 / 无限制`；顺手修复克雷原版 `IsGraphicsDirty()` 漏判刷新率 Bug，全屏切换高帧/无限制时自动映射安全显示器硬件刷新率（杜绝误回退 640×480）。
  - 在 `画面` 页右列 `动态树影` 下方同风格原位补齐仅有的 2 个新增画面项（`240帧骨骼补帧` + `帧率显示 HUD`），100% 接入克雷原生「修改黄底 -> 应用 / 撤销 -> 持久保存」闭环。
  - **重度模组零冲突热键**：为兼容数十个 Mod 的玩家，局内热键默认改为 **「禁用」**，同时在 Mod 配置开放 **全键盘自由选键**（`F1~F12` / 小键盘 `0~9` / 功能键 / `A~Z`）+ **`Alt / Ctrl / Shift` 组合修饰键**。
- **零运行时 `.text` 改写（彻底消除并发指令撕裂）**：将 `FrameLimiter::SetTargetFrameTime`（16B 对齐槽）升级为启动期一次性 14B 永久跳转，运行时切帧改为 **100% 纯原子变量写入**（`0` 运行时 `VirtualProtect`、`0` 指令边界撕裂风险），4 线程 400 万次并发实测 `PAGE_EXECUTE_READ` 恒稳通过。
- **冷启动线程冻结与 W^X 内存防线**：新增 `ThreadFreezeGuard`（挂起同进程其他线程并逐一核查 `RIP` 寄存器避让碰撞后再写补丁），且全部动态跳板/中继桩安装后立即锁定为 **`PAGE_EXECUTE_READ`**（零常驻 `RWX` 页）。
- **无锁完整 6/6 `dinput8.dll` 代理 + 模块钉扎**：完整导出并无锁透传 `System32\dinput8.dll` 全部 6 个函数（消除 `InitOnceExecuteOnce` 死锁风险）；接入 `GET_MODULE_HANDLE_EX_FLAG_PIN` 防止卸载悬空，清理 `DllMain` 退出的阻塞/卸载调用，采用严格文件名隔离并移除跨进程扫描。
- **`ALT_RENDERPATH` 三通配符动态解析 + Fail-Closed 安全熔断**：采用 3 通配符掩码签名动态提取结构偏移，若未来游戏更新导致未匹配则自动禁用主世界骨骼插值（绝不冒险崩溃）；同时在所有早退分支前刷新 `g_tlsLastParentKey`，消除 1 帧/特殊实体对后续特效层的键污染。
- **实测帧间隔（Real-`dt`）自适应 GC + 周期完成阈值回落**：改用实测 QPC 帧间隔连续缩放增量预算（`0.35ms~1.80ms`），彻底消灭高目标帧率在低帧大基地场景下的 GC 饥饿死区，并在增量周期完成时自动回落紧急阈值；第三方 Mod 的 `deployhelper` / `distancefade` 钩子自动识别保留；直连 Lua VM 桥接激活后自动停止 `Documents\Klei` 磁盘轮询。

### v2.2.1 历史兼容修复 / Previous Hotfix Notes
- **修复局内热键 F9 / F10 全员失效**：修复 `modmain.lua` 加载期捕获 `TheFrontEnd == nil` 导致的拦截 Bug，并在 `dinput8.dll` 新增 `luaL_openlibs` **0ms 直通 Lua VM 原生桥接**。
- **彻底修复老麦暗影陷阱/暗影囚笼闪退**（及植物人藤蔓陷阱、月雹、裂隙等 `ALT_RENDERPATH` 资产闪退）与三通道独立定长数组池隔离。
- **彻底修复 ShowMe（中文 / 原版）默认不显示详情冲突**与 **独行长路 (Don't Starve Alone)** 多模组世界生成兼容问题。

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
  - 局内可直接通过 **`ESC -> 设置 -> 画面`** 随时一键开关（或配置自定义热键），支持同屏 A/B 盲测对比！
- **平滑 240Hz Lua GC 调度器 & 反卡顿冻屏保护 (`Simulation::CollectGarbage` Hook)**  
  - 针对官方每帧固定 `1.0ms` GC 预算在 240Hz 下侵占 24% 帧时间的问题，结合心跳帧避让与并发平滑策略将单帧增量预算收束至 **`0.30~1.60ms`**（总回收吞吐优于原版，而单帧最大 GC 尖峰压低 **3×**，显著拉高 1% Low 帧率！）。
  - 将官方激进的 `1.5×` Stop-The-World 全量 GC 触发线升级为紧急安全兜底（`max(2.5×, +256MB)`），彻底消除打 Boss / 大型基地时的 `60~200ms` 主线程瞬冻，同时保留防第三方漏内存 Mod 的 OOM 保护。
- **2.5 倍极速加载：`libdeflate v1.23` + 硬件 `rep stosb` 修复**  
  - 接管游戏陈旧的 2005 版 `zlib 1.2.3` + `minizip` 16KB 分片解压循环，替换为现代 **`libdeflate v1.23`** 整块极速解压（运行时按 CPUID 自动调度 AVX-512 / AVX2 / BMI2 / SSE2，兼容所有 64 位 x86-64 CPU）。
  - 修复 MSVC 2008 编译遗留的 `std::vector<uint8_t>::_Insert_n` 逐字节清零热点循环，原地替换为 CPU 硬件 `rep stosb` 指令。
- **系统级调度与磁盘预热**  
  开启 1ms Windows 高精度系统定时器 (`timeBeginPeriod(1)`)、将客户端进程平滑提升至 `ABOVE_NORMAL_PRIORITY_CLASS`（绝不抢占本地子专服建图 CPU，亦绝不扫描或干涉其他进程），并异步将 `data/databundles/*.zip` 预热进系统文件缓存。

### 2. 纯客户端 Lua Mod (`mods/dst_smooth_highfps/`)
- **全套原版高刷新率 Bug 修复**：
  - `FollowCamera`：60Hz 等效精确指数阻尼，消除高刷镜头手感偏移。
  - `TrueScrollList` & `ChatScrollList`：修复制作栏、模组列表、服务器列表、皮肤、图鉴、聊天窗硬编码 `Lerp(..., 0.25)` 在 240Hz 下滚轮 **4 倍过快** 的问题。
  - `MapScreen`：修复小地图按键缩放硬编码 `1/60` 在 240Hz 下 **4 倍过快** 的问题。
  - `DeployHelper`：修复灭火器/避雷针/发电机等建筑放置辅助圈按帧递减导致 240Hz 闪烁的问题。
- **240Hz 零冗余 CPU & GC 减负**：
  - `DistanceFade`：将树冠/洞穴柱体透明淡出改为零分配标量点积，消除 240Hz 下每秒数千次 `Vector3` 临时表分配。
  - `Input:OnUpdate`：静止帧无损缓存鼠标实体拾取结果（鼠标移动/镜头移动/界面切换 0ms 零延迟刷新；静止时跳过 ~75% 冗余 C++ 实体扫描与表分配；100% 兼容 ShowMe / Insight）。
- **联机上下洞穴免重加载加速**：跨 `SimReset` 保活地面 (`forest`)、洞穴 (`cave`) 与角色资产常驻内存（自动旁路独行长路单人模式与本地主机），联机往返地面与洞穴跳过重复解压与重载；退回主界面自动安全释放（建议 >= 8GB 内存）。
- **模组与网络等待全清空**：缓存未启用模组 `modinfo` 免重复读盘、跳过 Steam 热门模组榜请求 (`QueryTopMods`)、跳过主界面 MOTD 远程海报阻塞等待（不影响登录与联机）。
- **轻量右上角 FPS / 1% Low / P99 HUD & 原生画面设置直控**：直接接入 `ESC -> 设置 -> 画面 -> 刷新率` 及同页骨骼补帧/HUD 开关，通过 `dinput8.dll` 原生 Lua VM 桥接 0ms 即时联动（并可选自定义热键与多路径文件桥接兜底）。

---

## 实测性能数据 / Verified Benchmarks

测试环境：`Windows 10 Pro x64 · Intel i7-12700KF · RTX 4060 · 32GB RAM · 240Hz 显示器 · DST 64-bit Build 762172`

| 优化项 / Benchmark | 原版 / Vanilla | HyperSmooth v2.4.0 | 实测收益 / Gain |
| :--- | :---: | :---: | :---: |
| 进图 `LOAD BE` 资产解压加载 | `10.05s` | **`4.0s`** | **提速 2.5×** |
| `std::vector::_Insert_n` 清零热点 | `2.86s` | **`0.69s`** | **提速 4.1×** |
| Zip 解压吞吐 (`libdeflate v1.23`) | ~330 MB/s | **`830+ MB/s`** (`12,960/12,960` 100% 命中) | **3.32 GB 仅约 4 秒** |
| 联机洞穴 ⇄ 地面折返资源重载 | 重复卸载并重载 | **`0.0 MB` 零重载** | **纯客户端跳过重读** |
| 2D 骨骼动画 & 关延迟补偿位移 | 锁定 30Hz 阶梯跳动 | **真 240Hz 三通道骨骼 + 世界坐标插值** | **300×18 骨骼匹配 `292 -> 67 µs` (4.3× 提速)** |
| 单帧增量 Lua GC 最大尖峰 | `1.00ms` + 突发 `60~200ms` 全量冻屏 | **`0.30~1.60ms`** + 并发平滑避让 + 紧急兜底 | **尖峰压低 3×，Boss 战 Full-GC = 0** |
| **v2.4.0 RTSS 240Hz 稳帧器实测** | 满核忙等 (`100%` 单核占用) | **`240.0 FPS` / `1%L 221.6~239.1` / 抖动 `0.023ms`** | **85~90% 等待 0% CPU，帧时间刀切级平稳** |
| **v2.4.0 局内 60Hz 实机压测** | 30Hz + 10ms RakNet 停顿 + 纹理/网格尖峰 | **`234~238 FPS` / `P99 4.3~5.0ms` / `anim 0.01~0.24ms`** | **`301K+` 零拷贝空间查询 + 14 次纹理分片零冻屏** |

---

## 玩家安装指南 / Player Installation

### 第一步：安装客户端 Mod
- **方式 A（Steam 创意工坊）**：直接在 Steam 创意工坊订阅并启用 **HyperSmooth 240FPS & FastLoad**。
- **方式 B（本地解压安装）**：将压缩包中的 `mods/dst_smooth_highfps` 文件夹复制到游戏根目录的 `mods/` 下（例如 `...\Steam\steamapps\common\Don't Starve Together\mods\dst_smooth_highfps`），进入游戏在「客户端模组」中勾选启用。

### 第二步：安装原生引擎伴侣插件（解锁 240 帧、RTSS 稳帧、mimalloc、骨骼补帧与 2.5 倍加载）
1. 前往本仓库 **[Releases 页面](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases)** 下载最新版 `DST_HyperSmooth_v2.4.0_Release.zip`（或单独下载 `dinput8.dll`）。
2. 将压缩包中的 `bin64/dinput8.dll` 复制到游戏根目录的 **`bin64/`** 文件夹内（与 `dontstarve_steam_x64.exe` 同目录）。
3. 正常从 Steam 启动 64 位《饥荒联机版》即可自动生效！
4. 首次启动后，插件会在 `bin64/` 下自动生成配置文件 `dst_fps.ini` 和运行日志 `dst_fps_plugin.log`（仅在加载完成时输出一条精简汇总，无持续刷屏）。

> **如何卸载 / Uninstall**  
> 直接删除 `bin64/dinput8.dll`（及生成的 `dst_fps.ini`、`dst_fps_plugin.log`）并在游戏内取消勾选 Mod 即可 100% 恢复原版，不留任何残留。

---

## 局内调节与自定义热键 / In-Game Controls & Custom Hotkeys

- **推荐方式（零冲突 · 原生习惯）**：主界面或游戏局内直接按 **`ESC` -> 设置 -> 画面**：
  - 直接调整官方 **`刷新率`** 选项即可 0ms 实时切换 `60 / 120 / 144 / 165 / 240 / 360 / 无限制`（窗口化与全屏均已解锁可用！）。
  - 同页右列下方可直接开关 **`240帧骨骼补帧`** 与 **`帧率显示 HUD`**，点原生 **「应用」** 即持久生效。
- **真·全按键自由录制热键**（默认全部禁用零冲突；启用并应用后可在 Mod 配置页或 **`ESC -> 设置 -> 控制` 页面底部** 直接点击并按下任意键盘按键或鼠标中键/侧键 4/侧键 5 实时改绑）：

| 配置项 | 功能说明 | 可自由绑定范围 |
| :---: | :--- | :--- |
| **循环切换帧率热键** | 局内循环切换 `60 -> 120 -> 144 -> 165 -> 240 -> 360 -> 无限制` | `禁用(默认) / 全键盘任意键 / 鼠标中键与侧键 4、5` |
| **开关骨骼补帧热键** | 局内实时开关 **真 240 帧骨骼与位移补帧**（同屏 A/B 盲测） | `禁用(默认) / 全键盘任意键 / 鼠标中键与侧键 4、5` |
| **开关帧率 HUD 热键** | 局内一键显示/隐藏右上角实时 FPS / 1% Low HUD | `禁用(默认) / 全键盘任意键 / 鼠标中键与侧键 4、5` |

---

## 原生插件配置参考 (`bin64/dst_fps.ini`)

若安装了 Steam 创意工坊 Mod，游戏内 `ESC -> 设置 -> 画面` 与自定义热键通过 0ms 原生内存桥接即时联动；若单独使用 `dinput8.dll`，可直接编辑 `bin64/dst_fps.ini`（保存后 1 秒内热生效）：

```ini
[FPS]
target_fps=240

[OPTIMIZE]
high_res_timer=1
boost_priority=1
prewarm_bundles=1
rtss_frame_pacer=1
hybrid_frame_pacer=1
fast_mimalloc_lua=1
fast_vector_fill=1
fast_libdeflate_zip=1
fast_zlib_inflate=1
smooth_skeletal_anim=1
world_pos_interp=1
raknet_zero_stall=1
low_latency_gpu_sync=1
smooth_lua_gc=1
fast_spatial_query=1
timeslice_tex_upload=1
```

---

## 进阶说明与已知引擎特性 / Technical Notes

- **关闭延迟补偿时的世界坐标插值 (`world_pos_interp=1`)**：经反汇编验证，开延迟补偿时由本地 `Locomotor` 在 `WallUpdate` 连续推进位置；关闭延迟补偿后远程客户端仅有服务端快照按 30Hz/60Hz 写入的离散坐标，因此在 `dinput8.dll` v2.4.0 中以渲染端纯视觉连续插值（带瞬移/传送/上船熔断保护）填补帧间阶梯，丝滑且 100% 不触碰任何服务端逻辑判定。
- **异步纹理时间片 (`timeslice_tex_upload=1`)**：不强行篡改游戏 `texture_streaming` 策略，而是仅在局内实际发生按需异步纹理上传爆发时按 `1.2ms/帧` 分帧消化，读图期间全速无限制，兼顾零白块与 Boss 并发防冻屏。
- **避免重复订阅同类功能 Mod**：若同时启用两份功能重叠的 Mod（如多份 Global Positions），会额外拖慢进图并增加内存开销，建议保持精简。

---

## 作者发布指南 / Steam Workshop Upload Steps

1. 确保 `mods/dst_smooth_highfps/` 内完整包含 4 个文件：`modinfo.lua`、`modmain.lua`、`modicon.tex`、`modicon.xml`。
2. 在 Steam 库 -> 工具中启动 **Don't Starve Mod Tools**，打开 **Mod Uploader**。
3. 选择本地目录 `mods/dst_smooth_highfps`，预览图上传本发布包根目录自带的 **`workshop_preview.png`** (`512x512`)。
4. 在工坊简介中附上 `bin64/dinput8.dll` 的下载链接（例如 GitHub Release / 网盘链接），即可供所有玩家一键搭配使用！

---

## 安全性与兼容性声明 / Safety & Compatibility

- **零磁盘篡改与完整 6/6 透传**：`dinput8.dll` 完整无锁透传 `System32\dinput8.dll` 全部 6 个导出函数，仅对 `dontstarve_steam(_x64).exe` / `dontstarve_rail(_x64).exe` 生效（其他进程 0 线程 0 日志纯透传）；不 Hook D3D9/OpenGL 虚表（100% 兼容 Steam 覆盖层 / RTSS / NVIDIA 滤镜），所有跳板执行 W^X 只读固化 (`PAGE_EXECUTE_READ`)，运行时零 `.text` 写入。
- **跨更新安全 (Fail-Closed)**：若未来游戏更新导致某条特征码变化，对应模块会自动安全跳过而不崩溃（主世界骨骼插值严格受 `ALT_RENDERPATH` 特征码熔断保护）；通用 x86-64 编译，零第三方 C++ 运行时依赖（纯 Windows UCRT），SIMD 由 `libdeflate` 运行时动态检测。
- **纯客户端合规**：不修改任何联机同步数据、判定逻辑或服务端行为。
