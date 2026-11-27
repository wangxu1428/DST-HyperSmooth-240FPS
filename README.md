<p align="right">
  <strong>简体中文</strong> | <a href="README_EN.md">English</a>
</p>

# DST HyperCore Server v1.6.0-beta1 — Windows & Linux Dedicated Server Native Engine

> **《饥荒联机版》服务端旗舰级「原生 C++ 引擎 (`bin64/iphlpapi.dll` / `bin64/libhypercore.so`) + 零分歧纯服务端 Mod」双端全栈优化套件**  
> **Ceiling-Tier Server-Only Native Engine (Windows + Linux x86_64) + Module 10B Replica Pacing + O(N) ReplicaFastPath + Causal 3-Ch RPC QoS + Server HW Tier + Spike Blackbox**  
> 👉 **Linux x86_64 专用独立目录**：[`server_hypercore_linux/`](server_hypercore_linux/README.md)（[English Guide](server_hypercore_linux/README_EN.md)） | 客户端分支：[`main`](https://github.com/wangxu1428/DST-HyperSmooth-240FPS) | [Releases 下载](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases)
>
> - **默认 100% 纯服务端生效**：默认 `30Hz` 模式下客户端零下载、所有原版玩家直接进服。
> - **Windows & Linux 双端支持**：Windows 使用 `bin64/iphlpapi.dll` 零锁代理，Linux x86_64 新增原生 `LD_PRELOAD` 引擎 `libhypercore.so`（基于 `glibc 2.17` 交叉构建，详情见 [`server_hypercore_linux/`](server_hypercore_linux/README.md)）。
> - **可选双端真 60Hz 逻辑刻（Windows）**：开启 `opt_sim_60hz` 后自动向 `DST HyperSmooth v2.4.0+` 客户端广播握手，双端无缝切换 **60Hz 物理与走位精度**，严格保持 **`1.000×` 原版游戏流速**，且支持同进程 `30Hz ⇄ 60Hz ⇄ 30Hz` 无损热切（Linux 端为保障 `.rodata` 浮点常量安全，保持原版 `30Hz`）。
> - **零存档锁死（Zero Save Lock-In）**：随时卸载插件与 Mod（包括 60Hz 下保存的存档），100% 纯原版 30Hz 专服零报错完整读档。

---

## v1.6.0-beta1 核心升级与全模块矩阵 / What's New in v1.6.0-beta1

### 0. Module 10B 零失步分层副本分帧平滑 + `O(N)` 副本极速引擎 + 硬件档位 & 尖峰黑匣子
- **Module 10B 零失步分层副本构造限流（Replica Construction Pacing）**：进入 1500+ 实体巨型基地或跳虫洞时，自动按距离分桶平滑下发远距静态建筑，消除 250KB RM3 网络洪峰与客户端瞬时卡顿；世界/网络代理、玩家、Classified、背包物品及 14 格近身威胁 0ms 直达，且父先于子提升杜绝挂件失步；支持 `c_hyperpacing()` 实时调参。
- **`O(N)` 副本极速路径 & 因果 3 通道 `RakNet::RPC4` QoS**（[replica_fastpath.h](server_hypercore/native_plugin/replica_fastpath.h) / [rpc_qos.h](server_hypercore/native_plugin/rpc_qos.h)）：替代大基地高频遍历扫描并将非关键广播隔离出 Ch0，降低主循环与网络抖动。
- **三档硬件性能策略 + `C++ FastJson` + 专服尖峰黑匣子（Spike Blackbox）**（[server_hardware_tier.h](server_hypercore/native_plugin/server_hardware_tier.h) / [fast_json.h](server_hypercore/native_plugin/fast_json.h) / [spike_blackbox.h](server_hypercore/native_plugin/spike_blackbox.h)）：支持 `c_hypertier()` 调度、原生 JSON 编解码与专服卡顿快照导出；同步在 [modinfo.lua](server_hypercore/server_mod/modinfo.lua) 声明 `dnh_github` 伴侣托管元数据。

### 1. 异步原子存档提交队列（Module 8 · `enable_async_save = 1`）与子阶段细分探针
- **64MB 严格因果 FIFO 异步落盘**（[async_save_commit.h](server_hypercore/native_plugin/async_save_commit.h)）：截获 `WritePersistentFile` 并将快照序列化/压缩后的实际磁盘写入卸载至后台独立 I/O 线程，主模拟线程仅耗时微秒级入队（实测 `enq=11 ok=11 err=0 enqUs=76`）。
- **完备读/删/目录/退出屏障**：自动挂钩 Win32 Kernel32 / MSVCR90（以及 Linux POSIX）的 `CreateFile` / `MoveFile` / `DeleteFile` / `stat` / `opendir` / `exit` / `_exit` / `_cexit`，任何同路径后续读写或关服前自动同步刷盘 + `.hc_tmp` 原子替换（Linux 上使用 `fsync` + `rename`），杜绝世界存档损坏或半写入。
- **子阶段细分遥测 (`[PROF-SUB]`)**：同步挂钩 `Simulation::luaUpdate`、`cNetworkManager::Update`、`ReplicaManager3::Update`，毫秒/微秒精准拆分每帧 Lua 脚本、网络收敛与实体同步耗时，彻底定位卡服元凶。

### 2. 只读 Mod/资源负向 Stat 缓存（Module 7 · `enable_fs_neg_cache = 1`）
- 复用跨端只读负向缓存（[fs_neg_cache.h](server_hypercore/native_plugin/fs_neg_cache.h)），在大型多 Mod 专服启动与资源查找时直接命中不存在路径的负向结果，搭配写/删/重命名/建目录 Epoch 失效保障，消除无效系统调用抖动。

### 3. 全新 Linux x86_64 原生服务端引擎（[`server_hypercore_linux/`](server_hypercore_linux/README.md)）
- 全量移植核心原生优化至 Linux x86_64 ELF（Build `762172`）：Module 0A/0B 探针、Module 1 无锁 `mimalloc v2.1.7`（`MI_OVERRIDE=0`）、Module 2 自适应 GC + Anti-STW 盾、Module 3 四点原位 W^X `SpatialGrid` 零拷贝、Module 4 SIMD `libdeflate v1.23` + `FastDataDumper` + `FastLoad`、Module 6 `O(N log N)` `FastAStar`、Module 7 `PosixFsNegCache` 与 Module 8 POSIX 异步原子落盘。
- 使用 `exports.map` 严格导出白名单，仅暴露 `19` 个必要的 `LD_PRELOAD` POSIX 拦截符号，通过 `/proc/self/exe` 严格限定仅在 `dontstarve_dedicated_server_nullrenderer_x64` 中激活，`bash` / `steamcmd` 等辅助进程零侵入直接透传。
- **当前验证边界说明**：`libhypercore.so` 已在 Windows 上通过 Zig `0.17.1`（目标 `x86_64-linux-gnu.2.17`，仅依赖 `GLIBC <= 2.17` 10 个基础符号，零 `libstdc++` 运行时依赖）零警告编译，并在真实 Linux Build `762172` ELF 上通过 `57/57` AOB 唯一匹配、`19/19` Capstone 指令边界、`AStar::Step+0x3CC -> 0x13a410` 与 `4/4` SpatialGrid 原位补丁静态验证；**尚未在真实 Linux 宿主机/容器内完成实机开服跑测**，欢迎 Linux 服主实机反馈问题！

### 4. `O(N log N)` `FastAStar` 寻路、2.5× 极速加载与反 STW GC 护盾
- **巨型围墙基地 23×~29× 寻路提速**（[fast_astar.h](server_hypercore/native_plugin/fast_astar.h) + [fast_astar_manager.h](server_hypercore/native_plugin/fast_astar_manager.h)）：`1,000` 步封闭大基地基准 `78.02ms → 2.62ms`（提速 `29.7×`），配备 64 位单调代数防同槽位 ABA 与读写锁隔离，4 线程 80,000 次并发 0 UAF。
- **专服 2.5× 极速加载 (`enable_fast_load = 1`)**：移植 `minizip unzReadCurrentFile` → SIMD `libdeflate`、`inflate_fast` 64 位重写及 `std::vector::_Insert_n` 硬件 `rep stosb`，大幅缩短 Master + Caves 启动时间。
- **反 STW 全量 GC 护盾 (`enable_anti_stw_gc = 1`)**：拦截突发 `LUA_GCCOLLECT` 转化为平滑增量预算，配合 `lua_close` 生命周期重置与 OOM 熔断保护。
- **R3.1 安全基线**：Loader Lock 外安全初始化 (`outsideLoaderLock=1`)、惰性腐烂默认关闭且完善 `StopPerishing`/`StartPerishing` 结算、`30Hz ⇄ 60Hz ⇄ 30Hz` 完整还原网络同步速率。

---

## 1. 三层解耦架构 / Three-Layer Architecture

- **Layer 1 · 原生引擎插件**（Windows: `bin64/iphlpapi.dll` 297 导出零锁代理 | Linux: `bin64/libhypercore.so` `LD_PRELOAD`）：
  - **Module 0**: 微秒级 X-Ray 主循环 & `[PROF-SUB]` 子阶段（`luaUpdate`/`netUpdate`/`repUpdate`）统计；按 `-shard` 输出 `dst_hypercore_Master.log` / `dst_hypercore_Caves.log`。
  - **Module 1**: 独立无锁 `mimalloc v2.1.7` Lua VM 堆分配器。
  - **Module 2**: 零拷贝只读 `SpatialGrid::Query`（`FindEntities` & `LightWatcher`），含 `NaN`/`math.huge` 边界等价保护。
  - **Module 3**: 压力感知自适应增量 GC + Anti-STW 护盾。
  - **Module 4**: `FastDataDumper` (155 MB/s) + SIMD `libdeflate` + 2.5× `FastLoad`。
  - **Module 5**: （Windows 独占）可选 60Hz SimTick + 120Hz Waitable-Timer + RakNet 零停顿唤醒。
  - **Module 6**: `O(N log N)` `FastAStar` 扁平哈希 + 二叉堆寻路引擎。
  - **Module 7**: 只读 Mod/数据负向 Stat 缓存 (`FsNeg`)。
  - **Module 8**: 64MB 有界严格因果 FIFO 异步原子存档队列 (`AsyncSave`)。
- **Layer 2 · 零分歧服务端 Mod (`server_hypercore/server_mod/`)**：鱼人村索敌过滤修复、大基地错峰苏醒、C++ `FastDataDumper` 桥接、安全白名单惰性腐烂（默认关）及 `c_hyperstats()` 一键体检。
- **Layer 3 · 按需生态治理（默认关）**：休眠地面散落物安全合并 (`opt_autostack`) 与野生牛群生态软上限 (`opt_wild_beefalo_cap`)，绝不删除已有物品或驯化牛群。

---

## 2. 实测基准汇总 (`Build 762172`)

- **26,028 实体「四合一地狱压力档」7 轮消融实测**（`i7-12700KF · 32GB · Windows x64`）：
  - 全栈开启（L1+L2）：平均 Tick **`5,334 → 3,631 µs (-31.9%)`**、P99 Tick **`17,728 → 8,620 µs (-51.4%)`**、最大尖峰 **`386.0ms → 29.3ms (-92.4%)`**、超时帧 **`2 → 0`**、Full-GC 冻服 **`380.4ms → 0ms`**、存档冻屏 **`399.0ms → 215.0ms (-46.1%)`**。
- **最新全链路自动化测试**：`test_redteam_server.exe` **22/22 PASS**、`test_proxy_passthrough.py` **ALL PASS**（297 导出、非服务端零挂钩）、`test_server_lua_v14.py` **8/8 ALL_PASS (`rc=0`)**、`test_linux_elf_verify.py` **ALL PASS (`rc=0`，静态 ELF64 + 66/66 Capstone AOB + 22/22 序言)**。

---

## 3. 快速安装、更新与卸载 / Installation & Uninstall

### Windows 专服
1. **安装服务端 Mod**：Steam 创意工坊订阅 **HyperCore Server**（或复制 `server_hypercore/server_mod/` 到专服 `mods/hypercore_server/`）。
2. **安装原生插件 (`bin64/iphlpapi.dll`)**：
   - 下载解压 **`DST_HyperCore_Server_v1.6.0-beta1_Release.zip`**，关闭饥荒/专服，双击 **`一键安装与更新.bat`** 自动识别目录并校验 SHA256（卸载选 `2`）；
   - 或手动将 `bin64/iphlpapi.dll` 与 `bin64/dst_hypercore.ini` 放入专服 **`bin64/`**（与 `dontstarve_dedicated_server_nullrenderer_x64.exe` 同目录），亦支持通过 **DNH 伴侣（`dnh_github`）** 自动拉取 `iphlpapi.dll`。

### Linux x86_64 专服
- 下载 **`DST_HyperCore_Server_Linux_v1.6.0-beta1_Release.zip`**，将 `bin64/libhypercore.so`、`bin64/dst_hypercore.ini` 与 `bin64/linux_start_with_hypercore.sh` 放入 Linux 专服 **`bin64/`**（不要放到 32 位 `bin/` 或 `mods/`），通过 `linux_start_with_hypercore.sh` 或设置 `LD_PRELOAD=<路径>/bin64/libhypercore.so` 启动即可！
- 详细部署、`systemd` / Docker / 翼龙面板模板与构建指南请移步独立文档 👉 **[`server_hypercore_linux/README.md`](server_hypercore_linux/README.md)**。
