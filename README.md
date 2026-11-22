# DST HyperCore Server v1.2.1 — Ceiling-Tier Dedicated Server Optimization Suite + Opt-In 60Hz SimTick

> **《饥荒联机版》服务端天花板级「C++ 原生引擎外置插件 (`bin64/iphlpapi.dll`) + 零分歧纯服务端 Lua Mod」双轨优化套件（v1.2.1 P0 存档安全热修 + 可选双端 60Hz 逻辑刻！）**  
> **Ceiling-Tier Server-Only Optimization Suite for Don't Starve Together 64-bit (`bin64/iphlpapi.dll` Native Engine + Zero-Divergence Server Mod + P0 Save-Safety Hotfix + Opt-In Dual-Side 60Hz SimTick)**
>
> - **100% Server-Only by Default / 默认纯服务端生效**：默认 `30Hz` 模式下客户端无需安装任何文件 / Zero client downloads required in default 30Hz mode.
> - **Opt-In Dual-Side 60Hz SimTick / 可选真 60Hz 双端高刷**：在 Mod 配置开启 `opt_sim_60hz`（或 `dst_hypercore.ini` 设置 `sim_tick_rate=60`）后，自动向 `DST HyperSmooth v2.4.0+` 客户端广播 `hypercore_sim60` 标签与 `HyperCoreProto` RPC，双端无缝切换 **真 60Hz 物理与走位响应**，且严格保持 **1.000× 原版真实流速**（实测 `120 ticks / 2.000s = 60.00 Hz`，腐烂速率精确到小数点后 6 位）！
> - **Zero Save Lock-In / 零存档锁死**：随时删除插件与 Mod（哪怕在 60Hz 下保存的存档！），纯原版 30Hz 秒读档无残留 / Remove anytime; 100% vanilla 30Hz save-compatible.
> - **Red-Team Hardened / 双轮红队安全加固**：经红队代码审计、C++ 6 项专项攻击回归、7 轮孤立真专服基准、5 项惰性腐烂存档红线回归与 60Hz 实测全绿验证。

### v1.2.1 P0 存档安全紧急热修 / Critical Save-Safety Hotfix
- **彻底修复 `opt_lazy_perishable` 存档读档崩溃隐患（`inventoryitem_replica.lua:225: attempt to index field 'classified'`）**：
  - 根因：旧版 `v1.2.0` 中 `Perishable:OnSave()` 同步结算导致将已过期休眠食物的负值 `time` 写入快照，而读档 `POPULATING` 阶段 `SetPosition -> OnEntitySleep` 与 `Perishable:OnLoad -> GetPercent() -> Perish() -> inst:Remove()` 联动在 `SetPersistData` 中途删除实体，引发紧随其后的 `stackable:OnLoad` 崩溃。
  - 修复：将 `GetPercent()` 与 `OnSave()` 重写为 **100% 零副作用纯计算**（过期条目安全钳制为 `time = 0.01`、`paused = nil` 落盘，过期结算推迟至 `DoTaskInTime(0)`）；为全部惰性休眠/唤醒/拾取/丢弃/错峰唤醒/杂物堆叠钩子加装 `GLOBAL.POPULATING` 防护锁，全部可变方法补齐 `inst:IsValid()` 守卫。
  - **旧存档自动无损自愈**：`Perishable:OnLoad` 自动清理残留惰性状态并将历史 `data.time <= 0` 的快照条目安全拉回 `0.01` 交由游戏首帧正常腐烂——**无需修改硬盘存档，升级 `v1.2.1` 即可直接救活受 `v1.2.0` 影响的既有存档**！
  - 新增真实专服 5 项红线回归脚本 [`tools/test_perish_savesafety.py`](server_hypercore/tools/test_perish_savesafety.py)（5/5 `ALL_PASS`），并在 50 天真实多周目联机档上实机救档验证通过。

