<p align="right">
  <strong>简体中文</strong> | <a href="README_EN.md">English</a>
</p>

# DST HyperCore Server — Linux x86_64 部署指南 (`v1.3.0`)

> 专为《饥荒联机版》Linux 64 位专用服务端（`dontstarve_dedicated_server_nullrenderer_x64`，Build `762172`）打造的原生 `LD_PRELOAD` C++ 加速引擎。  
> 返回服务端总览：[`../README.md`](../README.md) | 源码所在目录：[`../server_hypercore/native_plugin/linux/`](../server_hypercore/native_plugin/linux/hypercore_linux.cpp) | [Releases 下载](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases)

---

## 1. 包含功能与当前验证状态说明

- **核心加速模块（与 Windows 版共用统一后端核心）**：
  - **Module 0A / 0B**：微秒级主循环 `[PROF]` 与子阶段 `[PROF-SUB]`（`luaUpdate` / `netUpdate` / `repUpdate`）统计探针。
  - **Module 1**：无锁 `mimalloc v2.1.7`（`MI_OVERRIDE=0`）独立 Lua VM 分配器 + `HyperCore_*` C-API 导出。
  - **Module 2**：压力感知自适应增量 GC + Anti-STW Full-GC 护盾。
  - **Module 3**：4 处原位 W^X `SpatialGrid::Query` 零拷贝查询补丁。
  - **Module 4**：SIMD `libdeflate v1.23` 压缩 + `FastDataDumper` 序列化 + `FastLoad` 极速读包（`unzReadCurrentFile` / `inflate_fast`）。
  - **Module 6**：`O(N log N)` 扁平哈希 + 二叉堆 `FastAStar` 寻路引擎（与 MSVC/POSIX 共享统一头文件）。
  - **Module 7**：POSIX 只读 Mod/数据负向 Stat 缓存 (`PosixFsNegCache`)，拦截 `stat` / `open` / `access` misses。
  - **Module 8**：64MB 有界严格因果 FIFO 异步原子存档队列 (`PosixAsyncSaveQueue`)，采用 `.hc_tmp` + `fsync` + `rename` 配合 POSIX 读写/目录/退出屏障。
- **平台特性**：Linux 版保持原版 **`30Hz` SimTick**（不开启 60Hz 切频），并受 `glibc 2.17` 兼容约束编译，零 `libstdc++.so.6` / `libgcc_s.so.1` 运行时依赖。
- **验证境界透明说明**：`libhypercore.so` 已通过 Zig `0.17.1`（`-target x86_64-linux-gnu.2.17`）在 Windows 上零警告交叉编译产出，并通过 [test_linux_elf_verify.py](../server_hypercore/tools/test_linux_elf_verify.py) 对真实 Linux Build `762172` ELF 执行完整静态与跨二进制 Capstone 校验（`GLIBC <= 2.17`、`.dynsym` 严格 19 符号白名单、`57/57` AOB 唯一匹配、`19/19` 函数边界、`4/4` SpatialGrid 补丁校验）；**尚未在真实 Linux 宿主机/容器上实机跑服**，欢迎各路 Linux 服主试用并提交实机日志反馈！

---

## 2. 目录摆放位置（最关键 · 切勿放错）

假设你的饥荒 Linux 专服根目录为 `<DST_SERVER>`（目录内含 `bin/` 和 `bin64/` 两个目录）：

> ⚠️ **注意**：必须放进 **64 位 `bin64/` 目录**（与 `dontstarve_dedicated_server_nullrenderer_x64` 同目录），**切勿**放进 32 位 `bin/`，也**不要**把 `.so` 放进 `mods/`！

