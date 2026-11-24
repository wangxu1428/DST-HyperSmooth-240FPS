local is_zh = (locale == "zh" or locale == "zhr" or locale == "zht")
local function L(zh, en)
    if is_zh then
        return zh
    end
    return en
end

name = L("HyperSmooth 高刷稳帧与极速加载", "HyperSmooth 240FPS & FastLoad")
description = L([[
纯客户端 240 帧骨骼/位移/影子/粒子/动态光源/镜头全场景补帧、Native OpenGL 硬件加速（完整兼容 Steam 社区界面）、因果 3 通道 RPC QoS、Async libdeflate 零堵帧存盘、三层 80 旋钮实时画质光影工坊、事务级 HS1 分享码与 HS_EXT_V1 标准 Shader 扩展管线（全自动识别官方中英文语言并支持局内热切换）。

【v2.8.0 全栈旗舰更新】
1. 原生引擎全链路提速（Native GL/VBPool + 因果 RPC QoS + Async Save）：
   - 支持 auto / opengl / d3d9 后端路由（Desktop OpenGL 兼容 ES2 + 原生 Steam Overlay 零冲突）、32MB 双轨 GPU 缓冲池（VBPool）+ 2D 精灵/UI 多纹理合批，显著削减大基地与 Boss 峰值期渲染线程卡顿；
   - 因果安全 3 通道 RakNet::RPC4 QoS（战斗/走位/切装 100% 独占 Ch0 最高优先级）+ 500ms 实时 Ping/丢包遥测；
   - Async libdeflate+Base64 后台存盘队列，消除 >=4KB 持久化存档卡顿。
2. 三层 80 旋钮全自由画质光影工坊（默认 100% 原版纯净，零硬编码覆盖）：
   - 一楼单 Pass 后处理：BCAS Pro 双边锐化、手绘墨线反卷积、AURA 防白边、电影调色、ASC CDL（Slope/Offset/Power）、多级辉光金字塔/丁达尔光束、胶片颗粒、独立可逆原版滤镜（理智色/扭曲/积雪上限/沙尘暴）；
   - 二楼 & 三楼原生 C++ 世界光影：3D 实体浮雕受光（合批零损耗）、程序化海面焦散波光与浅滩沙底、240Hz 连续阻尼流体投影（1.0x 全分辨率阴影画布 + 9-Tap 柔和半影 + 高度衰减/微风摇曳 + 影怪免投影，关游戏自带泛光依然生效）；
   - 8 标签局内实时面板（c_hsmenu()，点击数值单项复位）+ 3 个玩家自存槽。
3. 事务级 HS1 画质分享码 & 可读 JSON 方案库（hypersmooth_presets.json）：
   - 支持可选中文标题 + CRC-16 校验短码、一键复制剪贴板；严格执行「先只读校验 → 再原子应用 → 最后落盘」，坏码/越界参数绝不覆盖当前配置，并支持一键撤销导入。
4. HS_EXT_V1 标准 Shader 扩展接口 + 硬件 GPU 耗时计费：
   - 开放 GLOBAL.HyperSmooth.RegisterPlugin 标准管线（6 执行阶段、互斥组、共享昼夜/雨雪上下文总线、单插件异常熔断隔离），Tab 8「扩展」自动生成第三方滑块并实时显示 0 停顿硬件 GL_TIME_ELAPSED GPU 耗时（ms）。
]], [[
Pure client-side 240Hz full-scene interpolation, Native OpenGL (ES2 + full Steam Overlay compatibility) + 32MB GPU VBPool & sprite/UI batching, causal 3-channel RPC QoS + live Ping/Loss telemetry, async libdeflate save offload, 80-knob 3-floor real-time Visual & Lighting Studio, transactional HS1 share codes, and the open HS_EXT_V1 Standard Shader Extension Platform (auto-detects official DST language with live ZH/EN switching).

[v2.8.0 Flagship Release]
1. Native Engine Acceleration (Native GL/VBPool + Causal RPC QoS + Async Save):
   - Auto/OpenGL/D3D9 backend routing (Desktop OpenGL ES2 compatibility with full Steam Overlay support), 32MB 2-frame deferred-retire GPU VBPool, and multi-texture 2D sprite/UI batching;
   - Causal 3-channel RakNet::RPC4 QoS keeping combat/movement/equip RPCs strictly on Ch0 IMMEDIATE_PRIORITY, plus live 500ms Ping/Packet-Loss HUD telemetry;
   - Background libdeflate+Base64 save-commit pipeline under full Win32/CRT safety barriers.
2. 80-Knob 3-Floor Real-Time Visual & Lighting Studio (100% Vanilla Pure by Default):
   - Post-FX Studio: BCAS Pro bilateral sharpening, ink deconvolution, AURA anti-overshoot, cinema grading, ASC CDL, multi-level glow pyramid / god rays, film grain, and reversible vanilla filter controls (sanity tint, distortion, snow cap, sandstorm);
   - Native C++ World Lighting: zero-batch-loss 3D entity relief shading, procedural ocean caustics & sandy shallows, and 240Hz dt-damped fluid ground shadows (1.0x full-res canvas + 9-tap soft penumbra + height fade/canopy sway + shadow-creature exclusion, works safely with Game Bloom OFF);
   - 8-Tab live sidebar (c_hsmenu()) with click-value single reset and 3 player custom slots.
3. Transactional HS1 Share Codes & Human-Readable JSON Preset Library (hypersmooth_presets.json):
   - Compact CRC-16 share codes with optional UTF-8 titles and 1-click clipboard copy; strict Validate -> Atomic Apply -> Persist transaction (corrupt codes never overwrite live config) + 1-click Undo.
4. Open HS_EXT_V1 Standard Shader Extension Interface + Hardware GPU Profiler:
   - Standardized 6-stage PostProcessor pipeline (GLOBAL.HyperSmooth.RegisterPlugin) with shared world context uniforms, exclusive groups, fault-isolation circuit breaker, auto-generated Tab 8 plugin UI, and zero-stall hardware GL_TIME_ELAPSED GPU timing (ms).
]])
author = "Hakimi Tech Lab"
version = "2.8.0"

