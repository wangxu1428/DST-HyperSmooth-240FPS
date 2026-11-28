<p align="right">
  <a href="README.md">简体中文</a> | <strong>English</strong>
</p>

# DST HyperCore Server v1.6.0-beta2 — Windows & Linux Dedicated Server Native Engine

> **Ceiling-Tier Server-Only Native C++ Engine (`bin64/iphlpapi.dll` / `bin64/libhypercore.so`) + Zero-Divergence Mod for Don't Starve Together**  
> Dedicated Linux x86_64 directory 👉 [`server_hypercore_linux/`](server_hypercore_linux/README_EN.md) ([中文指南](server_hypercore_linux/README.md)) | Client Repo: [`main`](https://github.com/wangxu1428/DST-HyperSmooth-240FPS) | [Releases](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases)
>
> - **100% Server-Side at Default 30Hz**: Zero client downloads required; vanilla players join normally.
> - **Cross-Platform (Windows + Linux x86_64)**: Lockless `iphlpapi.dll` proxy on Windows, plus a native `LD_PRELOAD` shared library `libhypercore.so` on Linux x86_64 (`glibc >= 2.17`, see [`server_hypercore_linux/`](server_hypercore_linux/README_EN.md)).
> - **Opt-In Dual-Side 60Hz SimTick (Windows)**: Broadcasts handshake to `DST HyperSmooth v2.4.0+` clients at exact **`1.000×` vanilla game speed**, supporting live lossless `30Hz ⇄ 60Hz ⇄ 30Hz` switching (Linux remains at vanilla `30Hz` to respect shared `.rodata` float constants).
> - **Zero Save Lock-In**: Remove the plugin and mod at any time — including saves created at 60Hz — and reload cleanly on 100% vanilla 30Hz servers.

---

## What's New in v1.6.0-beta2

- **beta2 Compatibility Hardening**: `opt_lazy_perishable` now defaults to Disabled (and auto-yields to custom/anti-spoil containers when enabled); Windows & Linux native engines detect foreign LuaJIT2 and safely yield native GC hooks; added English `OneClick_Install_Update.bat`.

### 0. Module 10B Zero-Desync Tiered Replica Pacing + `O(N)` ReplicaFastPath + Causal 3-Channel RPC QoS + Hardware Tier & Spike Blackbox
- **Module 10B Zero-Desync Tiered Replica Construction Pacing**: Spreads 1,500+ entity mega-base and wormhole entry bursts across smooth ticks (nearest-first distance buckets) while bypassing Tier-0 objects (proxies, players, classifieds, parented/inventory items, and `<=14wu` threats) with automatic Parent-Before-Child promotion; runtime tuning via `c_hyperpacing()`.
- **`O(N)` `ReplicaFastPath` & Causal 3-Channel `RakNet::RPC4` QoS** ([replica_fastpath.h](server_hypercore/native_plugin/replica_fastpath.h) / [rpc_qos.h](server_hypercore/native_plugin/rpc_qos.h)): Eliminates redundant replica RB-tree scans and keeps combat/sync RPCs prioritized on Channel 0.
- **Server Hardware Tiers + `C++ FastJson` + Server Spike Blackbox** ([server_hardware_tier.h](server_hypercore/native_plugin/server_hardware_tier.h) / [fast_json.h](server_hypercore/native_plugin/fast_json.h) / [spike_blackbox.h](server_hypercore/native_plugin/spike_blackbox.h)): Adds `c_hypertier()`, native JSON codec, spike snapshot JSON export, and `dnh_github` metadata in [modinfo.lua](server_hypercore/server_mod/modinfo.lua).

### 1. Async Atomic Save-Commit Queue (Module 8 · `enable_async_save = 1`) & Sub-Phase Profiler
- **Bounded 64MB Strict-FIFO Offload** ([async_save_commit.h](server_hypercore/native_plugin/async_save_commit.h)): Intercepts `WritePersistentFile` and moves blocking disk writes to a dedicated worker thread (`enqUs ~ 76µs` on the main sim thread; observed `enq=11 ok=11 err=0` in live server tests).
- **Strict Causal Read/Delete/Dir/Exit Barriers**: Wraps Win32 Kernel32 / MSVCR90 (and Linux POSIX) file/stat/directory/exit calls (`CreateFile`, `MoveFile`, `DeleteFile`, `stat`, `opendir`, `exit`, `_exit`, `_cexit`) plus `.hc_tmp` + `fsync` + `rename` atomic commits on Linux so subsequent reads or shutdowns always flush in order.
- **Sub-Phase Telemetry (`[PROF-SUB]`)**: Hooks `Simulation::luaUpdate`, `cNetworkManager::Update`, and `ReplicaManager3::Update` to break down per-tick Lua, network, and replication latencies.

### 2. Read-Only Mod/Data Negative Stat Cache (Module 7 · `enable_fs_neg_cache = 1`)
- Shared cross-platform cache ([fs_neg_cache.h](server_hypercore/native_plugin/fs_neg_cache.h)) eliminates repeated OS stat syscalls for missing mod/asset probe paths, with epoch invalidation on file writes, deletes, renames, or directory operations.

### 3. Native Linux x86_64 `libhypercore.so` Engine ([`server_hypercore_linux/`](server_hypercore_linux/README_EN.md))
- Brings Modules 0A/0B, 1 (`mimalloc v2.1.7`, `MI_OVERRIDE=0`), 2 (Adaptive GC + Anti-STW Shield), 3 (4-site in-place W^X `SpatialGrid` zero-copy), 4 (SIMD `libdeflate v1.23` + `FastDataDumper` + `FastLoad`), 6 (`O(N log N)` `FastAStar`), 7 (`PosixFsNegCache`), and 8 (`PosixAsyncSaveQueue`) to Linux x86_64 dedicated servers.
- Version-script whitelist ([exports.map](server_hypercore/native_plugin/linux/exports.map)) exports strictly `19` POSIX `LD_PRELOAD` interposition symbols, gated via `/proc/self/exe` strictly to `dontstarve_dedicated_server_nullrenderer_x64` (`bash`, `steamcmd`, and other processes pass through inertly).
- **Verification Scope Note**: `libhypercore.so` (`645,600 B`) is cross-compiled with Zig `0.17.1` targeting `x86_64-linux-gnu.2.17` (`GLIBC <= 2.17`, zero `libstdc++` dependency) and statically + Capstone cross-ELF verified against real Linux Build `762172` ELF (`57/57` unique AOBs, `19/19` prologues, `AStar::Step+0x3CC -> 0x13a410`, `4/4` SpatialGrid sites). **It has not yet been live-executed on a real Linux host** — community live-Linux testing feedback is welcome!

### 4. `O(N log N)` `FastAStar`, 2.5× `FastLoad`, Anti-STW GC & R3.1 Hardening
- **`FastAStar` Pathfinding** ([fast_astar.h](server_hypercore/native_plugin/fast_astar.h), [fast_astar_manager.h](server_hypercore/native_plugin/fast_astar_manager.h)): 1,000-step enclosed mega-base benchmark speeds up from **`78.02ms → 2.62ms` (29.7× faster)** with exact vanilla path cost parity, 64-bit ABA-proof pool handles, and 0 UAF under 80,000-op 4-thread stress.
- **2.5× Server `FastLoad`**: SIMD `libdeflate v1.23` `unzReadCurrentFile`, 64-bit `HookInflateFast`, and `rep stosb` vector zero-fill cut startup/cave bundle decompression time significantly.
- **Anti-STW GC Shield & R3.1 Safety**: Safe `outsideLoaderLock=1` init, `opt_lazy_perishable` OFF by default with exact `StopPerishing()`/`StartPerishing()` decay settlement, and lossless `30Hz ⇄ 60Hz ⇄ 30Hz` net-rate restoration.

---

## Three-Layer Architecture

- **Layer 1 · Native Engine (`bin64/iphlpapi.dll` on Windows / `bin64/libhypercore.so` on Linux)**:
  Modules `0` (`[PROF]` + `[PROF-SUB]`), `1` (Hermetic `mimalloc`), `2` (Zero-Copy `SpatialGrid` 4+1 sites), `3` (Adaptive + Anti-STW GC), `4` (`FastDataDumper` + `libdeflate` + `FastLoad`), `5` (Windows opt-in 60Hz + 120Hz pacer + RakNet wakeup), `6` (`FastAStar`), `7` (`FsNeg` stat cache), and `8` (`AsyncSave` atomic commit queue).
- **Layer 2 · Zero-Divergence Mod (`server_hypercore/server_mod/`)**: Merm retarget tag pre-filter, staggered entity wake, C++ `FastDataDumper` bridge, safe opt-in lazy perishable, and `c_hyperstats()`.
- **Layer 3 · Optional Ecosystem Controls (Default OFF)**: Dormant ground item safe stacker (`opt_autostack`) and wild beefalo soft cap (`opt_wild_beefalo_cap`), never deleting items or tamed beefalo.

---

## Verified Benchmarks & Suites (`Build 762172`)

- **26,028-Entity Stress Save Ablation** (`i7-12700KF · 32GB · Windows x64`): Mean tick **`5,334 → 3,631 µs (-31.9%)`**, P99 **`17,728 → 8,620 µs (-51.4%)`**, max spike **`386.0ms → 29.3ms (-92.4%)`**, overruns **`2 → 0`**, Full-GC freezes **`380.4ms → 0ms`**, save hitch **`399.0ms → 215.0ms (-46.1%)`**.
- **Automated Regression Suites**: `test_redteam_server.exe` **22/22 PASS**, `test_proxy_passthrough.py` **ALL PASS** (297 exports, 0 non-server hooks), `test_server_lua_v14.py` **8/8 ALL_PASS (`rc=0`)**, `test_linux_elf_verify.py` **ALL PASS (`rc=0`, static ELF64 + 66/66 Capstone AOB + 22/22 prologue verification)**.

---

## Installation

### Windows Dedicated Server
1. Subscribe to **HyperCore Server** on Steam Workshop (or copy `server_hypercore/server_mod/` into `<DST_SERVER>/mods/hypercore_server/`).
2. Download & extract **`DST_HyperCore_Server_v1.6.0-beta1_Release.zip`**, stop DST, and run **`一键安装与更新.bat`** (or copy `bin64/iphlpapi.dll` + `bin64/dst_hypercore.ini` into `<DST_SERVER>/bin64/`; DNH `dnh_github` companion auto-update for `iphlpapi.dll` is also declared in `modinfo.lua`).

### Linux x86_64 Dedicated Server
- Grab **`DST_HyperCore_Server_Linux_v1.6.0-beta1_Release.zip`**, place `libhypercore.so`, `dst_hypercore.ini`, and `linux_start_with_hypercore.sh` inside `<DST_SERVER>/bin64/` (NOT `bin/` or `mods/`), and launch with `LD_PRELOAD`!
- Full bare-metal, `systemd`, Docker, Pterodactyl & build instructions 👉 **[`server_hypercore_linux/README_EN.md`](server_hypercore_linux/README_EN.md)**.
