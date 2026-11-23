# DST HyperCore Server v1.3.0 — Dedicated Server Native Engine + Zero-Divergence Mod

> **《饥荒联机版》服务端旗舰级「C++ 原生引擎外置插件 (`bin64/iphlpapi.dll`) + 零分歧纯服务端 Mod」双轨优化套件**  
> **Ceiling-Tier Server-Only Native Engine (`bin64/iphlpapi.dll`) + Zero-Divergence Mod + FastAStar + 2.5× FastLoad + Opt-In Dual-Side 60Hz SimTick**
>
> - **默认 100% 纯服务端生效**：默认 `30Hz` 模式下客户端零下载、所有原版玩家直接进服。
> - **可选双端真 60Hz 逻辑刻**：开启 `opt_sim_60hz` 后自动向 `DST HyperSmooth v2.4.0+` 客户端广播握手，双端无缝切换 **60Hz 物理与走位精度**，严格保持 **`1.000×` 原版游戏流速**，且支持同进程 `30Hz ⇄ 60Hz ⇄ 30Hz` 无损热切。
> - **零存档锁死（Zero Save Lock-In）**：随时卸载插件与 Mod（包括 60Hz 下保存的存档），100% 纯原版 30Hz 专服零报错完整读档。

---

## v1.3.0 核心升级 / What's New in v1.3.0

### 1. `O(N log N)` 扁平哈希 + 二叉堆 `FastAStar` 寻路引擎（巨型围墙基地 **23×~29×** 提速）
- 原版饥荒寻路在巨型围墙基地或不可达封闭建筑中退化为 $O(S^2 \log S)$，单次寻路极易触发数十至数百毫秒服务端卡顿。
- `iphlpapi.dll` 新增三钩子原子安装的 C++ 原生 `FastAStar` 引擎（[fast_astar.h](native_plugin/fast_astar.h) + [fast_astar_manager.h](native_plugin/fast_astar_manager.h)），与原版路径代价/节点数精确一致；配备 `StateManager<8>` 共享池 + **64 位单调代数 `gen` 彻底消除同槽位 ABA**，4 线程 80,000 次并发压测 0 UAF；1,000 步封闭大基地基准实测 **`78.02ms → 2.62ms`（提速 29.7×）**！

### 2. 专服 2.5× 极速加载引擎（`enable_fast_load = 1` · 本地开洞穴 / 独服重启大幅提速）
- 将客户端历经数百小时验证的三大加载加速引擎完整移植至专服（与 `dontstarve_dedicated_server_nullrenderer_x64.exe` 指令 100% 一致，复用已内置 `libdeflate v1.23` 零新依赖）：
  1. **`minizip unzReadCurrentFile` → SIMD `libdeflate v1.23` 整块解压**（AVX2/SSE2 运行时自动调度，共享 W^X 只读跳板池）；
  2. **`zlib 1.2.3 inflate_fast` → 64 位 `HookInflateFast`**；
  3. **`std::vector::_Insert_n` Loop A/B → 硬件 `rep stosb`**。
- 显著加快本地带洞穴建房（同时拉起 Master + Caves 两个专服）与独立专服的脚本/资源包/存档解压启动速度，`[PROF]` 日志实时输出 `FastLoad(unz=... MB=... ms=...)` 实测统计。

### 3. 反 STW 启动与 Mod 全量 GC 护盾（`enable_anti_stw_gc = 1`）
- 拦截运行期外部 `LUA_GCCOLLECT` 突发全量清扫，在安全内存水位内自动平滑转化为有界增量回收（[anti_stw_gc.h](native_plugin/anti_stw_gc.h)），并配合 `lua_close` 生命周期自动重置与高水位 OOM 硬熔断，防止慢泄漏与联机瞬间冻服。