api_version = 10
dst_compatible = true
dont_starve_compatible = false
reign_of_giants_compatible = false
shipwrecked_compatible = false

client_only_mod = true
all_clients_require_mod = false

icon_atlas = "modicon.xml"
icon = "modicon.tex"

-- NOTE: Klei executes modinfo.lua in a bare sandbox (no ipairs/pairs/string/table/tostring globals).
-- Use pure numeric `for i = 1, #t` loops and literal tables only!
keys_placeholder = {
    {
        description = L("请先启用模组并应用", "Enable & Apply Mod First"),
        hover = L("请先勾选启用本模组并点击「应用」，之后即可在配置页或「设置 -> 控制」底部直接点击录制任意按键",
                  "Enable this mod and click Apply first, then click any hotkey here or in Settings -> Controls to record any key or mouse button"),
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
local mouse_keys = { "\238\132\130", "\238\132\131", "\238\132\132" } -- Middle, Mouse4, Mouse5
local key_disabled = { description = L("禁用", "Disabled"), data = "KEY_DISABLED" }

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
    keys[#keys + 1] = { description = L("小键盘 ", "Numpad ") .. k, data = "KEY_KP_" .. k:upper() }
end

configuration_options = {
    {
        name = "target_fps",
        label = L("默认目标帧率", "Default Target FPS"),
        hover = L("需配合 bin64/dinput8.dll 突破官方 60 帧上限；进游戏后也可直接在 ESC -> 设置 -> 画面 -> 刷新率 随时调整",
                  "Requires bin64/dinput8.dll to unlock >60 FPS; can also be adjusted live in ESC -> Settings -> Graphics -> Refresh Rate"),
        options = {
            { description = L("60 FPS (原版)", "60 FPS (Vanilla)"), data = 60 },
            { description = "120 FPS", data = 120 },
            { description = "144 FPS", data = 144 },
            { description = "165 FPS", data = 165 },
            { description = L("240 FPS (推荐)", "240 FPS (Recommended)"), data = 240 },
            { description = "360 FPS", data = 360 },
            { description = L("无限制", "Unlimited"), data = 0 },
        },
        default = 240,
    },
    {
        name = "visual_preset",
        label = L("[画质增强] 总方案 (默认原版纯净)", "[Visual] Overall Preset (Vanilla Default)"),
        hover = L("玩家优先原则：默认完全原版纯净，不增加任何视觉负担、不强迫开启后处理；可选择快捷档位，或直接保持原版并在下方逐项独立开关。关闭后完整恢复原版表现",
                  "Player-First: 100% vanilla pure by default with zero forced post-processing. Choose a preset or keep Vanilla and toggle individual effects below"),
        options = {
            { description = L("原版纯净 (默认)", "Vanilla Pure (Default)"), data = 0 },
            { description = L("护眼高清 (关泛光全效)", "Eye-Care HD (Works w/ Bloom Off)"), data = 1 },
            { description = L("柔和绘本", "Soft Storybook"), data = 2 },
            { description = L("自定义 (按下方独立开关)", "Custom (Use Toggles Below)"), data = 3 },
        },
        default = 0,
    },
    {
        name = "visual_af16",
        label = L("[画质分项] 16× 各向异性过滤", "[Visual] 16x Anisotropic Filtering"),
        hover = L("独立开关（需 bin64/dinput8.dll）：仅消除斜视角地皮/建筑发糊与闪烁，零偏色零后处理负担（默认关闭）",
                  "Requires bin64/dinput8.dll: Sharpens oblique turf/structures with zero tint or post-process overhead (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用 16× AF", "Enable 16x AF"), data = true },
        },
        default = false,
    },
    {
        name = "visual_full_lightmap",
        label = L("[画质分项] 全分辨率 1.0× 光照图", "[Visual] Full-Resolution 1.0x LightMap"),
        hover = L("独立开关（需 bin64/dinput8.dll）：支持局内 0ms 无重载热切 0.5× <-> 1.0× 光照图并启用篝火/火把 8-bit 抖动防色带，消除黄昏/夜火锯齿与同心环（默认关闭）",
                  "Requires bin64/dinput8.dll: 0ms live toggle between 0.5x and 1.0x LightMap resolution + 8-bit firelight dithering to eliminate banding (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用 1.0× 光照", "Enable 1.0x LightMap"), data = true },
        },
        default = false,
    },
    {
        name = "visual_cas",
        label = L("[画质分项] BCAS 双边锐化与墨线修复", "[Visual] BCAS Bilateral Sharpen & Ink Restore"),
        hover = L("独立开关：单 Pass YCoCg 色亮分离双边锐化 + 反卷积墨线收敛 + AURA 零白边保护 + TPDF 暗夜防色带（关泛光全效，支持侧栏实时调强度，默认关闭）",
                  "Single-pass YCoCg bilateral sharpening + asymmetric ink deconvolution + AURA anti-ringing + TPDF de-banding; works with Bloom OFF (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用", "Enabled"), data = true },
        },
        default = false,
    },
    {
        name = "visual_contact_ao",
        label = L("[画质分项] 实体温和接地阴影与防爆白", "[Visual] Soft Contact Shadow & Highlight Guard"),
        hover = L("独立开关（需 bin64/dinput8.dll）：类似原版树荫理念，仅在树干/双脚贴地处增加温和落地阴影并压制白面过曝，零轮廓白光（默认关闭）",
                  "Requires bin64/dinput8.dll: Subtle grounding shadow around feet/trunks + highlight clip protection with zero bright halos (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用", "Enabled"), data = true },
        },
        default = false,
    },
    {
        name = "visual_calm_env",
        label = L("[画质分项] 柔和近岸与雨天湿土微调", "[Visual] Calm Shorelines & Rain Soil"),
        hover = L("独立开关（需 bin64/dinput8.dll）：保留原版沉静水色与干爽草地，仅加极微弱近岸过渡与雨天哑光湿土感（默认关闭）",
                  "Requires bin64/dinput8.dll: Gentle coastal transition and subtle matte damp soil during rain while keeping vanilla palette (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用", "Enabled"), data = true },
        },
        default = false,
    },
    {
        name = "visual_soft_bloom",
        label = L("[画质分项] 护眼防过曝柔光 Bloom", "[Visual] Eye-Care Soft Bloom"),
        hover = L("独立开关：仅在游戏画面设置手动开启「泛光」时替代原版刺眼泛光，强高光保护避免人物白脸过曝；关闭游戏泛光时零开销（默认关闭）",
                  "Only active when in-game Bloom setting is enabled; replaces harsh vanilla bloom with highlight-safe glow (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用", "Enabled"), data = true },
        },
        default = false,
    },
    {
        name = "visual_storybook_tone",
        label = L("[画质分项] 电影绘本调色与 ACES 胶片", "[Visual] Cinematic Storybook Grading & ACES"),
        hover = L("独立开关：单 Pass 集成暖阳色温、ACES 胶片高光保护、自然饱和与轻暗角，可在 c_hsmenu() 侧栏实时微调曝光/色温/饱和/胶片/官方滤镜（默认关闭）",
                  "Single-pass warm white-balance, ACES filmic curve, highlight protection & subtle vignette; live-tunable in c_hsmenu() sidebar (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用", "Enabled"), data = true },
        },
        default = false,
    },
    {
        name = "visual_mip_sharp",
        label = L("[画质分项] 原生纹理防虚化", "[Visual] Native Texture Mip-Bias Sharpness"),
        hover = L("独立开关（需 bin64/dinput8.dll）：仅对世界/地形/角色 Mipmap 贴图施加硬件原生 -0.45 偏移，拉远视角线条依然清晰，UI/字体零影响、零后处理零显存开销（默认关闭）",
                  "Requires bin64/dinput8.dll: Applies hardware -0.45 Mip LOD bias ONLY to world textures; zero effect on UI/fonts and zero extra VRAM cost (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用", "Enabled"), data = true },
        },
        default = false,
    },
    {
        name = "visual_dark_clarity",
        label = L("[画质分项] 暗夜/洞穴护眼层次", "[Visual] Dusk/Cave/Night Dark Clarity"),
        hover = L("独立开关（需 bin64/dinput8.dll）：仅在洞穴/黄昏/夜晚极暗区域做保色暗部层次提亮并修复原版 ColourCube 31.0 偏移，战争迷雾纯黑不变、白天亮部 0% 改动，关泛光全效（默认关闭）",
                  "Requires bin64/dinput8.dll: Hue-preserving shadow lift in dark caves/dusk/night + ColourCube 31.0 LUT fix; fog-of-war black and daytime untouched (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用", "Enabled"), data = true },
        },
        default = false,
    },
    {
        name = "visual_clean_filter",
        label = L("[画质分项] 低理智保色去晃动与柔雪", "[Visual] Clean Sanity/Distortion & Soft Snow"),
        hover = L("独立开关（纯 Lua 可逆）：屏蔽低理智偏暗偏灰与屏幕扭曲晃动，并将冬季地面刺眼积雪封顶柔化，关闭后即刻还原原版（默认关闭）",
                  "Pure Lua reversible filter: removes low-sanity desaturation & screen wobble distortion, and caps harsh winter snow glare; disabling restores 100% vanilla (Default OFF)"),
        options = {
            { description = L("关闭 (原版默认)", "Off (Vanilla Default)"), data = false },
            { description = L("启用", "Enabled"), data = true },
        },
        default = false,
    },
    {
        name = "skeletal_tween",
        label = L("真 240 帧全场景补帧 (骨骼/影子/粒子/光源/镜头)", "True 240Hz Full-Scene Interpolation"),
        hover = L("需配合 bin64/dinput8.dll：对主世界/特效/UI 全部 2D 骨骼、接地影、关延迟补偿位移与跟拍镜头、动态光源及粒子执行 240Hz 连续插值，也可在画面设置或 c_hsmenu() 面板随时开关",
                  "Requires bin64/dinput8.dll: Co-phase 240Hz interpolation for skeletal anims, DynamicShadows, world positions, FollowText, dynamic lights, and particles"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用 (原版 30Hz)", "Disabled (Vanilla 30Hz)"), data = false },
        },
        default = true,
    },
    {
        name = "low_latency_input",
        label = L("硬件 1 帧低延迟与 240Hz 急甩点击防漏", "D3D9Ex 1-Frame Reflex & 240Hz Click Rescue"),
        hover = L("启用 D3D9Ex 硬件 1 帧渲染队列上限 + 240Hz 急甩鼠标点击前即时刷新动作目标（消除 0~33ms 过时点击落空），100% 公共服安全兼容",
                  "Sets D3D9Ex max frame latency to 1 + refreshes hover action target at 240Hz before clicks & force-attacks (100% public-server safe)"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用 (原版)", "Disabled (Vanilla)"), data = false },
        },
        default = true,
    },
    {
        name = "smooth_camera",
        label = L("高刷镜头阻尼修复", "High-Refresh Camera Damping Fix"),
        hover = L("将 FollowCamera 线性插值升级为 60Hz 等效精确指数阻尼，消除高刷手感偏移",
                  "Upgrades FollowCamera linear step to exact frame-rate-independent exponential damping"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用 (原版)", "Disabled (Vanilla)"), data = false },
        },
        default = true,
    },
    {
        name = "smooth_ui_240hz",
        label = L("高刷列表/地图/放置圈修复", "High-Refresh UI/Scroll/Placer Fix"),
        hover = L("修复高刷下制作栏/聊天滚轮 4 倍过快、小地图缩放 4 倍过快、以及灭火器/避雷针等放置圈闪烁 Bug",
                  "Fixes 4x overly fast scrolling, minimap zoom speed, and placer circle flicker at 240Hz"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用 (原版)", "Disabled (Vanilla)"), data = false },
        },
        default = true,
    },
    {
        name = "reduce_240hz_churn",
        label = L("高刷零冗余 CPU/GC 减负", "240Hz Zero-Churn CPU/GC Guard"),
        hover = L("消除树冠 DistanceFade 每秒数千次临时表分配与静止帧冗余鼠标拾取(0延迟)，100% 兼容 ShowMe / Insight",
                  "Eliminates canopy DistanceFade table churn and redundant stationary mouse hover picks at 240Hz (100% ShowMe/Insight compatible)"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用", "Disabled"), data = false },
        },
        default = true,
    },
    {
        name = "keep_shard_assets",
        label = L("上下洞穴免重加载加速", "Cave Shard Asset Retention"),
        hover = L("跨 SimReset 保活已加载的地面/洞穴地形与角色资产，联机往返地面与洞穴跳过重复解压与重载（需 >=8GB 内存，退回主菜单自动释放）",
                  "Retains loaded world/character textures across shard hops so Forest<->Caves skips redundant reloads (requires >=8GB RAM; freed on main menu)"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用", "Disabled"), data = false },
        },
        default = true,
    },
    {
        name = "fast_mod_scan",
        label = L("未启用模组缓存免重扫", "Fast Disabled-Mod Index Cache"),
        hover = L("切界面时直接复用内存中已解析的未启用模组信息，跳过重复读盘，仍保留全部模组可见可开关",
                  "Caches metadata of disabled mods in memory across screen switches instead of re-reading modinfo files from disk"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用", "Disabled"), data = false },
        },
        default = true,
    },
    {
        name = "skip_top_mods_query",
        label = L("跳过热门模组榜请求", "Skip Top-Mods Network Wait"),
        hover = L("打开模组/建房界面时不再向 Steam 拉取热门模组推荐列表，省去 3~13 秒网络等待",
                  "Skips Steam Featured/Top Mods Workshop query when opening Mods/Host screen (saves 3-13s menu stall)"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用", "Disabled"), data = false },
        },
        default = true,
    },
    {
        name = "skip_motd_delay",
        label = L("跳过公告网络等待", "Skip MOTD Poster Wait"),
        hover = L("主界面直接从本地缓存完成 MOTD 加载，不再白等远程海报请求（不影响联机登录）",
                  "Loads MOTD directly from local cache on title screen instead of stalling on remote banner fetches"),
        options = {
            { description = L("启用 (推荐)", "Enabled (Recommended)"), data = true },
            { description = L("禁用", "Disabled"), data = false },
        },
        default = true,
    },
    {
        name = "show_fps_hud",
        label = L("实时帧率 HUD", "Real-Time FPS HUD"),
        hover = L("在屏幕右上角显示实时性能与联机信息，也可在 ESC -> 设置 -> 画面 随时开关与切换模式",
                  "Displays compact top-right performance and network telemetry; toggleable in ESC -> Settings -> Graphics"),
        options = {
            { description = L("显示", "Show"), data = true },
            { description = L("隐藏", "Hide"), data = false },
        },
        default = true,
    },
    {
        name = "hud_mode",
        label = L("右上角 HUD 显示模式", "Top-Right HUD Mode"),
        hover = L("简洁模式(默认)：仅显示帧率与联机延迟/丢包，一目了然；详细模式：显示帧率、1% Low 最低帧、单帧耗时、渲染后端与网络延迟/丢包（局内可用热键、侧栏面板或 c_hshud() 随时切换）",
                  "Compact (Default): shows FPS + Ping/Loss cleanly at a glance. Detailed: adds 1% Low FPS, P99 frame time, backend badges, and Ping/Loss (switch live via hotkey, sidebar, or c_hshud())"),
        options = {
            { description = L("简洁模式 (默认)", "Compact (Default)"), data = "simple" },
            { description = L("详细模式", "Detailed"), data = "detail" },
        },
        default = "simple",
    },
    {
        name = "show_1pct_low",
        label = L("HUD 详细模式包含 1% Low", "Include 1% Low / P99 in Detailed HUD"),
        hover = L("需配合 bin64/dinput8.dll v2.4.0+：开启 HUD 详细模式时，一并展示 C++ 底层实测的 1% Low 最低帧与单帧耗时 (ms)",
                  "Requires bin64/dinput8.dll v2.4.0+: includes native C++ 1% Low FPS and P99 frame time when Detailed HUD mode is active"),
        options = {
            { description = L("显示 (推荐)", "Show (Recommended)"), data = true },
            { description = L("仅显示平均帧率", "Average FPS Only"), data = false },
        },
        default = true,
    },
    {
        name = "auto_60hz_sim",
        label = L("[双端高刷] 自动协商 60Hz 逻辑刻", "[Synced] Auto-Negotiate 60Hz SimTick"),
        hover = L("纯被动安全开关：连接官方/公共服时 100% 保持原版 30Hz 零改动；仅在连接开启 60Hz 的 HyperCore 服务端时自动切换双端 60Hz",
                  "100% passive safety switch: stays strictly vanilla 30Hz on official/public servers; switches to 60Hz only when connected to a 60Hz HyperCore server"),
        options = {
            { description = L("自动协商 (推荐)", "Auto-Negotiate (Recommended)"), data = true },
            { description = L("始终锁定 30Hz 原版", "Force 30Hz Vanilla"), data = false },
        },
        default = true,
    },
    {
        name = "hotkey_cycle",
        label = L("循环切换帧率热键", "Cycle Target FPS Hotkey"),
        hover = L("点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（也可在 ESC -> 设置 -> 控制 底部实时改绑，默认禁用零冲突）",
                  "Click to bind any key or mouse button (also rebindable in ESC -> Settings -> Controls; disabled by default)"),
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
    {
        name = "hotkey_skeletal",
        label = L("开关 240 帧骨骼补帧热键", "Toggle 240Hz Interpolation Hotkey"),
        hover = L("点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（局内一键 A/B 盲测对比，默认禁用零冲突）",
                  "Click to bind any key or mouse button for instant in-game 240Hz vs 30Hz A/B comparison (disabled by default)"),
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
    {
        name = "hotkey_hud",
        label = L("切换帧率 HUD 热键", "Cycle FPS HUD Mode Hotkey"),
        hover = L("点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（局内按键循环切换：简洁 -> 详细 -> 隐藏，默认禁用零冲突）",
                  "Click to bind any key or mouse button to cycle top-right HUD in-game: Compact -> Detailed -> Hidden (disabled by default)"),
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
    {
        name = "hotkey_visual",
        label = L("循环切换画质方案热键", "Cycle Visual Preset Hotkey"),
        hover = L("点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（局内一键在原版纯净 / 护眼高清 / 柔和绘本间 0ms 同屏对比，关闭即完整恢复原版，默认禁用零冲突）",
                  "Click to bind any key or mouse button for 0ms visual preset A/B comparison (disabled by default)"),
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
    {
        name = "hotkey_panel",
        label = L("开关局内实时微调面板热键", "Toggle In-Game Tuning Sidebar Hotkey"),
        hover = L("点击后按任意键盘按键或鼠标中键/侧键即可自由绑定（局内不暂停呼出右侧半透明微调面板，随时单开/单关全部 9 项画质、240Hz 与低延迟；亦可控制台输入 c_hsmenu()，默认禁用零冲突）",
                  "Click to bind any key or mouse button to open the non-pausing live sidebar panel (or run c_hsmenu() in console; disabled by default)"),
        options = keys_placeholder,
        default = "KEY_DISABLED",
    },
}
