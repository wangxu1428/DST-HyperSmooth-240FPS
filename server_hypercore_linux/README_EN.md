<p align="right">
  <a href="README.md">简体中文</a> | <strong>English</strong>
</p>

# DST HyperCore Server — Linux x86_64 Deployment Guide (`v1.6.0-beta1`)

> Native `LD_PRELOAD` C++ optimization engine for **Don't Starve Together Linux x86_64 Dedicated Server** (`dontstarve_dedicated_server_nullrenderer_x64`, Build `762172`).  
> Server Overview: [`../README_EN.md`](../README_EN.md) | Source: [`../server_hypercore/native_plugin/linux/`](../server_hypercore/native_plugin/linux/hypercore_linux.cpp) | [Releases](https://github.com/wangxu1428/DST-HyperSmooth-240FPS/releases)

---

## 1. Included Modules & Verification Scope

- **Active Native Modules**:
  - **Module 0A / 0B**: Microsecond `[PROF]` tick telemetry and `[PROF-SUB]` breakdown (`luaUpdate` / `netUpdate` / `repUpdate`).
  - **Module 1**: Hermetic lockless `mimalloc v2.1.7` (`MI_OVERRIDE=0`) Lua VM allocator + `HyperCore_*` Lua C-API globals.
  - **Module 2**: Adaptive incremental GC + Anti-STW Full-GC shield.
  - **Module 3**: 4-site in-place W^X `SpatialGrid::Query` zero-copy lookup patches.
  - **Module 4**: SIMD `libdeflate v1.23` compression + `FastDataDumper` + `FastJson` + `FastLoad` (`unzReadCurrentFile` / `inflate_fast`).
  - **Module 6**: `O(N log N)` flat-hash + binary-heap `FastAStar` pathfinding (shared cross-platform headers).
  - **Module 7**: POSIX read-only mod/asset negative stat cache (`PosixFsNegCache` across `stat` / `open` / `access`).
  - **Module 8**: Bounded 64MB strict-FIFO async atomic save-commit queue (`PosixAsyncSaveQueue`: `.hc_tmp` + `fsync` + `rename` with read/stat/dir/exit barriers).
  - **v1.6.0-beta1 Sync**: Module 10B Zero-Desync Tiered Replica Pacing, `O(N)` `ReplicaFastPath`, Causal 3-Channel RPC QoS, Server Hardware Tier (`c_hypertier`), and `SpikeBlackbox`.
- **Platform Notes**: Built targeting **`GLIBC 2.17`** with zero `libstdc++.so.6` / `libgcc_s.so.1` dependency. Keeps vanilla **`30Hz` SimTick** on Linux.
- **Verification Scope Disclosure**: Cross-compiled cleanly (0 warnings, `724,608 B`) via Zig `0.17.1` (`-target x86_64-linux-gnu.2.17`) and verified statically + cross-ELF Capstone against the real Linux Build `762172` ELF via [`test_linux_elf_verify.py`](../server_hypercore/tools/test_linux_elf_verify.py) (`GLIBC <= 2.17`, strict 19-symbol `.dynsym` whitelist, `66/66` 1-of-1 AOBs, `22/22` function boundaries, `4/4` SpatialGrid patch sites, pure LF `.sh`). **It has NOT yet been live-executed on a real Linux host** — community live-Linux testing reports are very welcome!

---

## 2. Exact File Placement (Do Not Put `.so` in `bin/` or `mods/`)

Assuming `<DST_SERVER>` is your Linux server root directory (containing both 32-bit `bin/` and 64-bit `bin64/`):

| File | Target Path on Linux Server | Notes |
| :--- | :--- | :--- |
| `libhypercore.so` | `<DST_SERVER>/bin64/libhypercore.so` | Included in `DST_HyperCore_Server_Linux_v1.6.0-beta1_Release.zip` |
| `dst_hypercore.ini` | `<DST_SERVER>/bin64/dst_hypercore.ini` | Optional module configuration (next to the 64-bit binary) |
| `linux_start_with_hypercore.sh` | `<DST_SERVER>/bin64/linux_start_with_hypercore.sh` | Wrapper launcher; run `chmod +x linux_start_with_hypercore.sh` |
| `hypercore_server` Lua Mod | `<DST_SERVER>/mods/hypercore_server/` | Companion server mod (or enable via Steam Workshop in `modoverrides.lua`) |

---

## 3. Launch Methods (`LD_PRELOAD`)

> `libhypercore.so` checks `/proc/self/exe` and activates **only** inside `dontstarve_dedicated_server_nullrenderer_x64`, passing through helper tools (`bash`, `steamcmd`, etc.) inertly.

### Option A: Bundled Wrapper Script (Recommended)
```bash
cd <DST_SERVER>/bin64
chmod +x linux_start_with_hypercore.sh
./linux_start_with_hypercore.sh -cluster Cluster_1 -shard Master
```

### Option B: Existing Custom Bash Script
```bash
cd <DST_SERVER>/bin64
export LD_LIBRARY_PATH="./lib64:${LD_LIBRARY_PATH:-}"
export LD_PRELOAD="<DST_SERVER>/bin64/libhypercore.so"
exec ./dontstarve_dedicated_server_nullrenderer_x64 -cluster Cluster_1 -shard Master
```

### Option C: `systemd` Service Unit
```ini
[Service]
WorkingDirectory=<DST_SERVER>/bin64
Environment="LD_LIBRARY_PATH=<DST_SERVER>/bin64/lib64"
Environment="LD_PRELOAD=<DST_SERVER>/bin64/libhypercore.so"
ExecStart=<DST_SERVER>/bin64/dontstarve_dedicated_server_nullrenderer_x64 -cluster Cluster_1 -shard Master
```

### Option D: Docker / Pterodactyl Panel
Place `libhypercore.so` and `dst_hypercore.ini` into `<DST_SERVER>/bin64/` and add the environment variable (or startup command prefix):
```text
LD_PRELOAD=/home/container/bin64/libhypercore.so
```

---

## 4. Verification & Building From Source

- **Log Check**: Inspect `<DST_SERVER>/bin64/dst_hypercore_<shard>.log` (e.g. `dst_hypercore_Master.log`, `dst_hypercore_Caves.log`, or `dst_hypercore_server.log` if `-shard` is omitted) for `[HOOK] ... -> OK` and periodic `[PROF]` / `[PROF-SUB]` lines.
- **Console Check**: Run `c_hyperstats()` in the server console for live telemetry.
- **Cross-Build From Source**: Run [`../server_hypercore/tools/build_linux.ps1`](../server_hypercore/tools/build_linux.ps1) with Zig `0.17.1+` (`third_party/libdeflate-1.23` + `third_party/mimalloc-2.1.7` -> `x86_64-linux-gnu.2.17`).