### 4. R3.1 红队全量安全加固与存档安全默认策略
- **Loader Lock 外安全延迟初始化 (`outsideLoaderLock=1`)**：通过主入口点延迟桩在 Windows 释放 Loader Lock 之后、`main()` 之前完成全量初始化与 Hook 安装，杜绝 `DllMain` 死锁风险。
- **`30Hz ⇄ 60Hz ⇄ 30Hz` 完整对称切频**：从 60Hz 切回 30Hz 时完整还原 `SetNetworkTickRate` 至默认 `U=15(2)` 或保留服主 `cluster.ini` 显式自定义 `tick_rate`。
- **惰性腐烂安全边界加固**：`opt_lazy_perishable` 改为**默认关闭（按需手动开启）**，并补齐 `StopPerishing()` / `StartPerishing()` 累计惰性时间精确结算（真专服 7/7 存档红线回归全通）。
- **配套开源一键安装/更新工具（`tools/一键安装与更新.bat`）**：自动识别 Steam / 专服 / WeGame 的 `bin64/` 目录，保留玩家自定义 `dst_hypercore.ini`，自动备份旧 DLL + SHA256 逐字节校验 + 一键卸载恢复原版。

---

## 1. 三层解耦架构 / Three-Layer Architecture

### Layer 1: 原生引擎伴侣插件 (`bin64/iphlpapi.dll` · 297 导出零锁透明代理)
- **进程严格隔离**：无锁按名称透明转发 `System32\iphlpapi.dll` 全部 297 个导出（含完备 x64 SEH `.pdata` 栈展开表），仅在 `dontstarve_dedicated_server_nullrenderer_x64.exe` 中激活；放在客户端共用 `bin64/` 目录对客户端 0 侵入、0 日志。
- **Module 0 · 微秒级 X-Ray 遥测**：`SRWLOCK` 线程安全记录 `tick_us (avg/p95/p99/max/overruns)`、GC、`FindEntities`、`FastAStar`、`FastDataDumper`、`Deflate` 与 `FastLoad` 解压指标；按 `-shard` 自动隔离日志（`dst_hypercore_Master.log` / `dst_hypercore_Caves.log`），可选开启只读本机 `127.0.0.1:7869/metrics`。
- **Module 1 · 独立无锁 `mimalloc v2.1.7` Lua 堆 (`lua_newstate`)**：消除 Simulation / WorldGen Lua VM 的全局互斥锁争抢与低效 `realloc`。
- **Module 2 · 零拷贝只读 `SpatialGrid::Query` (`EntityManager::FindEntities` & `LightWatcher`)**：双 AOB Fail-Closed 校验 + `NaN / math.huge / 负半径` 边界等价回退，彻底消除每帧 `std::vector` 深拷贝风暴。
- **Module 3 · 压力感知自适应增量 GC + 反 STW 护盾**：动态追踪真实活集水位，结合单 Tick 富余时间平滑调度增量预算（60Hz 自动按 `16.67ms` 预算对齐），消灭 `380ms+` Stop-The-World 卡顿。
- **Module 4 · `FastDataDumper` (155 MB/s) + SIMD `libdeflate` 存档压缩 + 2.5× `FastLoad` 解压**：区域无关 `std::to_chars` 精确序列化 + $O(1)$ 环检测 + 250 万节点/256MB 安全熔断；存档写压与启动读包双双向加速。
- **Module 5 · 可选真 60Hz 逻辑刻 + RakNet 事件驱动零停顿唤醒**：独占步长与 4/4 子系统同步切频 + 120Hz Waitable-Timer 2× Nyquist 稳帧 + RakNet 消除 `0~10ms` 发包滞留。
- **Module 6 · `O(N log N)` `FastAStar` 寻路引擎**：彻底抹平巨型围墙基地与封闭区域寻路尖峰。

