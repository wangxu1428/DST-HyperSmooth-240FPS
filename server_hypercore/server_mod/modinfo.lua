name = "HyperCore Server (服务端深度性能优化引擎)"
description = [[
《饥荒联机版》纯服务端零玩法分歧深度性能优化套件（v1.3.0 巨型基地全链路提速版 · 5/5 空间网格零拷贝 & O(N log N) 原生寻路引擎）。
单装本 Mod 即可生效全部 Lua 级手术优化；配合开源原生引擎插件 bin64/iphlpapi.dll（可前往 GitHub Release 下载）更可解锁无锁 mimalloc 内存池、5/5 空间网格零拷贝、O(N log N) 极速寻路引擎、压力驱动自适应 GC、C++ 原生极速存档序列化与 SIMD 压缩！

【v1.3.0 新特性 · 巨型基地全链路引擎升级（零玩法分歧 · 零存档变更）】
- 5/5 全覆盖 SpatialGrid::Query 零拷贝加速：在原有 FindEntities 缓存标签路径基础上，完整补齐其余 4 处高频只读空间查询（FindEntities_DirectTag、FindEntities_Registered、LightWatcher 光照强度与 RGB 查询），彻底消除巨型基地每帧数万次 MSVCR90 malloc + memcpy + free 堆复制风暴！
- O(N log N) 原生 FastAStar 寻路引擎（bin64/iphlpapi.dll 默认启用）：将原版 PathFinder 每步全表 std::sort + 双链表线性扫 + 逐节点 new/delete（O(S^2 log S)）替换为平坦开放寻址哈希表 + 二叉最小堆 + 连续内存池，100% 复用原版邻居/地形/围墙/船只/道路判定函数与启发函数，巨型围墙基地与猎犬/Boss 绕障寻路大幅提速且路径结果与原版等价！
- 控制台 c_hyperstats() 同步升级：实时展示 SpatialGrid 实体/光照查询总数、FastAStar 寻路次数/步数/总耗时/回退次数。

【v1.2.1 P0 存档安全热修】
- 彻底根治旧版 v1.2.0 开启「白名单惰性腐烂精确积分 (opt_lazy_perishable)」时，在存档保存阶段对已过期休眠食物写入负值 perishremainingtime 导致重载世界时在 POPULATING 期中途销毁实体并触发 inventoryitem_replica.lua:225 (classified 为 nil) 专服无法启动的严重隐患！
- 全面改为 100% 零副作用只读 GetPercent() 与零副作用 OnSave()（过期实体统一以安全正值 0.01 落盘，真正腐烂推迟至存盘后异步结算），为全部休眠/唤醒/容器事件加装 GLOBAL.POPULATING 防护锁，并在 Perishable:OnLoad 内置向后兼容自愈——无需手动修改存档文件，更新本版即可直接无损救活受 v1.2.0 影响的旧存档！

【v1.2.0 新特性 · 双端自协商真 60Hz 逻辑刻 & 0ms 零停顿网络发包】
- 新增可选开关「60Hz 极速逻辑刻模式」（默认关闭保持 100% 纯原版 30Hz 兼容，客户端无需安装任何文件）；
- 服主开启后（需配套 bin64/iphlpapi.dll），服务端逻辑判定、物理步进与 4/4 子系统时钟全面升级至真 60Hz（16.7ms/刻，保持 1.000x 原版真实游戏倍速），自动同步网络快照频率至 U=60(1)（尊重 cluster.ini 手动配置）、启用 120Hz 高精度内核稳帧消除 0/2 拍频抖动，并自动通过世界网络标签/RPC 握手通知 HyperSmooth v2.4.0+ 客户端无缝切入 60Hz 协同模式！
- 原生插件内置 RakNet 事件驱动零停顿唤醒（30Hz/60Hz 均默认生效），彻底消除服务端状态快照在 WaitOnEvent(10) 中白等 0~10ms 的网络空转（零轮询、0% 空闲 CPU）！

【v1.1.0+ 架构与安全加固】
- FastDataDumper 防环检测 + 256MB 安全上限 + 区域无关浮点格式化，彻底免疫异常 Mod 嵌套环表与逗号小数区域环境。
- SpatialGrid 无限大/NaN 半径原版等价回退 + 独立 AOB Fail-Closed 校验 + W^X 跳板只读固化 + Windows x64 SEH 栈展开表完备。
- 多分片（Master / Caves）独立日志隔离 + 端口独占防劫持 + 鱼人索敌保留第三方 Mod 兼容 + 野牛软上限精确排除玩家驯化与带鞍牛。

【核心特性 · 零分歧默认开启】
1. 鱼人村 RetargetFn 无过滤全扫 Bug 手术修复：保留 100% 原版战斗逻辑与第三方 Mod 兼容，前置标签过滤，彻底消除鱼人王 / 沃特鱼人军团对树木、箱子、冬季盛宴糖果的无效全扫卡顿。
2. 实体群体错峰苏醒调度：平滑分散大基地加载瞬间的集中唤醒尖峰。
3. 安全白名单惰性腐烂积分：对休眠地面与关闭容器内的普通易腐食物启用精确温湿度积分（自动跳过全部自定义 onperishfn 物品、热食、光源与温控场），ShowMe/Insight 查询实时精确，存档绝不写入 paused=true，随时卸载 100% 恢复原版！
4. C++ 原生极速存档桥接：配合 bin64/iphlpapi.dll 时自动切换 C++ FastDataDumper（155 MB/s，实测存档主线程冻结降低 46%），异常自动回退原版，100% 原版存档格式零锁死。
5. 控制台性能透视：服务端控制台输入 c_hyperstats() 一键查看 Tick 均值/P95/P99、GC、空间检索与存档耗时。

【可选治理策略 · 默认关闭，按需开启】
- 休眠地面杂物安全自动堆叠：仅合并远离玩家且持续休眠的同种基础可堆叠杂物，永不删除任何物品。
- 野生牛群生态软承载上限：超过上限后仅暂停野牛自然繁殖，永不删除已有牛群，自动排除已驯化/正在驯化/带鞍的玩家牛。

注意：默认 30Hz 模式下 100% 纯服务端生效，客户端无需下载；随时取消勾选即可无损退回原版，零存档残留！
]]
author = "Hakimi Tech Lab"
version = "1.3.0.1"
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
    {
        name = "opt_sim_60hz",
        label = "[实验 · 双端高刷] 60Hz 真逻辑刻模式",
        hover = "需配套 bin64/iphlpapi.dll（自动同步 U=60(1) 网络快照，无需手改 cluster.ini）。开启后逻辑刻由 30Hz 翻倍至 60Hz 并自动与 HyperSmooth v2.4.0+ 客户端握手生效（普通玩家服请保持关闭）",
        options = {
            { description = "关闭 (原版 30Hz · 推荐)", data = false },
            { description = "开启 (60Hz 双端高刷实验模式)", data = true },
        },
        default = false,
    },
}
