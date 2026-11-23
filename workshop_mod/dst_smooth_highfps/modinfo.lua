name = "HyperSmooth 240FPS & FastLoad (高刷稳帧骨骼补帧与极速加载)"
description = [[
纯客户端 240 帧骨骼/位移/粒子/动态光源/镜头全场景补帧、D3D9Ex 硬件 1 帧低延迟队列与 240Hz 急甩点击防漏、零争抢 RTSS 级 1% Low 稳帧、Boss 并发防卡顿、局内实时微调侧栏面板、全按键自由录制绑定与玩家优先护眼画质精修。

【v2.6.0 大版本核心升级 · 三大体验支柱 + 局内微调全面突破】
1. 画面与交互流畅（全场景 240Hz 同步 + 拾取/开箱零微卡）：
   - 纯渲染层 240Hz 粒子与特效连续插值（覆盖现代 VFXEffect 与经典 ParticleEmitter），雨雪、花粉、篝火火星、火把烟雾与 Boss 法术粒子告别 30Hz 阶梯；
   - 新增 240Hz 移动动态光源插值（火把/提灯/矿工帽/萤火虫/唤星法杖光圈随人物 240Hz 同相滑行）、关延迟补偿 240Hz 同相跟拍镜头、240Hz 对话气泡（FollowText）同相锁定与全滚动列表（含图鉴/菜谱/设置 TrueScrollArea）高刷顺滑滚动；
   - 制作栏零卡顿重构：修复原版拾取物品/开箱/战斗血理波动时重复全扫背包箱子数万次并刷新隐藏界面的微卡顿（兼容附近箱子制作等第三方 Mod，单次 O(格数) 快照提速数十倍）；单人/无洞穴本地建房自动启用 C++ FastDataDumper + libdeflate 存档压缩 + O(N log N) FastAStar 寻路引擎，大幅削减清晨自动存档卡顿与围墙基地寻路尖峰！
2. 操作低延迟（硬件队列 + 战斗/走位/强制攻击防漏）：
   - D3D9Ex 硬件级 SetMaximumFrameLatency(1) + 0ms RakNet 事件驱动发包唤醒，消除排队与网络空转延迟；
   - 240Hz 鼠标急甩点击 & 静止 Ctrl+左键强制攻击即时动作刷新 + 走位取消（Walk-Cancel）方向即时放行，修复原版 30Hz 动作缓存导致的快甩落空、强制攻击误走位与翻滚/走位延迟（消除 0~33.3ms 延迟，不发双包、零状态机分歧），配合 c_hslatency() 实时体检。
3. 画质提升（玩家优先五铁律 · 默认 100% 原版纯净）：
   - 全新 9 项独立开关（全部默认关闭，关闭即 100% 完整还原原版）：新增「原生纹理防虚化 (D3DSAMP_MIPMAPLODBIAS -0.45，仅世界贴图生效、UI/字体零影响)」、「暗夜/洞穴护眼层次（含原版 ColourCube 31.0 偏移修复，战争迷雾纯黑与白天亮部 0% 改动）」；
   - 升级「全分辨率 1.0× 光照图」支持局内 0ms 无重载即时热切（免重启/免改窗口）+ 篝火/火把 8-bit 同心圆色带消除（零开销 IGN 抖动，纯黑无抬升）。
4. 局内实时微调侧栏面板（不暂停、不挡视野）：
   - 控制台输入 c_hsmenu() 或绑定热键即可呼出右侧半透明微调面板，一边走位一边 0ms 单独开关全部 9 项画质、240Hz 补帧与低延迟选项，并内置实时 FPS/1%Low/显存/延迟遥测与「一键恢复原版纯净」。

【既有核心特性】
5. Boss 并发防卡顿五件套：5× 提速栈哈希骨骼管线、5 条客户端空间网格零拷贝、RenderThread 异步纹理上传 1.2ms/帧时间片限流、并发平滑避让 GC 与极端并发保护。
6. 全按键自由录制绑定 & 原生画面设置直控：Mod 配置页与「ESC -> 设置 -> 控制」底部支持录制任意键盘/鼠标中键侧键（默认全部禁用零冲突）；原生解锁「ESC -> 设置 -> 画面 -> 刷新率」（60~360/无限制）。
7. 上下洞穴免重加载加速 + RTSS 稳帧 + mimalloc 无锁 Lua 堆 + 可选双端 60Hz 逻辑刻（连接官方/公共服 100% 保持原版 30Hz）+ libdeflate v1.23 极速加载。

【使用提示】
- 100% 纯客户端生效，支持所有官方/公共联机服；将配套开源 bin64/dinput8.dll 放入游戏 bin64/ 目录即可解锁全部 C++ 引擎级特性。
- 局内控制台命令：c_hsmenu() 开关实时微调面板、c_hslatency() 查看延迟/粒子/光源状态、c_hsvisual() 查看画质与显存、c_hsopt("mip", true) 单项开关。
]]
author = "Hakimi Tech Lab"
version = "2.6.0"

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
        hover = "独立开关（需 bin64/dinput8.dll）：支持局内 0ms 无重载热切 0.5× <-> 1.0× 光照图并启用篝火/火把 8-bit 抖动防色带，消除黄昏/夜火锯齿与同心环（默认关闭）",
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
        name = "visual_mip_sharp",
        label = "[画质分项] 原生纹理防虚化 (Mip LOD)",
        hover = "独立开关（需 bin64/dinput8.dll）：仅对世界/地形/角色 Mipmap 贴图施加硬件原生 -0.45 偏移，拉远视角线条依然清晰，UI/字体零影响、零后处理零显存开销（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用", data = true },
        },
        default = false,
    },
    {
        name = "visual_dark_clarity",
        label = "[画质分项] 暗夜/洞穴护眼层次",
        hover = "独立开关（需 bin64/dinput8.dll）：仅在洞穴/黄昏/夜晚极暗区域做保色暗部层次提亮并修复原版 ColourCube 31.0 偏移，战争迷雾纯黑不变、白天亮部 0% 改动，关泛光全效（默认关闭）",
        options = {
            { description = "关闭 (原版默认)", data = false },
            { description = "启用", data = true },
        },
        default = false,
    },
    {
        name = "skeletal_tween",
        label = "真 240 帧全场景补帧 (骨骼/粒子/光源/镜头)",
        hover = "需配合 bin64/dinput8.dll：对主世界/特效/UI 全部 2D 骨骼、关延迟补偿位移与跟拍镜头、移动火把/萤火虫动态光源及雨雪/火星/法术粒子执行 240Hz 连续插值，也可在画面设置或 c_hsmenu() 面板随时开关",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用 (原版 30Hz)", data = false },
        },
        default = true,
    },
    {
        name = "low_latency_input",
        label = "硬件 1 帧低延迟与 240Hz 急甩点击防漏",
        hover = "启用 D3D9Ex 硬件 1 帧渲染队列上限 + 240Hz 急甩鼠标点击前即时刷新动作目标（消除 0~33ms 过时点击落空），100% 公共服安全兼容",
        options = {
            { description = "启用 (推荐)", data = true },
            { description = "禁用 (原版)", data = false },
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
    {
        name = "hotkey_panel",
        label = "开关局内实时微调面板热键",
        hover = "点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（局内不暂停呼出右侧半透明微调面板，随时单开/单关全部 9 项画质、240Hz 与低延迟；亦可控制台输入 c_hsmenu()，默认禁用零冲突）",
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
}