### Layer 2: 零分歧服务端 Mod (`server_mod/` · 默认仅开高兼容安全项)
- **鱼人村索敌过滤修复 (`opt_merm_retarget_fix`, 默认开)**：前置 `{"character"}` 与排除标签（自动避让第三方自定义 `targetfn`），消灭鱼人军团对树木/箱子/糖果的无效全扫。
- **实体群体错峰苏醒 (`opt_staggered_wake`, 默认开)**：平滑分散大基地加载瞬间的集中唤醒尖峰。
- **C++ `FastDataDumper` 无缝桥接 (`opt_fast_dumper`, 默认开)**：失败自动无损回退原版 `DataDumper`。
- **安全白名单惰性腐烂 (`opt_lazy_perishable`, 默认关 · 按需开启)**：零副作用 `GetPercent`/`OnSave` + `POPULATING` 防护锁 + `StopPerishing`/`StartPerishing` 精确结算，绝不向存档写入 `paused=true`。
- **控制台一键体检**：服务端输入 `c_hyperstats()` 查看 Native + Lua 全维遥测。

### Layer 3: 按需开启生态治理（默认关闭）
- **休眠地面杂物安全堆叠 (`opt_autostack`)**：仅合并远离玩家且持续休眠的同种基础散落物，**永不删除任何物品**。
- **野生牛群生态软上限 (`opt_wild_beefalo_cap`)**：仅暂停自然繁殖，实时精确排除玩家驯化/带鞍牛，**永不删除已有牛群**。

---

## 2. 实测基准汇总 (`Build 762172`)

- **26,028 实体「四合一地狱压力档」7 轮消融实测**（`i7-12700KF · 32GB · Windows x64`）：
  - 全栈开启（L1+L2）：平均 Tick **`5,334 → 3,631 µs (-31.9%)`**、P99 Tick **`17,728 → 8,620 µs (-51.4%)`**、最大尖峰 **`386.0ms → 29.3ms (-92.4%)`**、超时帧 **`2 → 0`**、Full-GC 冻服 **`380.4ms → 0ms`**、存档冻屏 **`399.0ms → 215.0ms (-46.1%)`**。
- **巨型围墙基地 A\* 寻路实测**：1,000 步封闭路径 **`78.02ms → 2.62ms`（提速 29.7×）**，路径代价与节点数与原版 100% 精确一致。
- **60Hz 实机与纯原版重载回归**：同进程 `30Hz → 60Hz (120t / 2.000s = 60.00 Hz, U=60(1)) → 30Hz (U=15(2))` 全部通过；删除 `iphlpapi.dll` 与 Mod 后由 100% 纯原版 30Hz 专服完整读档（`ents=17,330`，物品堆叠与腐烂百分比精确到小数点后 6 位）。

---

## 3. 安装、更新与卸载 / Installation & Uninstall

1. **安装服务端 Mod**：在 Steam 创意工坊订阅 **HyperCore Server** 并加入 `modoverrides.lua`（或复制 `server_mod/` 到专服 `mods/hypercore_server/`）。
2. **安装 / 更新原生插件（`bin64/iphlpapi.dll`）**：
   - **方式 A（推荐 · 一键安装/更新）**：解压最新版 `DST_HyperCore_Server_v1.3.0_Release.zip`，先关闭饥荒/专服，双击根目录 **`一键安装与更新.bat`** 即可自动识别目录、保留现有自定义 `dst_hypercore.ini`、备份旧版 DLL 并完成 SHA256 校验安装。
   - **方式 B（手动安装）**：将 `bin64/iphlpapi.dll` 与 `bin64/dst_hypercore.ini` 复制到专服 `bin64/` 目录（与 `dontstarve_dedicated_server_nullrenderer_x64.exe` 同目录）。
3. **验证生效**：查看 `bin64/dst_hypercore_Master.log` 确认 `outsideLoaderLock=1`、`[FASTLOAD] ... -> OK` 与 `W^X -> PAGE_EXECUTE_READ`，并在控制台输入 `c_hyperstats()`。
4. **无损卸载**：运行 `一键安装与更新.bat` 选 `2`（或手动删除 `bin64/iphlpapi.dll`）并取消勾选 Mod，即可 100% 退回原版，存档零锁死。
