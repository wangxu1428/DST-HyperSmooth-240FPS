# DST HyperCore Server v1.0.0（服务端天花板级性能优化引擎）

> **分支说明**：本分支 `server-hypercore` 专门存放**服务端优化引擎与纯服务端 Mod**（统一收纳于 `server_hypercore/` 目录，与 `main` 分支客户端 `DST HyperSmooth 240FPS` 清晰隔离，互不冲突）。  
> **目录结构**：
> - `server_hypercore/native_plugin/` — C++ 原生引擎源码（297 导出透明 `iphlpapi.dll` 代理：`mimalloc` 无锁 Lua 堆 + 空间网格零拷贝 + 压力驱动 30Hz 自适应 GC + `FastDataDumper` + SIMD `libdeflate`）
> - `server_hypercore/server_mod/` — 纯服务端零分歧 Lua Mod（可直接上传 Steam 创意工坊，内置中文配置与 `modicon.tex`）
> - `server_hypercore/tools/` — 自动编译脚本、297 导出代理生成器、跨版本 AOB 校验器与 7 轮消融压测套件
> - `server_hypercore/workshop_preview.png` — 512×512 创意工坊封面图

---

> **《饥荒联机版》服务端天花板级「C++ 原生引擎外置插件 (`bin64/iphlpapi.dll`) + 零分歧纯服务端 Lua Mod」双轨优化套件**  
> **Ceiling-Tier Server-Only Optimization Suite for Don't Starve Together 64-bit (`bin64/iphlpapi.dll` Native Engine + Zero-Divergence Server Mod)**
>
> - **100% Server-Only / 纯服务端生效**：客户端无需安装任何文件 / Zero client downloads required.
> - **Zero Save Lock-In / 零存档锁死**：随时删除插件与 Mod，纯原版秒读档无残留 / Remove anytime; 100% vanilla save-compatible.
> - **Scientifically Verified / 7 轮消融实锤**：经三轮反编译、红队自我绞杀与 7 轮孤立消融 + 卸载安全实测验证。

---

## 1. 三层解耦架构 / Three-Layer Architecture

### Layer 1: Native Engine Plugin (`bin64/iphlpapi.dll` · 297-Export Transparent Proxy)
- **Strict Process Isolation / 进程严格隔离**：透明转发全部 297 个 `System32\iphlpapi.dll` 导出函数（序号与名称 100% 一致），仅当宿主进程为 `dontstarve_dedicated_server_nullrenderer_x64.exe` 时激活；放入共用 `bin64/` 目录对客户端零侵入、零日志。
  *(Transparent 297/297 proxy for `System32\iphlpapi.dll`; activates exclusively in `dontstarve_dedicated_server_nullrenderer_x64.exe`, pure pass-through in client/other processes.)*
- **Module 0 · Microsecond X-Ray Profiler**：实时采集每 Tick `avg / p95 / p99 / max / overruns`、GC、空间查询、存档序列化与压缩耗时；支持按需开启 `127.0.0.1:7869/metrics` JSON 遥测（默认关闭）。
- **Module 1 · Hermetic `mimalloc v2.1.7` Lockless Lua Heap (`lua_newstate` Hook)**：定向替换 Simulation 与 WorldGen Lua VM 的 `lua_Alloc`，绕过 Klei 底层 `WaitForSingleObject` 内核态全局互斥锁与无原地 `realloc` 缺陷，同时保持非 Lua 引擎分配器零侵入。
- **Module 2 · Zero-Copy Read-Only `SpatialGrid::Query` Fast Path (`EntityManager::FindEntities`)**：消除每次空间检索按网格单元深拷贝 `std::vector<Entity*>` 的堆分配/加锁/释放风暴，TLS 只读守卫保障完全重入安全。
- **Module 3 · Pressure-Driven 30Hz Adaptive Incremental GC (`Simulation::CollectGarbage`)**：跟踪真实周期后活集水位，低压力冷静帧仅用 `0.55ms`（比原版固定 `1.0ms` 轻 45%），高负载战斗帧压低至 `0.35ms` 护航 P99，真实内存增长（`+32MB..+96MB`）时平滑爬坡 `0.85~2.5ms` 消化峰值，并将 STW 阈值抬至 `+256MB` 紧急兜底，彻底消灭 `300ms+` 停顿。
- **Module 4 · C++ Native `FastDataDumper` + SIMD `libdeflate v1.23`**：以精确 Lua 5.1 `%.14g` 精度 + 内联 `NaN/Inf` 拦截 + 128 层环保护在 C++ 端直接序列化存档表（实测 `152 MB/s`），异常自动回退原版 `DataDumper`；并以 CPUID 自动调度 AVX2/SSE2 的 `libdeflate` 替换标量 zlib Level 9 `compress2`。

### Layer 2: Zero-Divergence Server Mod (`mods/hypercore_server/` · Default ON)
- **Merm Village `RetargetFn` Unfiltered Scan Fix / 鱼人村索敌无过滤修复**：保留 100% 原版战斗逻辑，前置 `{"character"}` 与排除标签，消除鱼人王 / 沃特鱼人军团对树木、箱子、冬季盛宴糖果的无效全扫。
- **Staggered Mob Group Wakeup / 实体群体错峰苏醒**：平滑分散大基地加载瞬间的集中唤醒尖峰。
- **Safe-Whitelist Lazy `Perishable` Integral Decay / 安全白名单惰性腐烂积分**：对休眠地面与关闭容器内的白名单易腐食物启用精确温湿度积分（自动跳过全部自定义 `onperishfn` 物品、热食、光源与温控场），完整接管 `GetPercent / IsPerishing / AddTime / SetPercent / LongUpdate / OnSave` 与容器事件，**绝不向存档写入 `paused=true`**。
- **Console X-Ray Readout**：服务端控制台输入 `c_hyperstats()` 一键查看 Native + Lua 全维遥测。