### v1.2.0 可选真 60Hz 逻辑刻引擎 & RakNet 零停顿网络发包 / Opt-In True 60Hz SimTick & RakNet Zero-Stall
- **独占 `Simulation::Update` 固定步长常数 + 4/4 子系统时钟同步安全切频（Fail-Closed 验值）**：经全可执行文件 XREF 证明，`0x140458D08` (`float`)、`0x140459860` (`double`) 以及 4 处二次 `mTick*(1/30)` 子系统（`AnimShader` / `PathFinder` / `Spatial` / `SimTimeDiff`）独占服务于固定步长逻辑；切换至 `1/60` 时，克雷原生调度器（`scheduler.lua`、`stategraph.lua`）自动按 `GetTickTime()` 换算秒数，`FRAMES = 1/30` 保持恒定，实现 **双倍逻辑精度 + 精确 1.000× 真实游戏速度**。
- **自动同步 `NetworkTickRate -> U=60(1)` + 120Hz Waitable-Timer 2× Nyquist 稳帧**：60Hz 模式下自动修正 `SetNetworkTickRate` 的 `30.5f -> 60.5f` 分频基准并将默认 15Hz 自动升至 `U=60(1)`（尊重服主 `cluster.ini` 显式配置）；同时将服务端外层 `FrameLimiter::Wait` 升级为 `120Hz` 高精度内核等待计时器（Win10 过冲防抖 `[450, 1850] µs`），彻底消除 60fps `Sleep(1)` 与 60Hz SimTick 的 `0/2/0/2` 拍频抖动。
- **RakNet 事件驱动零停顿唤醒 (`enable_raknet_instant_wake = 1`)**：经双调用点交叉校验 + 成对回滚保护，消除服务端 `SendBuffered` / `SendBufferedList` 对非 `IMMEDIATE` 快照包跳过 `SetEvent()` 导致的 `0~10ms` `WaitOnEvent` 发包滞留（纯事件驱动、0% 空闲 CPU）。
- **60Hz 自适应 GC 预算折半保护与超时基准动态对齐**：60Hz 模式下自动将单 Tick 预算线对齐 `16.67ms`（闲时 GC 基准平滑收束至 `0.30ms`，每秒总吞吐与 30Hz 保持一致）。
- **双通道客户端零配置自动握手**：服务端在 `forest_network` / `cave_network` / `lavaarena_network` / `quagmire_network` / `world` 复制 `hypercore_sim60` 标签，并在 `ms_playerjoined` 推送 `ClientModRPC("HyperCoreProto", "SimTickRate", 60)`（含同进程共载槽位 `id=1` 去重保护）；`DST HyperSmooth v2.4.0+` 客户端连接时自动解锁 60Hz 与 `D=0` 无节流收包，断开进公共服自动归零 30Hz。
- **孤立真专服 60Hz 实测结果**：`GetTickTime = 0.016667 (1/60.0)`、`world_net_tag = true`、稳态 2 秒窗口实测 **`d_ticks = 120`、`sim_dt = 2.000s`、`wall_dt = 2.000s` (`60.00 Hz`)**、胡萝卜腐烂下降 **`0.000625`**（理论值 `2.0 × 1.5 / 4800 = 0.000625` 零误差），且 60Hz 存档删除插件与 Mod 后由 100% 纯原版 30Hz 专服零报错完整读档（`ents = 15,672`, `Exit = 0`）！

