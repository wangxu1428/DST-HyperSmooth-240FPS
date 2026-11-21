name = "HyperCore Server (服务端深度性能优化引擎)"
description = [[
《饥荒联机版》纯服务端零玩法分歧深度性能优化套件（v1.0.0）。
单装本 Mod 即可生效全部 Lua 级手术优化；配合开源原生引擎插件 bin64/iphlpapi.dll（可前往 GitHub Release 下载）更可解锁无锁 mimalloc 内存池、空间网格零拷贝、压力驱动 30Hz 自适应 GC、C++ 原生极速存档序列化与 SIMD 压缩！

【核心特性 · 零分歧默认开启】
1. 鱼人村 RetargetFn 无过滤全扫 Bug 手术修复：保留 100% 原版战斗逻辑，前置标签过滤，彻底消除鱼人王 / 沃特鱼人军团对树木、箱子、冬季盛宴糖果的无效全扫卡顿。
2. 实体群体错峰苏醒调度：平滑分散大基地加载瞬间的集中唤醒尖峰。
3. 安全白名单惰性腐烂积分：对休眠地面与关闭容器内的普通易腐食物启用精确温湿度积分（自动跳过全部自定义 onperishfn 物品、热食、光源与温控场），ShowMe/Insight 查询实时精确，存档绝不写入 paused=true，随时卸载 100% 恢复原版！
4. C++ 原生极速存档桥接：配合 bin64/iphlpapi.dll 时自动切换 C++ FastDataDumper（152 MB/s，实测存档主线程冻结降低 44%），异常自动回退原版，100% 原版存档格式零锁死。
5. 控制台性能透视：服务端控制台输入 c_hyperstats() 一键查看 Tick 均值/P95/P99、GC、空间检索与存档耗时。

【可选治理策略 · 默认关闭，按需开启】
- 休眠地面杂物安全自动堆叠：仅合并远离玩家且持续休眠的同种基础可堆叠杂物，永不删除任何物品。
- 野生牛群生态软承载上限：超过上限后仅暂停野牛自然繁殖，永不删除已有牛群或驯化牛。

注意：100% 纯服务端 Mod，客户端无需下载；随时取消勾选即可无损退回原版，零存档残留！
]]
author = "Hakimi Tech Lab"
version = "1.0.0"
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

configuration_options = {
    {
        name = "opt_fast_dumper",
        label = "[零分歧] C++ 极速存档序列化桥接",
        hover = "配合 bin64/iphlpapi.dll 启用 C++ 原生 FastDataDumper 加速每日存档（100% 原版存档格式，异常自动回退原版）",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "opt_merm_retarget",
        label = "[零分歧] 鱼人村索敌无过滤风暴修复",
        hover = "修复原版 merm.lua 未传标签导致鱼人王/沃特鱼人军团每秒全扫地上杂物与树木的 Bug（战斗逻辑 100% 一致）",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "opt_staggered_wake",
        label = "[零分歧] 密集生物错峰苏醒调度",
        hover = "将加载瞬间同时苏醒的密集生物大脑分散在 0.0~0.5 秒内平滑启动，消除瞬间卡顿尖峰",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "opt_lazy_perishable",
        label = "[零分歧] 白名单惰性腐烂精确积分",
        hover = "对休眠与关闭容器内的安全白名单易腐食物按温湿度精确积分（跳过特殊腐烂物品；绝不锁死存档，卸载零残留）",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "opt_offline_autostack",
        label = "[可选治理] 休眠地面杂物安全堆叠",
        hover = "仅在远离玩家且持续休眠后，自动合并地面同种基础散落物（木头/石头/草/树枝等），永不删除任何物品",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "开启 (仅安全合并)", data = true },
        },
        default = false,
    },
    {
        name = "opt_wild_beefalo_cap",
        label = "[可选治理] 野生牛群生态软上限",
        hover = "全图野生牛群达到上限后仅暂停自然发情繁殖，绝不删除已有牛群，也不影响玩家驯化牛",
        options = {
            { description = "关闭 (原版默认)", data = 0 },
            { description = "40 头野牛", data = 40 },
            { description = "60 头野牛", data = 60 },
            { description = "100 头野牛", data = 100 },
        },
        default = 0,
    },
}
