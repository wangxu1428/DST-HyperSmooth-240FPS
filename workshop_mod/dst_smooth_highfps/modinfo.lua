name = "HyperSmooth 240FPS & FastLoad (高刷稳帧骨骼补帧与极速加载)"
description = [[
纯客户端 240 帧骨骼与位移补帧、零争抢 RTSS 级 1% Low 稳帧引擎、Boss 并发防卡顿、全按键自由录制绑定、玩家优先护眼画质精修与可选双端 60Hz 逻辑刻。

【v2.5.0 核心特性 · 玩家优先护眼画质精修（默认原版纯净）】
1. 玩家优先五铁律：默认完全「原版纯净」，不增加任何视觉负担、不强迫开启后处理；「护眼高清 / 柔和绘本 / 自定义」均需玩家主动启用，关闭后 100% 完整恢复原版表现。
2. 7 项独立开关（关泛光同样全效）：支持在模组配置页或局内控制台 c_hsopt() 独立开关 16× 各向异性过滤、全分辨率 1.0× 光照图、零白边轻度保真微锐化与暗夜防色带、类似原版树荫的实体温和接地阴影与防爆白、柔和近岸/雨天湿土微调、护眼防过曝柔光 Bloom（仅游戏开启泛光时生效）与柔和绘本微通透；0 额外世界纹理采样，240FPS 稳态几乎零开销，同时支持 ESC -> 设置 -> 画面 一键同屏切换与 HUD 避让时钟优化。

【v2.4.x 社区热修与底层引擎特性】
3. 关延迟补偿 240Hz 丝滑位移 & 萤火虫防拉线：底层三通道 240Hz 连续骨骼与世界坐标插值，针对萤火虫/孢子/火花等同名多粒子动画采用 FP16 空间最近邻匹配（彻底修复萤火虫光效连成直线）；搭配 240Hz 键盘 WASD 即时发包与 RakNet 零停顿唤醒，关闭延迟补偿既精准又丝滑。
4. Boss 并发防卡顿五件套：专为天体三阶段陨石、龙蝇岩浆虫、蜂后蜜蜂群、织影者小虫打造——5× 提速栈哈希骨骼管线、5 条客户端空间网格全路径零拷贝、RenderThread 异步纹理上传 1.2ms/帧时间片限流、并发平滑避让 GC 与极端并发保护。
5. 全按键自由录制绑定 & 原生画面设置直控：启用本模组并点击「应用」后，Mod 配置页与「ESC -> 设置 -> 控制」页面底部均可点击录制任意键盘按键或鼠标中键/侧键（默认全部禁用零冲突）；同时原生解锁「ESC -> 设置 -> 画面 -> 刷新率」（60~360/无限制）并集成骨骼补帧、HUD 与画质开关。
6. 上下洞穴免重加载加速：跨 SimReset 保活已加载的地面/洞穴地形与角色资产，联机往返地面与洞穴跳过重复解压与重载（退回主菜单自动释放内存；注：首次下洞仍需加载洞穴资源，切层仍需游戏重置脚本与分片重连）。
7. 零争抢 RTSS 稳帧 + mimalloc 无锁 Lua 堆 + 实时 1% Low HUD：高精度内核计时器稳帧，85%~90% 等待保持 0% CPU 占用；内嵌静态 mimalloc v2.1.7 堆，右上角实时展示 FPS | 1%L (P99 ms)。
8. 双端 60Hz 自动协商 & 2.5 倍极速加载：所有公共服默认 100% 保持原版 30Hz（退主界面/换服强制归零），仅在连接开启 60Hz 的 HyperCore Server v1.2.1+ 时自动握手生效；libdeflate v1.23 极速解压 + 全套高刷镜头/滚轮/地图/放置圈修复。

【使用提示】
- 100% 纯客户端生效，支持所有官方/公共联机服；单装本模组即可生效全部 Lua 优化，将配套开源 bin64/dinput8.dll 放入游戏 bin64/ 目录即可解锁全部 C++ 引擎级特性。
- 双显卡笔记本若帧数停在 150 左右，在显卡控制面板切换为「独显直连」即可跑满 240+ FPS。
]]
author = "Hakimi Tech Lab"
version = "2.5.0"

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
keys_placeholder = {
    {
        description = "请先启用模组并应用",
        hover = "请先勾选启用本模组并点击「应用」，之后即可在配置页或「设置 -> 控制」底部直接点击录制任意按键",
        data = "KEY_DISABLED",
    },
}