### v1.1.0 红队安全与架构全面加固 / Security & Architecture Hardening
- **`FastDataDumper` 内存与环表安全防线**：新增基于 `lua_topointer` 的 $O(1)$ 活动栈环检测（15 µs 秒拒多分支环表 `{a=c, b=c}`，彻底封堵 $2^{128}$ 指数爆炸）+ `2,500,000` 共享 DAG 节点上限 + `GrowBuf` 256MB 严苛容量/OOM 全路径返回校验（零空指针解引用/堆溢出）+ 峰值后自动收缩至 4MB；改用 C++17 `std::to_chars` 彻底免疫德/法等逗号小数 `LC_NUMERIC` 区域环境。
- **Windows x64 SEH 栈展开完备 + 无锁代理 + W^X 内存隔离**：`iphlpapi_thunks.S` 补齐完整 `.seh_proc / .seh_stackalloc 0x88 / .seh_endprologue / .seh_endproc`（`.pdata` 异常展开表实测验证）与按 API 契约精准分类的失败返回值表，移除脆弱的 Win10 硬编码序号 `@1..@297`；替换 `InitOnceExecuteOnce` 为无锁原子懒加载；所有跳板安装完成后立即收敛为 **`PAGE_EXECUTE_READ`**（W^X 零常驻 `RWX` 页）；接入模块钉扎（`GET_MODULE_HANDLE_EX_FLAG_PIN`）并清理 `DllMain` 退出阻塞/线程拉起。
- **`SpatialGrid` Fail-Closed 校验与边界等价保护**：对 `SpatialGrid::Query` 入口与 `EntityVisitor` 实施双独立 AOB Fail-Closed 校验与成对回滚；对 `NaN / Inf / math.huge / 负半径 / >100000` 统一走原版等价回退（杜绝 `cvttss2si` 未定义行为）。
- **多分片零碰撞与精细兼容**：按 `-shard <name>` 自动拆分独立日志（`dst_hypercore_Master.log` / `dst_hypercore_Caves.log`）；HTTP 遥测接入 `SRWLOCK` 线程同步、`SO_EXCLUSIVEADDRUSE` 防端口劫持、`2s` 读写超时并移除通配 `CORS`；Lua 端修复鱼人索敌保留第三方 Mod 兼容、野牛软上限精确排除玩家驯化/带鞍牛。

---

## 1. 三层解耦架构 / Three-Layer Architecture

### Layer 1: Native Engine Plugin (`bin64/iphlpapi.dll` · 297-Export Transparent Proxy)
- **Strict Process Isolation / 进程严格隔离**：无锁透明转发全部 297 个 `System32\iphlpapi.dll` 导出函数（纯按名称导出，含完备 Windows x64 SEH `.pdata` 栈展开表），仅当宿主进程为 `dontstarve_dedicated_server_nullrenderer_x64.exe` 时激活；放入共用 `bin64/` 目录对客户端零侵入、零日志。
  *(Lock-free 297/297 named proxy for `System32\iphlpapi.dll` with full x64 SEH `.pdata` unwind metadata; activates exclusively in `dontstarve_dedicated_server_nullrenderer_x64.exe`, pure pass-through in client/other processes.)*
- **Module 0 · Microsecond X-Ray Profiler**：`SRWLOCK` 线程安全采集每 Tick `avg / p95 / p99 / max / overruns`、GC、空间查询、存档序列化与压缩耗时；按 `-shard` 自动隔离日志，支持按需开启 `127.0.0.1:7869/metrics` JSON 遥测（默认关闭、`SO_EXCLUSIVEADDRUSE` 独占保护）。
- **Module 1 · Hermetic `mimalloc v2.1.7` Lockless Lua Heap (`lua_newstate` Hook)**：定向替换 Simulation 与 WorldGen Lua VM 的 `lua_Alloc`（仅在分配成功后计账），绕过 Klei 底层 `WaitForSingleObject` 内核态全局互斥锁与无原地 `realloc` 缺陷，同时保持非 Lua 引擎分配器零侵入。
- **Module 2 · Zero-Copy Read-Only `SpatialGrid::Query` Fast Path (`EntityManager::FindEntities`)**：双独立 AOB Fail-Closed 校验 + 边界安全守卫，消除每次空间检索按网格单元深拷贝 `std::vector<Entity*>` 的堆分配/加锁/释放风暴，TLS 只读守卫保障完全重入安全。
- **Module 3 · Pressure-Driven 30Hz Adaptive Incremental GC (`Simulation::CollectGarbage`)**：跟踪真实周期后活集水位，结合 `gc_spare_target_ms` 动态调度增量预算，低压力冷静帧仅用 `0.55ms`，高负载战斗帧压低至 `0.35ms` 护航 P99，真实内存增长（`+32MB..+96MB`）时强制平滑爬坡 `0.85~2.5ms` 防止饥饿，并将 STW 阈值抬至 `+256MB` 紧急兜底，彻底消灭 `380ms+` 停顿。
- **Module 4 · C++ Native `FastDataDumper` + SIMD `libdeflate v1.23`**：以区域无关 `std::to_chars` 精确 Lua 5.1 14 位有效数字精度 + 内联 `NaN/Inf` 拦截 + $O(1)$ 环检测 + 250 万节点/256MB 安全上限在 C++ 端直接序列化存档表（实测 **`155 MB/s`**），异常自动回退原版 `DataDumper`；并以 CPUID 自动调度 AVX2/SSE2 的 `libdeflate` 替换标量 zlib Level 9 `compress2`。

