local is_zh = (locale == "zh" or locale == "zhr" or locale == "zht")
local function L(zh, en)
    if is_zh then
        return zh
    end
    return en
end

name = L("HyperCore 服务端深度性能优化引擎", "HyperCore Server Performance Engine")
description = L([[
《饥荒联机版》纯服务端零玩法分歧深度性能优化套件（v1.6.0-beta2 测试版 · 零失步分层副本分帧平滑 + 兼容加固）。
单装本 Mod 即可生效全部 Lua 级手术优化（零分配 PushEvent_Internal、HasTags 展开、C++ FastJson 加速与 c_hyperprof() 热点采样）；配合开源原生引擎插件 bin64/iphlpapi.dll（或 Linux x86_64 libhypercore.so）更可解锁无锁 mimalloc 内存池、5/5 空间网格零拷贝、O(N log N) 极速寻路引擎、压力驱动自适应 GC、C++ 原生极速存档序列化、因果 3 通道 RPC QoS、验证原子存盘与巨型基地副本分帧平滑！

【v1.6.0-beta2 测试版热更新】
- 反鲜容器兼容加固：`opt_lazy_perishable`（惰性腐烂）改为默认关闭，且开启时自动检测并退让反鲜大箱子/模组容器。
- LuaJIT2 共存保护：Windows / Linux 双端自动检测外源 LuaJIT2 并安全退让原生 GC 钩子，防止双开启动存档崩溃。
- 包含 v1.6.0-beta1 全部特性（Module 10B 巨型基地副本分帧限流、`O(N)` 副本加速、因果 RPC QoS、硬件分级与 DNH 托管），新增英文一键安装脚本。

【既有核心特性 · 零分歧默认开启】
1. 5/5 SpatialGrid 空间网格全零拷贝 + O(N log N) FastAStar 寻路引擎 + mimalloc 无锁 Lua 堆 + RakNet 0ms 事件唤醒 + 因果 3 通道 RPC QoS + 原子存盘防护。
2. 鱼人村 RetargetFn 无过滤风暴修复 + 生物错峰苏醒 + 可选白名单惰性腐烂 + 60Hz 双端可选协同逻辑刻。
3. 控制台 c_hyperstats() 实时查看 Tick P95/P99、GC、SpatialGrid、FastAStar、FastDump、FastJson、RepFast、FSNeg、AsyncSave 与 RpcQoS 指标。
]], [[
Dedicated & Host-Server Deep Performance Optimization Suite for Don't Starve Together (v1.6.0-beta2 Beta: Zero-Desync Tiered Replica Pacing + Compatibility Hardening).
Install this mod alone for all Lua-level surgical optimizations (zero-alloc PushEvent_Internal pool, unrolled HasTags, FastJson bridge, c_hyperprof() profiler); pair with open-source native companion bin64/iphlpapi.dll (Windows) or libhypercore.so (Linux x86_64) to unlock lockless mimalloc Lua heap, 5/5 SpatialGrid zero-copy, O(N log N) FastAStar pathfinding, pressure-driven adaptive GC, causal 3-channel RPC QoS, verified atomic save commits, and mega-base replica burst pacing!

[v1.6.0-beta2 Beta Hotfix]
- Anti-Spoil Container Compatibility: opt_lazy_perishable now defaults to Disabled for maximum compatibility and auto-yields to custom/anti-spoil containers when enabled.
- Foreign LuaJIT2 Safety Guard: both Windows & Linux native engines detect LuaJIT2 and yield native GC hooks to prevent world-load crashes.
- Includes all v1.6.0-beta1 features (Module 10B Replica Pacing, O(N) ReplicaFastPath, Causal RPC QoS, Hardware Tiers, DNH metadata) plus English OneClick_Install_Update.bat.

[Core Features]
1. 5/5 SpatialGrid zero-copy queries, O(N log N) FastAStar, mimalloc Lua allocator, 0ms RakNet event wakeup, Causal 3-Channel RPC QoS, Verified Atomic Save-Commit.
2. Merm RetargetFn tag pre-filter fix, staggered entity wake, opt-in safe whitelist lazy perishable, optional 60Hz synced SimTick mode.
3. Run c_hyperstats() in server console to inspect Tick P95/P99, GC, SpatialGrid, FastAStar, FastDump, FastJson, RepFast, FSNeg, AsyncSave, and RpcQoS telemetry.
]])
author = "Hakimi Tech Lab"
version = "1.6.0-beta2"
dnh_github = "wangxu1428/DST-HyperSmooth-240FPS"
dnh_github_asset = "iphlpapi.dll"
forumthread = ""
api_version = 10