local keyboard_rows = {
    { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12", "Print", "ScrolLock", "Pause" },
    { "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
    { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M" },
    { "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z" },
    { "Tab", "CapsLock", "LShift", "LCtrl", "LAlt", "Space", "RAlt", "RCtrl", "RShift", "Enter", "Backspace" },
    { "BackQuote", "Minus", "Equals", "LeftBracket", "RightBracket", "Backslash", "Semicolon", "Quote", "Period", "Slash" },
    { "Up", "Down", "Left", "Right", "Insert", "Delete", "Home", "End", "PageUp", "PageDown" },
}
local numpad_keys = { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "Period", "Divide", "Multiply", "Minus", "Plus" }
local mouse_keys = { "\238\132\130", "\238\132\131", "\238\132\132" } -- 鼠标中键、侧键4、侧键5
local key_disabled = { description = "禁用", data = "KEY_DISABLED" }

keys = { key_disabled }
for i = 1, #mouse_keys do
    keys[#keys + 1] = { description = mouse_keys[i], data = mouse_keys[i] }
end
for i = 1, #keyboard_rows do
    local row = keyboard_rows[i]
    for j = 1, #row do
        local k = row[j]
        keys[#keys + 1] = { description = k, data = "KEY_" .. k:upper() }
    end
end
for i = 1, #numpad_keys do
    local k = numpad_keys[i]
    keys[#keys + 1] = { description = "小键盘 " .. k, data = "KEY_KP_" .. k:upper() }
end

configuration_options = {
    {
        name = "target_fps",
        label = "默认目标帧率",
        hover = "需配合 bin64/dinput8.dll 突破官方 60 帧上限；进游戏后也可直接在 ESC -> 设置 -> 画面 -> 刷新率 随时调整",
        options = {
            { description = "60 FPS (原版)", data = 60 },
            { description = "120 FPS", data = 120 },
            { description = "144 FPS", data = 144 },
            { description = "165 FPS", data = 165 },
            { description = "240 FPS (推荐)", data = 240 },
            { description = "360 FPS", data = 360 },
            { description = "无限制", data = 0 },
        },
        default = 240,
    },
    {
        name = "visual_preset",
        label = "[画质增强] 总方案 (默认原版纯净)",
        hover = "玩家优先原则：默认完全原版纯净，不增加任何视觉负担、不强迫开启后处理；可选择快捷档位，或直接保持原版并在下方逐项独立开关。关闭后完整恢复原版表现",
        options = {
            { description = "原版纯净 (默认)", data = 0 },
            { description = "护眼高清 (关泛光全效)", data = 1 },
            { description = "柔和绘本", data = 2 },
            { description = "自定义 (按下方独立开关)", data = 3 },
        },
        default = 0,
    },
    {
        name = "visual_af16",
        label = "[画质分项] 16× 各向异性过滤",
        hover = "独立开关（需 bin64/dinput8.dll）：仅消除斜视角地皮/建筑发糊与闪烁，零偏色零后处理负担（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用 16× AF", data = true },
        },
        default = false,
    },
    {
        name = "visual_full_lightmap",
        label = "[画质分项] 全分辨率 1.0× 光照图",
        hover = "独立开关（需 bin64/dinput8.dll）：将原版 0.5× 光照图提升至 1.0×，消除篝火/黄昏边缘方块锯齿（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用 1.0× 光照", data = true },
        },
        default = false,
    },
    {
        name = "visual_cas",
        label = "[画质分项] 轻度保真锐化与暗夜防色带",
        hover = "独立开关：极克制零白边保真微锐化（CAS 0.24）+ 暗夜 8-bit 抖动防色带，关闭泛光时同样生效（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用", data = true },
        },
        default = false,
    },
    {
        name = "visual_contact_ao",
        label = "[画质分项] 实体温和接地阴影与防爆白",
        hover = "独立开关（需 bin64/dinput8.dll）：类似原版树荫理念，仅在树干/双脚贴地处增加温和落地阴影并压制白面过曝，零轮廓白光（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用", data = true },
        },
        default = false,
    },
    {
        name = "visual_calm_env",
        label = "[画质分项] 柔和近岸与雨天湿土微调",
        hover = "独立开关（需 bin64/dinput8.dll）：保留原版沉静水色与干爽草地，仅加极微弱近岸过渡与雨天哑光湿土感（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用", data = true },
        },
        default = false,
    },
    {
        name = "visual_soft_bloom",
        label = "[画质分项] 护眼防过曝柔光 Bloom",
        hover = "独立开关：仅在游戏画面设置手动开启「泛光」时替代原版刺眼泛光，强高光保护避免人物白脸过曝；关闭游戏泛光时零开销（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用", data = true },
        },
        default = false,
    },
    {
        name = "visual_storybook_tone",
        label = "[画质分项] 柔和绘本微通透与轻暗角",
        hover = "独立开关：在保护高光不过曝前提下轻微提纯发灰中间调，并加极轻柔和边缘收敛（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用", data = true },
        },
        default = false,
    },
    {
        name = "skeletal_tween",
        label = "真 240 帧骨骼与位移补帧",
        hover = "需配合 bin64/dinput8.dll：对主世界/特效/UI 全部 2D 骨骼与关延迟补偿位移执行连续插值，也可在 ESC -> 设置 -> 画面 随时开关",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用 (原版 30Hz)", data = false },
        },
        default = true,
    },
    {
        name = "smooth_camera",
        label = "高刷镜头阻尼修复",
        hover = "将 FollowCamera 线性插值升级为 60Hz 等效精确指数阻尼，消除高刷手感偏移",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用 (原版)", data = false },
        },
        default = true,
    },
    {
        name = "smooth_ui_240hz",
        label = "高刷列表/地图/放置圈修复",
        hover = "修复高刷下制作栏/聊天滚轮 4 倍过快、小地图缩放 4 倍过快、以及灭火器/避雷针等放置圈闪烁 Bug",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用 (原版)", data = false },
        },
        default = true,
    },
    {
        name = "reduce_240hz_churn",
        label = "高刷零冗余 CPU/GC 减负",
        hover = "消除树冠 DistanceFade 每秒数千次临时表分配与静止帧冗余鼠标拾取(0延迟)，100% 兼容 ShowMe / Insight",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "keep_shard_assets",
        label = "上下洞穴免重加载加速",
        hover = "跨 SimReset 保活已加载的地面/洞穴地形与角色资产，联机往返地面与洞穴跳过重复解压与重载（需 >=8GB 内存，退回主菜单自动释放）",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "fast_mod_scan",
        label = "未启用模组缓存免重扫",
        hover = "切界面时直接复用内存中已解析的未启用模组信息，跳过重复读盘，仍保留全部模组可见可开关",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "skip_top_mods_query",
        label = "跳过热门模组榜请求",
        hover = "打开模组/建房界面时不再向 Steam 拉取热门模组推荐列表，省去 3~13 秒网络等待",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "skip_motd_delay",
        label = "跳过公告网络等待",
        hover = "主界面直接从本地缓存完成 MOTD 加载，不再白等远程海报请求（不影响联机登录）",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用", data = false },
        },
        default = true,
    },
    {
        name = "show_fps_hud",
        label = "实时帧率 HUD",
        hover = "在屏幕右上角显示轻量实时 FPS、1% Low 与帧时间 (ms)，也可在 ESC -> 设置 -> 画面 随时开关",
        options = {
            { description = "显示", data = true },
            { description = "隐藏", data = false },
        },
        default = true,
    },
    {
        name = "show_1pct_low",
        label = "HUD 显示 1% Low 稳帧指标",
        hover = "需配合 bin64/dinput8.dll v2.4.0+：在右上角 HUD 同步展示 C++ 底层实测的 1% Low 帧率与 P99 帧时间 (ms)",
        options = {
            { description = "显示 (推荐)", data = true },
            { description = "仅显示平均帧率", data = false },
        },
        default = true,
    },
    {
        name = "auto_60hz_sim",
        label = "[双端高刷] 自动协商 60Hz 逻辑刻",
        hover = "纯被动安全开关：连接官方/公共服时 100% 保持原版 30Hz 零改动；仅在连接开启 60Hz 的 HyperCore 服务端时自动切换双端 60Hz",
        options = {
            { description = "自动协商 (推荐)", data = true },
            { description = "始终锁定 30Hz 原版", data = false },
        },
        default = true,
    },
    {
        name = "hotkey_cycle",
        label = "循环切换帧率热键",
        hover = "点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（也可在 ESC -> 设置 -> 控制 底部实时改绑，默认禁用零冲突）",
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
    {
        name = "hotkey_skeletal",
        label = "开关 240 帧骨骼补帧热键",
        hover = "点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（局内一键 A/B 盲测对比，默认禁用零冲突）",
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
    {
        name = "hotkey_hud",
        label = "开关帧率 HUD 热键",
        hover = "点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（局内随时显示/隐藏右上角帧率 HUD，默认禁用零冲突）",
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
    {
        name = "hotkey_visual",
        label = "循环切换画质方案热键",
        hover = "点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（局内一键在原版纯净 / 护眼高清 / 柔和绘本间 0ms 同屏对比，关闭即完整恢复原版，默认禁用零冲突）",
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
}
