name = "HyperSmooth 240FPS & FastLoad (高刷骨骼补帧与极速加载)"
description = [[
纯客户端 240 帧骨骼动画补帧、全套高刷修复、反 GC 掉帧与全链路加载提速伴侣 / Client-Only 240FPS Skeletal Tweening, Full High-FPS Fixes, Anti-Stutter GC & Fast-Load Suite.

核心功能 / Features (v2.2.1 紧急兼容性与热键重构版):
1. 【全实体真 240 帧骨骼补帧】配合 bin64/dinput8.dll，锁定底层持久化 AnimNode* 追踪全屏所有玩家、怪物、Boss（梦魇疯猪/巨鹿/龙蝇等）、制作/跳跃/攻击、特效层与 UI 三通道 2D 骨骼动画：连续浮点 QPC 亚刻插值 + 2D 极分解防扭曲 + 两段式换图补帧 + Boss 级大位移保护 + ALT_RENDERPATH 自动安全旁路（彻底修复老麦暗影陷阱/囚笼、植物人藤蔓陷阱、月雹等闪退），局内按 F10 一键盲测开关！
2. 【反 GC 卡顿引擎级调度】原生插件接管 Simulation::CollectGarbage：按帧率动态缩放增量 GC 预算（240Hz 下每帧 <=0.35ms 尖峰、总吞吐反升 40%），并将原版 1.5x 突发全量卡顿阈值改为紧急安全兜底，彻底消除打 Boss/大型基地 60~200ms 瞬冻！
3. 【2.5 倍极速解压与加载】原生插件内置 libdeflate v1.23 (运行时按 CPUID 自动调度 AVX-512/AVX2/SSE2) 与硬件 rep stosb 清零修复，进图资源解压实测提速 2.5 倍！
4. 【全套原版高刷 Bug 修复】
   - FollowCamera 60Hz 等效精确指数阻尼（消除高刷镜头漂移）
   - 制作栏/模组/服务器/聊天滚动列表 (TrueScrollList / ChatScrollList) 指数平滑修复（消除 4 倍过快）
   - 小地图缩放 (MapScreen) 帧率无关修复（消除 4 倍过快）
   - 建筑放置辅助圈 (DeployHelper) 时间化修复（消除 240Hz 闪烁）
5. 【240Hz 零冗余 CPU/GC 优化】消除树冠/柱体 DistanceFade 每秒数千次 Vector3 垃圾分配；静止帧无损缓存鼠标拾取 (0ms 移动延迟)；100% 兼容 ShowMe（中文/原版）与 Insight 悬停详情。
6. 【联机上下洞穴零卸载秒切 & 网络等待清空】纯客户端保留地面/洞穴资源常驻内存（自动旁路独行长路与本地主机，退回主菜单自动释放）；缓存未启用模组 modinfo；跳过热门模组榜与 MOTD 阻塞等待。
7. 【0ms 直通内存热键 & 实时帧率 HUD】C++ 原生桥接直连游戏 Lua VM（无视文档/OneDrive/WeGame 路径差异）；F9 循环切帧，F10 实时开关 240 帧骨骼补帧，未装 dinput8.dll 自动提示。

注意 / Note:
100% 纯客户端优化，无需服务器安装，支持所有官方/公共联机房间。单装本 Mod 即可生效全部 Lua 优化；将配套开源 dinput8.dll 放入 bin64/ 目录即可解锁全部 C++ 引擎级特性。
]]
author = "Hakimi Tech Lab"
version = "2.2.1"

api_version = 10
dst_compatible = true
dont_starve_compatible = false
reign_of_giants_compatible = false
shipwrecked_compatible = false

client_only_mod = true
all_clients_require_mod = false

icon_atlas = "modicon.xml"
icon = "modicon.tex"

configuration_options = {
    {
        name = "target_fps",
        label = "目标帧率 / Target FPS",
        hover = "需配合 bin64/dinput8.dll 伴侣插件突破官方 60 帧上限，局内可随时按 F9 切换",
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
        hover = "需配合 bin64/dinput8.dll：对主世界/特效/UI 全部 2D 骨骼动画执行亚刻矩阵插值，局内可按 F10 实时对比开关",
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
        hover = "消除树冠 DistanceFade 每秒数千次临时表分配、静止帧无损缓存鼠标拾取(0延迟)、悬停文本脏检查防重排",
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
        hover = "在屏幕右上角显示轻量实时 FPS 与帧生成时间 (ms)",
        options = {
            { description = "显示", data = true },
            { description = "隐藏", data = false },
        },
        default = true,
    },
    {
        name = "hotkey_cycle",
        label = "局内切帧热键 / Cycle FPS Hotkey",
        hover = "局内一键循环切换 60 -> 120 -> 144 -> 240 -> 无限制，方便对比测试",
        options = {
            { description = "F9 (推荐)", data = "KEY_F9" },
            { description = "F8", data = "KEY_F8" },
            { description = "F7", data = "KEY_F7" },
            { description = "禁用", data = false },
        },
        default = "KEY_F9",
    },
    {
        name = "hotkey_skeletal",
        label = "骨骼补帧开关热键 / Toggle Skeletal Hotkey",
        hover = "局内一键实时开关 240 帧骨骼动画补帧，支持同屏 A/B 对比（配合 bin64/dinput8.dll）",
        options = {
            { description = "F10 (推荐)", data = "KEY_F10" },
            { description = "F11", data = "KEY_F11" },
            { description = "禁用", data = false },
        },
        default = "KEY_F10",
    },
}