dst_compatible = true
dont_starve_compatible = false
reign_of_giants_compatible = false
shipwrecked_compatible = false

all_clients_require_mod = false
client_only_mod = false
server_filter_tags = { "optimization", "performance", "hypercore", "优化" }

icon_atlas = "modicon.xml"
icon = "modicon.tex"

-- NOTE: Klei executes modinfo.lua in a bare sandbox (no ipairs/pairs/string/table/tostring globals).
configuration_options = {
    {
        name = "opt_fast_dumper",
        label = L("[零分歧] C++ 极速存档序列化桥接", "[Zero-Div] C++ FastDataDumper Bridge"),
        hover = L("配合原生引擎插件启用 C++ FastDataDumper 加速每日存档（100% 原版格式，异常自动回退原版）",
                  "Accelerates daily save serialization via native FastDataDumper when iphlpapi.dll/libhypercore.so is installed (100% vanilla save format)"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用", "Disabled"), data = false },
        },
        default = true,
    },
    {
        name = "opt_merm_retarget",
        label = L("[零分歧] 鱼人村索敌无过滤风暴修复", "[Zero-Div] Merm Retarget Tag-Filter Fix"),
        hover = L("修复原版 merm.lua 未传标签导致鱼人王/沃特鱼人军团每秒全扫地上杂物与树木的 Bug（战斗逻辑 100% 一致）",
                  "Fixes vanilla merm.lua un-filtered entity scans that scan trees/items on Merm King/Wurt servers (100% identical combat behavior)"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用", "Disabled"), data = false },
        },
        default = true,
    },
    {
        name = "opt_staggered_wake",
        label = L("[零分歧] 密集生物错峰苏醒调度", "[Zero-Div] Staggered Entity Wakeup"),
        hover = L("将加载瞬间同时苏醒的密集生物大脑分散在 0.0~0.5 秒内平滑启动，消除瞬间卡顿尖峰",
                  "Spreads simultaneous brain wakeups across 0.0-0.5s during world/sector load to smooth out tick spikes"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用", "Disabled"), data = false },
        },
        default = true,
    },
    {
        name = "opt_lazy_perishable",
        label = L("[可选激进] 白名单惰性腐烂精确积分", "[Optional] Whitelist Lazy Perishable"),
        hover = L("对休眠且确认原版静态容器内的易腐食物按温湿度积分（若你使用反鲜箱子/模组容器，建议保持关闭；开启时亦会自动检测并退让）",
                  "Pauses vanilla per-tick perish timers on dormant/vanilla-container foods via temperature math (keep Disabled for custom/anti-spoil container mods; also auto-yields when enabled)"),
        options = {
            { description = L("关闭 (兼容优先默认)", "Disabled (Compat Default)"), data = false },
            { description = L("启用 (原版白名单静态容器)", "Enabled (Vanilla Static Containers)"), data = true },
        },
        default = false,
    },
    {
        name = "opt_offline_autostack",
        label = L("[可选治理] 休眠地面杂物安全堆叠", "[Optional] Dormant Ground Auto-Stack"),
        hover = L("仅在远离玩家且持续休眠后，自动合并地面同种基础散落物（木头/石头/草/树枝等），永不删除任何物品",
                  "Safely merges matching basic ground stackables when far from players and dormant; never deletes any item"),
        options = {
            { description = L("关闭 (原版默认)", "Disabled (Vanilla Default)"), data = false },
            { description = L("开启 (仅安全合并)", "Enabled (Safe Merge Only)"), data = true },
        },
        default = false,
    },
    {
        name = "opt_wild_beefalo_cap",
        label = L("[可选治理] 野生牛群生态软上限", "[Optional] Wild Beefalo Soft Cap"),
        hover = L("全图野生牛群达到上限后仅暂停自然发情繁殖，绝不删除已有牛群，也不影响玩家驯化牛",
                  "Pauses natural wild beefalo reproduction once the global wild cap is reached; never deletes existing or domesticated/saddled beefalo"),
        options = {
            { description = L("关闭 (原版默认)", "Disabled (Vanilla Default)"), data = 0 },
            { description = L("40 头野牛", "40 Wild Beefalo"), data = 40 },
            { description = L("60 头野牛", "60 Wild Beefalo"), data = 60 },
            { description = L("100 头野牛", "100 Wild Beefalo"), data = 100 },
        },
        default = 0,
    },
    {
        name = "opt_sim_60hz",
        label = L("[实验 · 双端高刷] 60Hz 真逻辑刻模式", "[Experimental] True 60Hz Server SimTick"),
        hover = L("需配套原生插件（自动同步 U=60(1) 网络快照，无需手改 cluster.ini）。开启后逻辑刻由 30Hz 翻倍至 60Hz 并与 HyperSmooth 客户端协同（普通服请保持关闭）",
                  "Requires native plugin (auto-syncs U=60(1) snapshots). Doubles server simulation clock from 30Hz to true 60Hz and handshakes with HyperSmooth clients (keep OFF for standard servers)"),
        options = {
            { description = L("关闭 (原版 30Hz · 推荐)", "Disabled (Vanilla 30Hz · Recommended)"), data = false },
            { description = L("开启 (60Hz 双端高刷实验模式)", "Enabled (60Hz Synced Experimental)"), data = true },
        },
        default = false,
    },
    {
        name = "hardware_tier",
        label = L("[原生引擎] 服务端硬件性能档位", "[Native] Server Hardware Liberation Tier"),
        hover = L("需配合原生插件 iphlpapi.dll / libhypercore.so：按服务器配置调节内存/GC/存盘/压缩预算，高端解放档还会关闭 Win11 EcoQoS 并为地面与洞穴分别分配独立物理大核",
                  "Requires native plugin (iphlpapi.dll / libhypercore.so): scales mimalloc retention, GC budget, FastDump/AsyncSave limits, Win11 EcoQoS bypass, and Master/Caves physical P-core hints"),
        options = {
            { description = L("0 · 省电低配 (低内存保守)", "0 · Low Spec (Conservative)"), data = 0 },
            { description = L("1 · 均衡标准 (推荐默认)", "1 · Balanced (Recommended)"), data = 1 },
            { description = L("2 · 高端全解放 (大内存高核全开)", "2 · Unleashed (Full Hardware)"), data = 2 },
        },
        default = 1,
    },
    {
        name = "opt_replica_pacing",
        label = L("[原生引擎 · 零分歧] 巨型基地副本分帧平滑", "[Native · Zero-Div] Mega-Base Replica Construction Pacing"),
        hover = L("需配合原生插件：进入巨型基地或跳虫洞时将远端静态建筑分散在多帧平滑下发，消除客户端瞬间卡顿；周围 14 格内威胁/玩家/背包容器/父实体永远 0 延迟直达",
                  "Requires native plugin: spreads mega-base / wormhole entry bursts across smooth ticks (nearest-first) while keeping <=14wu nearby threats, players, classifieds, inventory/parented entities, and needed parents at 0-tick latency"),
        options = {
            { description = L("自动 (跟随硬件档位 · 推荐)", "Auto (Follow Hardware Tier · Recommended)"), data = -1 },
            { description = L("关闭 (原版单帧全发)", "Off (Vanilla Single-Tick Burst)"), data = 0 },
            { description = L("64 实体/刻 (极度平滑)", "64 entities/tick (Ultra Smooth)"), data = 64 },
            { description = L("96 实体/刻 (均衡推荐)", "96 entities/tick (Balanced)"), data = 96 },
            { description = L("128 实体/刻 (高吞吐)", "128 entities/tick (High Throughput)"), data = 128 },
            { description = L("192 实体/刻 (宽松限流)", "192 entities/tick (Relaxed)"), data = 192 },
            { description = L("256 实体/刻 (轻度限流)", "256 entities/tick (Light)"), data = 256 },
        },
        default = -1,
    },
}