### Layer 2: Zero-Divergence Server Mod (`mods/hypercore_server/` · Default ON)
- **Merm Village `RetargetFn` Unfiltered Scan Fix / 鱼人村索敌无过滤修复**：保留 100% 原版战斗逻辑与第三方 Mod 兼容（自动检测非原版 `targetfn` 不覆盖），前置 `{"character"}` 与排除标签，消除鱼人王 / 沃特鱼人军团对树木、箱子、冬季盛宴糖果的无效全扫。
- **Staggered Mob Group Wakeup / 实体群体错峰苏醒**：平滑分散大基地加载瞬间的集中唤醒尖峰。
- **Safe-Whitelist Lazy `Perishable` Integral Decay / 安全白名单惰性腐烂积分**：对休眠地面与关闭容器内的白名单易腐食物启用精确温湿度积分（自动跳过全部自定义 `onperishfn` 物品、热食、光源与温控场），完整接管 `GetPercent / IsPerishing / AddTime / SetPercent / LongUpdate / OnSave` 与容器事件，**绝不向存档写入 `paused=true`**。
- **Console X-Ray Readout**：服务端控制台输入 `c_hyperstats()` 一键查看 Native + Lua 全维遥测。

### Layer 3: Opt-In World Governance (Default OFF / 默认关闭，按需开启)
- **Sleeping Ground Clutter Auto-Stack / 休眠地面杂物安全堆叠**：仅合并远离玩家且持续休眠的同种基础散落物，**永不删除任何物品** *(Never deletes any item)*.
- **Wild Beefalo Soft Carrying-Capacity / 野生牛群生态软上限**：达到上限后仅暂停野牛自然繁殖，实时精确排除玩家已驯化/正在驯化/带鞍牛，**永不删除已有牛群或驯化牛** *(Never deletes existing or domesticated beefalo)*.

---

## 2. 7-Run Isolated Ablation & Uninstall-Safety Benchmarks (`v1.1.0 · Build 762172`)

**Stress World ("4-in-1 Hell World", Deterministic Seed, 26,028 Entities)**:
- 80 active Merms / Merm Guards + Merm King + 40 Merm Houses (with high-frequency `RetargetFn` combat scans)
- 3,000 Winter's Feast candies & loose clutter (1,000 active zone + 2,000 sleeping)
- 150 full Iceboxes (1,350 real boxed perishable foods)
- 60 active Beefalo + 16 active Ice Flingomatics
- Hardware: `Intel i7-12700KF · 32GB RAM · Windows 10 x64 · 100% Isolated Offline Sandbox`