| 文件名 | 放置路径 | 说明 |
| :--- | :--- | :--- |
| `libhypercore.so` | `<DST_SERVER>/bin64/libhypercore.so` | 从 [GitHub Releases](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases/tag/server-v1.3.0) 的 `DST_HyperCore_Server_Linux_v1.3.0_Release.zip` 获取 |
| `dst_hypercore.ini` | `<DST_SERVER>/bin64/dst_hypercore.ini` | 可选模块开关配置文件（与 Windows 通用） |
| `linux_start_with_hypercore.sh` | `<DST_SERVER>/bin64/linux_start_with_hypercore.sh` | 开服启动包装脚本，执行 `chmod +x linux_start_with_hypercore.sh` |
| `hypercore_server` Lua Mod | `<DST_SERVER>/mods/hypercore_server/` | 配套服务端 Mod（亦可通过 Steam 创意工坊订阅并在 `modoverrides.lua` 中启用） |

---

## 3. 四种开服方式的 `LD_PRELOAD` 启动写法

> `libhypercore.so` 内建 `/proc/self/exe` 进程名白名单，**仅**对 `dontstarve_dedicated_server_nullrenderer_x64` 生效，脚本里的 `bash`、`steamcmd` 等辅助进程会自动安全透传。

### 方式 A：直接使用仓库启动脚本（最简单）
```bash
cd <DST_SERVER>/bin64
chmod +x linux_start_with_hypercore.sh
./linux_start_with_hypercore.sh -cluster Cluster_1 -shard Master
```
（如需双开地上与地下，请分别为 `-shard Master` 与 `-shard Caves` 各启动一个实例。）

### 方式 B：在你自己的开服 Shell 脚本里接入
只需在启动 `./dontstarve_dedicated_server_nullrenderer_x64` 的那一行前加上绝对路径 `LD_PRELOAD`：
```bash
cd <DST_SERVER>/bin64
export LD_LIBRARY_PATH="./lib64:${LD_LIBRARY_PATH:-}"
export LD_PRELOAD="<DST_SERVER>/bin64/libhypercore.so"
exec ./dontstarve_dedicated_server_nullrenderer_x64 -cluster Cluster_1 -shard Master
```

### 方式 C：`systemd` 托管服务
在对应 `.service` 文件的 `[Service]` 段中添加环境变量：
```ini
[Service]
WorkingDirectory=<DST_SERVER>/bin64
Environment="LD_LIBRARY_PATH=<DST_SERVER>/bin64/lib64"
Environment="LD_PRELOAD=<DST_SERVER>/bin64/libhypercore.so"
ExecStart=<DST_SERVER>/bin64/dontstarve_dedicated_server_nullrenderer_x64 -cluster Cluster_1 -shard Master
```

### 方式 D：Docker / 翼龙面板 (Pterodactyl)
将 `libhypercore.so` 与 `dst_hypercore.ini` 放入挂载目录 `<DST_SERVER>/bin64/` 后，在面板环境变量中添加：
```text
LD_PRELOAD=/home/container/bin64/libhypercore.so
```
（或在启动命令前缀加上 `LD_PRELOAD=/home/container/bin64/libhypercore.so`。）

---

## 4. 启动验收与从源码交叉编译

- **日志验收**：开服后检查 `<DST_SERVER>/bin64/dst_hypercore_<shard>.log`（例如 `dst_hypercore_Master.log`、`dst_hypercore_Caves.log`；未传 `-shard` 时为 `dst_hypercore_server.log`），看到各模块 `[HOOK] ... -> OK` 及定期 `[PROF]` / `[PROF-SUB]` 输出即表明原生引擎正常接管。
- **控制台体检**：在服务端控制台或管理员聊天框执行 `c_hyperstats()`，即可查看模块命中率与统计摘要。
- **自行从源码编译（Windows/Zig 交叉构建）**：使用 Zig `0.17.1+` 运行 [`../server_hypercore/tools/build_linux.ps1`](../server_hypercore/tools/build_linux.ps1)，将自动基于 `third_party/libdeflate-1.23` 与 `third_party/mimalloc-2.1.7` 编出目标 `x86_64-linux-gnu.2.17` 的 `libhypercore.so`。