### Layer 3: Opt-In World Governance (Default OFF / 默认关闭，按需开启)
- **Sleeping Ground Clutter Auto-Stack / 休眠地面杂物安全堆叠**：仅合并远离玩家且持续休眠的同种基础散落物，**永不删除任何物品** *(Never deletes any item)*.
- **Wild Beefalo Soft Carrying-Capacity / 野生牛群生态软上限**：达到上限后仅暂停野牛自然繁殖，**永不删除已有牛群或驯化牛** *(Never deletes existing or domesticated beefalo)*.

---

## 2. 7-Run Isolated Ablation & Uninstall-Safety Benchmarks (`Build 762172`)

**Stress World ("4-in-1 Hell World", Deterministic Seed, 26,010 Entities)**:
- 80 active Merms / Merm Guards + Merm King + 40 Merm Houses (with high-frequency `RetargetFn` combat scans)
- 3,000 Winter's Feast candies & loose clutter (1,000 active zone + 2,000 sleeping)
- 150 full Iceboxes (1,350 real boxed perishable foods)
- 60 active Beefalo + 16 active Ice Flingomatics
- Hardware: `Intel i7-12700KF · 32GB RAM · Windows 10 x64 · 100% Isolated Offline Sandbox`

| Benchmark Run | Avg Tick (`us`) | P95 Tick (`us`) | P99 Tick (`us`) | Max Tick (`us`) | Overruns (`>33.3ms`) | GC Avg / Max (`us`) | FindEntities (`us/t`) | SaveGame Freeze |
| :--- | ---: | ---: | ---: | ---: | :---: | ---: | ---: | ---: |
| **RUN A · Pure Vanilla Baseline** | `4,909` | `13,488` | `16,148` | `38,947` | **3** | `1,011` / `2,724` | `802` | `372.0 ms` |
| **RUN B1 · +mimalloc ONLY** | `4,837` (`-1.5%`) | `12,129` (`-10.1%`) | `15,003` (`-7.1%`) | `34,790` (`-10.7%`) | `2` | `1,002` / `2,499` | `744` (`-7.2%`) | `353.0 ms` (`-5.1%`) |
| **RUN B2 · +SpatialGrid ZC ONLY** | `5,172`\* | `11,996` (`-11.1%`) | `14,124` (`-12.5%`) | `331,900`\* | `2` | `1,469` / **`331,587`**\* | `722` (`-10.0%`) | `370.0 ms` |
| **RUN B3 · +Adaptive GC ONLY** | `4,593` (`-6.4%`) | `12,664` (`-6.1%`) | `15,215` (`-5.8%`) | `40,083` | `2` | `682` (`-32.5%`) / `2,773` | `772` (`-3.7%`) | `393.0 ms` |
| **RUN C · Layer 1 Native ALL ON** | **`4,173` (`-15.0%`)** | **`10,714` (`-20.6%`)** | **`14,591` (`-9.6%`)** | **`26,691` (`-31.5%`)** | **`0`** | **`556` (`-45.0%`)** / `2,611` | **`637` (`-20.6%`)** | `339.0 ms` (`-8.9%`) |
| **RUN D · FULL STACK (L1 + L2)** | **`3,682` (`-25.0%`)** | **`7,850` (`-41.8%`)** | **`8,323` (`-48.5%`)** | **`23,648` (`-39.3%`)** | **`0`** | **`555` (`-45.1%`)** / `2,481` | **`570` (`-28.9%`)** | **`208.0 ms` (`-44.1%`)** |

> \*In `RUN B2` (Adaptive GC OFF), vanilla Lua GC triggered a live **`331.6ms`** Stop-The-World freeze (`gc_max_us=331587`). With Adaptive GC ON (`RUN C` & `RUN D`), **0 full-GC pauses** and **0 tick overruns** occurred (`gc_max = 2.48ms`).  
> `FastDataDumper` serialized `4.09 MB` of real world entities in **`26.9ms` (`152 MB/s`)**, passing 100% `DeepEqual` round-trip verification and `NaN / Inf / Cycle` guards.  
> **RUN E (Uninstall Safety)**: After deleting `iphlpapi.dll` and disabling `hypercore_server`, 100% pure vanilla dedicated server reloaded the exact `RUN D` save cleanly (`Exit=0`): `ents=26010`, `merms=40+40`, `iceboxes=150`, `perish_foods=1393 (stuck_paused=0)`, `winter_foods=1800`, `beefalo=78`, `flingos=16`.

---

## 3. 安装与卸载 / Installation & Uninstall

1. **安装服务端 Mod / Install Server Mod**：
   - 在 Steam 创意工坊订阅 **HyperCore Server (服务端深度性能优化引擎)** 并加入服务端 `modoverrides.lua`（或将 `mods/hypercore_server/` 复制到专服 `mods/` 目录）。
2. **安装原生引擎伴侣插件 / Install Native Engine (`bin64/iphlpapi.dll`)**：
   - 从 GitHub Releases 下载 `DST_HyperCore_Server_v1.0.0_Release.zip`，将 `bin64/iphlpapi.dll` 与 `bin64/dst_hypercore.ini` 放入专服 `bin64/` 目录（与 `dontstarve_dedicated_server_nullrenderer_x64.exe` 同目录）。
3. **验证生效 / Verify**：
   - 查看 `bin64/dst_hypercore.log` 确认全部 Hook `-> OK`，并在服务端控制台执行 `c_hyperstats()`。
4. **无损卸载 / Clean Uninstall**：
   - 删除 `bin64/iphlpapi.dll` 并取消启用 Mod 即可 100% 退回原版，现有存档零锁死、直接正常加载。