| Benchmark Run | Avg Tick (`us`) | P95 Tick (`us`) | P99 Tick (`us`) | Max Tick (`us`) | Overruns (`>33.3ms`) | GC Avg / Max (`us`) | FindEntities (`us/t`) | SaveGame Freeze |
| :--- | ---: | ---: | ---: | ---: | :---: | ---: | ---: | ---: |
| **RUN A · Pure Vanilla Baseline** | `5,334` | `13,140` | `17,728` | `386,043`\* | **2** | `1,545` / **`380,391`**\* | `808` | `399.0 ms` |
| **RUN B1 · +mimalloc ONLY** | `5,222` (`-2.1%`) | `13,178` | `18,694` | `37,544` | `3` | `1,018` / `2,781` | `834` | `382.0 ms` (`-4.3%`) |
| **RUN B2 · +SpatialGrid ZC ONLY** | `5,295`\* | `12,723` (`-3.2%`) | `15,839` (`-10.7%`) | `410,515`\* | `3` | `1,595` / **`410,142`**\* | `800` | `394.0 ms` |
| **RUN B3 · +Adaptive GC ONLY** | `4,682` (`-12.2%`) | `14,017` | `16,981` | `38,025` | `1` | `686` (`-55.6%`) / `2,580` | `768` (`-5.0%`) | `393.0 ms` |
| **RUN C · Layer 1 Native ALL ON** | **`4,323` (`-19.0%`)** | **`12,042` (`-8.4%`)** | **`15,457` (`-12.8%`)** | **`28,612` (`-92.6%`)** | **`0`** | **`557` (`-63.9%`)** / `2,884` | **`675` (`-16.5%`)** | `341.0 ms` (`-14.5%`) |
| **RUN D · FULL STACK (L1 + L2)** | **`3,631` (`-31.9%`)** | **`7,673` (`-41.6%`)** | **`8,620` (`-51.4%`)** | **`29,265` (`-92.4%`)** | **`0`** | **`556` (`-64.0%`)** / `2,750` | **`607` (`-24.9%`)** | **`215.0 ms` (`-46.1%`)** |

> \*In `RUN A` & `RUN B2` (Adaptive GC OFF), vanilla Lua GC triggered **`380.4ms` / `410.1ms`** Stop-The-World freezes. With Adaptive GC ON (`RUN C` & `RUN D`), **0 full-GC pauses** and **0 tick overruns** occurred (`gc_max = 2.75ms`).  
> `FastDataDumper v1.1.0` serialized `4.09 MB` of real world entities in **`26.4ms` (`154.9 MB/s`)**, passing live `DeepEqual` round-trip verification + multi-branch cycle (`cycle_detected`) + exponential $2^{24}$ DAG (`node_limit_exceeded`) + `SpatialGrid` edge-radii (`r=5000: 20,313 ents`, `r=math.huge: 0`, `r=-5: 0`) parity checks.  
> **RUN E (Uninstall Safety)**: After deleting `iphlpapi.dll` and disabling `hypercore_server`, 100% pure vanilla dedicated server reloaded the exact `RUN D` save cleanly (`Exit=0`): `ents=26028`, `merms=40+40`, `iceboxes=150`, `perish_foods=1393 (stuck_paused=0)`, `winter_foods=1800`, `beefalo=78`, `flingos=16`.

---

## 3. 安装与卸载 / Installation & Uninstall

1. **安装服务端 Mod / Install Server Mod**：
   - 在 Steam 创意工坊订阅 **HyperCore Server (服务端深度性能优化引擎)** 并加入服务端 `modoverrides.lua`（或将 `mods/hypercore_server/` 复制到专服 `mods/` 目录）。
2. **安装原生引擎伴侣插件 / Install Native Engine (`bin64/iphlpapi.dll`)**：
   - 从 GitHub Releases 下载 `DST_HyperCore_Server_v1.2.1_Release.zip`，将 `bin64/iphlpapi.dll` 与 `bin64/dst_hypercore.ini` 放入专服 `bin64/` 目录（与 `dontstarve_dedicated_server_nullrenderer_x64.exe` 同目录）。
3. **验证生效 / Verify**：
   - 查看 `bin64/dst_hypercore_Master.log`（或 `dst_hypercore_Caves.log`）确认全部 Hook `-> OK` 且跳板已锁定 W^X (`PAGE_EXECUTE_READ`)，并在服务端控制台执行 `c_hyperstats()`。
4. **无损卸载 / Clean Uninstall**：
   - 删除 `bin64/iphlpapi.dll` 并取消启用 Mod 即可 100% 退回原版，现有存档零锁死、直接正常加载。
