name = "HyperSmooth 240FPS & FastLoad (高刷稳帧骨骼补帧与极速加载)"
description = [[
纯客户端 240 帧骨骼补帧、零争抢 RTSS 级 1% Low 稳帧引擎、反 GC 掉帧、mimalloc 无锁 Lua 堆与可选双端 60Hz 逻辑刻 / Client-Only 240FPS Skeletal Tweening, Zero-Contention RTSS Frame Pacer (1% Low Opt), Anti-Stutter GC, mimalloc Lua Heap & Auto-Negotiated 60Hz SimTick.

核心功能 / Features (v2.4.0 1% Low 稳帧 · 无延迟补偿顺滑 · 并发防卡顿 & 双端高刷版):
1. 【关延迟补偿 240Hz 丝滑位移动画 + 零等待操控】解决关闭延迟补偿后人物/怪物走位锯齿抖动！底层三通道 240Hz 连续骨骼与世界坐标插值（含 5× 提速 O(N) 栈哈希骨骼匹配、极分解与瞬移熔断保护），搭配 240Hz WASD 即时发包（零重复 RPC）与 RakNet 事件驱动零停顿唤醒（消除 0~10ms 发包空等），关闭延迟补偿既有绝对精准判定，又如丝般顺滑！
2. 【Boss 并发防卡顿全链路五件套】专为天体英雄三阶段陨石、龙蝇岩浆虫、蜂后蜜蜂群、织影者骨牢/小虫等高并发战斗打造：① 5 条客户端 SpatialGrid::Query 全路径零拷贝（含 FindEntities 与 LightWatcher，零 malloc）；② RenderThread 异步纹理上传 1.2ms/帧时间片限流（读图自动全速、局内不瞬冻）；③ 并发平滑 GC（彻底消灭帧率小幅波动时的锯齿尖峰）；④ 2 级并发骨骼负载保护（永不断掉 240Hz 世界位移插值）；⑤ 无锁后台卡顿归因遥测。
3. 【零争抢 RTSS 级帧时间控制器 + mimalloc v2.1.7 无锁 Lua 堆】Win10/11 高精度内核等待计时器 (`CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) + 自适应唤醒余量 EMA (`[500, 1950] µs` 防 Win10 过冲) + 绝对截止期调度，约 85%~90% 等待保持 0% CPU 占用；内置独立静态 mimalloc v2.1.7 取代原版全局互斥锁 Lua 堆分配器，显著拉升 1% Low！
4. 【实时 1% Low / P99 帧时间 HUD + 原生 ESC->设置->画面 接管】C++ 底层 512 帧无锁环形采样直通右上角 HUD 实时显示 `FPS | 1%L (P99 ms)`；直接激活克雷原版「画面 -> 刷新率」（全屏/窗口解锁 60~360 FPS 与无限制），并在右列同风格原生集成「240帧骨骼补帧」与「帧率显示 HUD」；热键默认禁用（零冲突），支持 56 键 + 修饰键定制。
5. 【双端真 60Hz 逻辑刻自动协商开关】所有官方/公共联机服默认 100% 保持原版 30Hz 零副作用（每次返回主界面/换服底层强制归零 30Hz）；仅在连接开启 60Hz 的 HyperCore Server v1.2.0 服务端时自动握手切至双端 60Hz（同步解锁客户端 D=0 无节流收包与 4/4 子系统时钟）。
6. 【2.5 倍极速加载 + 全套高刷修复】libdeflate v1.23 (AVX-512/AVX2/SSE2)、硬件 `rep stosb` 清零修复、FollowCamera/滚轮/地图/放置圈高刷修复、零分配 DistanceFade、静止鼠标拾取缓存（100% 兼容 ShowMe/Insight）与上下洞穴零卸载秒切。

注意 / Note:
100% 纯客户端优化，支持所有官方/公共联机房间；双端 60Hz 仅在服务端同步安装并开启 HyperCore Server v1.2.0 的 60Hz 选项时自动生效。单装本 Mod 即可生效全部 Lua 优化；将配套开源 dinput8.dll 放入 bin64/ 目录即可解锁全部 C++ 引擎级特性。
]]
author = "Hakimi Tech Lab"
version = "2.4.0"

api_version = 10
dst_compatible = true
dont_starve_compatible = false
reign_of_giants_compatible = false
shipwrecked_compatible = false

client_only_mod = true
all_clients_require_mod = false

icon_atlas = "modicon.xml"
icon = "modicon.tex"

-- NOTE: Klei executes modinfo.lua in a bare sandbox (no ipairs/pairs/string/table globals).
-- Use pure numeric `for i = 1, #t` loops and literal tables only!
local function BuildHotkeyOptions()
    local opts = {
        { description = "禁用 (推荐，零冲突)", data = false },
    }
    for i = 1, 12 do
        opts[#opts + 1] = { description = "F" .. i, data = "KEY_F" .. i }
    end
    local extras = {
        { "Insert", "KEY_INSERT" },
        { "Home", "KEY_HOME" },
        { "PageUp", "KEY_PAGEUP" },
        { "PageDown", "KEY_PAGEDOWN" },
        { "End", "KEY_END" },
        { "Pause", "KEY_PAUSE" },
        { "ScrollLock", "KEY_SCROLLOCK" },
    }
    for i = 1, #extras do
        local e = extras[i]
        opts[#opts + 1] = { description = e[1], data = e[2] }
    end
    for i = 0, 9 do
        opts[#opts + 1] = { description = "小键盘 " .. i, data = "KEY_KP_" .. i }
    end
    local letters = {
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    }
    for i = 1, #letters do
        local ch = letters[i]
        opts[#opts + 1] = { description = ch, data = "KEY_" .. ch }
    end
    return opts
end

configuration_options = {
    {
        name = "target_fps",
        label = "默认目标帧率 / Default FPS",
        hover = "需配合 bin64/dinput8.dll 突破官方 60 帧上限；进游戏后也可直接在 ESC -> 设置 -> 画面 -> 刷新率 随时调整",
        options = {
            { description = "60 FPS (官方原版)", data = 60 },
            { description = "120 FPS", data = 120 },
            { description = "144 FPS", data = 144 },
            { description = "165 FPS", data = 165 },
            { description = "240 FPS (推荐)", data = 240 },
            { description = "360 FPS", data = 360 },
            { description = "无限制 (Unlimited)", data = 0 },
        },
        default = 240,
    },
    {
        name = "skeletal_tween",
        label = "真 240 帧骨骼补帧 / 240fps Skeletal Tween",
        hover = "需配合 bin64/dinput8.dll：对主世界/特效/UI 全部 2D 骨骼动画执行亚刻矩阵插值，也可在 ESC -> 设置 -> 画面 随时开关",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用 (原版 30Hz)", data = false },
        },
        default = true,
    },
    {
        name = "smooth_camera",
        label = "高刷镜头阻尼修复 / Smooth Camera",
        hover = "将 FollowCamera 线性插值升级为 60Hz 等效精确指数阻尼，消除高刷手感偏移",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用 (原版)", data = false },
        },
        default = true,
    },
    {
        name = "smooth_ui_240hz",
        label = "高刷列表/地图/放置圈修复 / 240Hz UI Fixes",
        hover = "修复高刷下制作栏/聊天滚轮 4 倍过快、小地图缩放 4 倍过快、以及灭火器/避雷针等放置圈闪烁 Bug",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用 (原版)", data = false },
        },
        default = true,
    },
    {
        name = "reduce_240hz_churn",
        label = "高刷零冗余 CPU/GC 减负 / Zero-Churn Opt",
        hover = "消除树冠 DistanceFade 每秒数千次临时表分配与静止帧冗余鼠标拾取(0延迟)，100% 兼容 ShowMe / Insight",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "keep_shard_assets",
        label = "上下洞穴零卸载秒切 / Fast Cave Switch",
        hover = "纯客户端保留地面/洞穴原版资源常驻内存，联机反复上下洞穴跳过重加载（需 >=8GB 内存，退回主菜单自动释放）",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "fast_mod_scan",
        label = "未启用模组缓存免重扫 / Fast Mod Scan",
        hover = "切界面时直接复用内存中已解析的未启用模组信息，跳过重复读盘，仍保留全部模组可见可开关",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "skip_top_mods_query",
        label = "跳过热门模组榜请求 / Skip Top Mods",
        hover = "打开模组/建房界面时不再向 Steam 拉取热门模组推荐列表，省去 3~13 秒网络等待",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "skip_motd_delay",
        label = "跳过公告网络等待 / Skip MOTD Wait",
        hover = "主界面直接从本地缓存完成 MOTD 加载，不再白等远程海报请求（不影响联机登录）",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "show_fps_hud",
        label = "实时帧率 HUD / FPS Overlay",
        hover = "在屏幕右上角显示轻量实时 FPS、1% Low 与帧时间 (ms)，也可在 ESC -> 设置 -> 画面 随时开关",
        options = {
            { description = "显示", data = true },
            { description = "隐藏", data = false },
        },
        default = true,
    },
    {
        name = "show_1pct_low",
        label = "HUD 显示 1% Low 稳帧指标 / Show 1% Low",
        hover = "需配合 bin64/dinput8.dll v2.4.0+：在右上角 HUD 同步展示 C++ 底层实测的 1% Low 帧率与 P99 帧时间 (ms)",
        options = {
            { description = "显示 (推荐)", data = true },
            { description = "仅显示平均帧率", data = false },
        },
        default = true,
    },
    {
        name = "auto_60hz_sim",
        label = "[双端高刷] 自动协商 60Hz 逻辑刻 / Auto 60Hz Sim",
        hover = "纯被动安全开关：连接官方/公共服时 100% 保持原版 30Hz 零改动；仅在连接开启 60Hz 的 HyperCore 服务端时自动切换双端 60Hz",
        options = {
            { description = "自动协商 (推荐)", data = true },
            { description = "始终锁定 30Hz 原版", data = false },
        },
        default = true,
    },
    {
        name = "hotkey_modifier",
        label = "自定义热键组合前缀 / Hotkey Modifier",
        hover = "启用下方自定义热键时，要求同时按住的修饰键（默认 Alt+ 防止与重度 Mod 单键冲突）",
        options = {
            { description = "Alt + 按键 (推荐防冲突)", data = "ALT" },
            { description = "Ctrl + 按键", data = "CTRL" },
            { description = "Shift + 按键", data = "SHIFT" },
            { description = "无 (仅按单键)", data = "NONE" },
        },
        default = "ALT",
    },
    {
        name = "hotkey_cycle",
        label = "局内切帧热键 (可选) / Cycle FPS Hotkey",
        hover = "默认禁用避免多 Mod 冲突（直接按 ESC -> 设置 -> 画面 -> 刷新率 即可调帧）；极客玩家可任选全键盘按键",
        options = BuildHotkeyOptions(),
        default = false,
    },
    {
        name = "hotkey_skeletal",
        label = "骨骼补帧热键 (可选) / Toggle Skeletal Hotkey",
        hover = "默认禁用避免多 Mod 冲突（直接按 ESC -> 设置 -> 画面 即可开关）；极客玩家可任选全键盘按键用于同屏 A/B 盲测",
        options = BuildHotkeyOptions(),
        default = false,
    },
}
