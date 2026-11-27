local is_zh = (locale == "zh" or locale == "zhr" or locale == "zht")
local function L(zh, en)
    if is_zh then
        return zh
    end
    return en
end

name = L("HyperShade · 饥荒超帧与视觉引擎（原 HyperSmooth）", "DST HyperShade — 240FPS & Visual Engine (formerly HyperSmooth)")
description = L([[
纯客户端 240 帧骨骼/位移/影子/粒子/动态光源/镜头全场景补帧、Native OpenGL 硬件加速（完整兼容 Steam 社区界面）、世界空间独立 ShadowMask FBO 流体阴影与 360° 多光源篝火/提灯动态投影 + 沿岸实景平面水面倒影 + 真·白名单植被风动与穿行拨草、原生崩溃侦探与双屏防甩出鼠标锁、全参数实时画质光影工坊、视觉风格闭环（任意截图吸色拟合自有管线参数 → HS1 分享码）与 HS_EXT_V1 标准 Shader 扩展管线（全自动识别官方中英文语言并支持局内热切换）。

【v2.9.7-beta1 测试版热更新】
1. v4.0 Phase 1–5 原生渲染与流送引擎：新增同上下文 3 帧持久一致 PBO 异步贴图上传环、双线程无锁卡顿栈采样、无阻塞 `glGetError`/`glGenTextures` 预分配池、SSE2 向量化与 65520 顶点上限精灵合批、Mode-4 零阻塞环形上传、Klei 64 槽 `VertexDecl::Apply` 硬件直连与 `UiBatcher` 零回读优化，大幅降低大基地转动视角与跑图卡顿。
2. GPU Compute 清晰度分级 & 三档硬件性能档位：支持原版 / 1080P 清晰 / UltraHD 计算锐化 / BluRay 虚拟 4K 四档切换（附 360 帧 / 2.0ms 滞回防抖与实时 GPU 遥测），配合低配 / 均衡 / 狂暴三档硬件策略一键调度。
3. 行为学（ActionQueuer）兼容与 Lua 热路径减负：框选操作期间自动避让低延迟点击刷新，消除行为学框选引发的输入与键盘异常；体积光雾（hs_vollight）改为三相交错流式扫描、`distancefade` 零分配防闪烁、隐藏制作栏延迟重构、`FastDataDumper` 客户端/本地建房加速，并升级 `c_hsprof()` 支持按模组耗时归因。
4. DNH 伴侣支持与 DLL 升级提示：已声明 `dnh_github` 托管元数据；完整体验本版本 Phase 1–5 原生引擎加速与 Compute 清晰度请同步更新 `bin64/dinput8.dll` 至 `v2.9.7-beta1`（旧版 `2.9.6` DLL 会自动降级运行但无新增原生优化）。

【v2.9.6 紧急热修】
1. 修复部分显卡环境下贴图槽位越界导致的整屏异常发红（类似强行戴鼹鼠帽），并修复鼹鼠帽/夜视/启迪冠升级月亮滤镜与画质预设冲突串色的问题。
2. 修复全局帧率偏低回退：默认关闭实验性 DXGI 翻转模式、恢复现代 A 卡（OpenGL 4.5+）走原生 OpenGL、消除原版纯净档位每帧冗余同步与着色彩蛋开销。
3. 修复复杂高密度场景（如神话青竹洲）动作补帧静默失效问题：动作过渡池扩容至 512、修复单帧毛刺误降级并提升密集场景补帧预算。
4. 修复耕地水分变化时的画面频闪：自动直通进度驱动的世界叠加动画（如耕地湿度/养分覆盖层）。
5. 修复基地种植时拉网格对齐辅助导致严重掉帧的问题。

【v2.9.5 全栈演进更新（HyperShade 视觉闭环 + M1 性能 + M2 工具 + M3 水面倒影 + M4 白名单风动 + 多光源动态投影）】
1. 视觉风格闭环（零外部部件 · 内置原生特征分析）：
   - 玩家只需 Win+Shift+S 截图或复制任意参考图片（ReShade、其他游戏、参考画面），在分享卡片点击「吸取剪贴板截图」或控制台输入 c_hslook()，即可在后台线程自动解码缩放并提取 Oklab 视觉特征，严格映射至 HyperShade 自有可控管线参数白名单，一键生成可复现参数状态、可滑块微调、可一键撤销的 HS1- 分享码（仅基于用户提供图片提取视觉统计特征，不读取/复制任何第三方模组着色器、素材或代码）。
2. 360° 多光源近场动态投影（最多 3 盏篝火/火坑/提灯/星杖/火把）：
   - 夜晚/洞穴绕火源移动时，合批建筑与蒙皮树木/生物的影子同步 360° 实时背离火源旋转并随距离自然拉长衰减；黄昏与满月自动平滑交接日/月平行影，零额外 FBO 显存占用。
3. 沿岸真·平面水面倒影（M3 · 内陆/洞穴 0 开销）：
   - 独立 512×512 RGBA8+DEPTH16 双缓冲透视精确平面倒影 FBO，仅当帧内出现海洋绘制时按需激活（内陆与洞穴 0 Draw / 0.00ms），与原生海浪噪声波纹同步扭曲并自动接入原版光照图；默认 0% 纯净原版。
4. 真·白名单植被微风摇曳 & 玩家穿行分浪拨草（M4 · 100% 刚体建筑防果冻）：
   - 严格通过官方植物/树木标签白名单 + L1 缓存 16KB 量化空间哈希在 GPU 顶点阶段驱动草丛/树苗/浆果丛/芦苇微风与穿行拨草、树木仅树冠随风摇曳且与树影同相位锁步，建筑/墙体/玩家 100% 刚体不动。
5. 渲染状态去重加速 + 崩溃侦探 + 双屏防甩出鼠标锁（M1 + M2）：
   - Stencil/PolygonOffset 纳入 GL 状态缓存消除多余合批打断，崩溃自动生成中英双语 hypersmooth_crash_report.txt；Tab 1 与 c_hsclip() 支持双屏防甩出鼠标锁（ESC/控制台/切屏/崩溃 100% 自动解锁）。
6. 内置 6 套官方实调预设面板 + 100% 原版纯净保障：
   - 面板顶部与简易模式一键打开「预设面板」（原版纯净 / 护眼高清 / 电影绘本 / 清透日系 / 篝火暖夜 / 林间晨雾，全套原创调试档位，支持一键撤销与滑块微调）；选择「原版纯净」时严格保持原版 0.25× 泛光采样与魔光护符/火光原生亮度，不附加任何画面改动。
]], [[
Pure client-side 240Hz full-scene interpolation, Native OpenGL (ES2 + Steam Overlay compatibility), World-Space ShadowMask FBO fluid shadows with 360-deg Top-3 campfire/lantern dynamic point shadows, coastal-only true planar water reflections, strict tag-whitelisted GPU foliage wind-sway & player parting wake, zero-heap Crash Detective + safe Dual-Monitor Cursor Lock, real-time Visual & Lighting Studio with 6 built-in curated presets, Visual Closed-Loop (any reference screenshot -> HyperShade owned capability whitelist -> transactional HS1 share code), and the HS_EXT_V1 Shader Extension Platform.

[v2.9.7-beta1 Beta Hotfix]
1. v4.0 Phase 1-5 Native Rendering & Streaming Engine: 3-frame in-context persistent-coherent PBO async texture upload ring, lockless dual-thread hitch stack sampler, non-blocking glGetError & PreallocTexIdPool, SSE2-vectorized SpriteBatcher with 65520-vertex ceiling, Mode-4 non-blocking ring uploads, direct Klei 64-slot VertexDecl::Apply fastpath, and zero-readback UiBatcher optimization to slash mega-base camera rotation and movement hitches.
2. GPU Compute Clarity Grades & 3 Hardware Performance Tiers: Vanilla / 1080p Clarity / UltraHD Compute / BluRay Virtual-4K grades (with 360-frame / 2.0ms hysteresis and live GPU telemetry) paired with Low / Balanced / Unleashed hardware tiers.
3. ActionQueuer Compatibility & Lua Hot-Path Savings: Automatically defers low-latency click refresh during ActionQueuer box selection (fixing input/keyboard glitches), 3-phase staggered hs_vollight spatial streaming, zero-alloc distancefade, deferred hidden crafting rebuilds, client/host FastDataDumper acceleration, and per-mod attribution in c_hsprof().
4. DNH Companion Support: Declared dnh_github metadata; update bin64/dinput8.dll to v2.9.7-beta1 for full Phase 1-5 native acceleration and GPU Compute Clarity (older 2.9.6 DLL degrades safely without the new native features).

[v2.9.6 Hotfix]
1. Fixed abnormal red-screen tint (texture unit bounds guard for ShadowMask/WaterRefl) and preserved special vision / upgraded Enlightened Crown lunacy ColourCube filters.
2. Fixed baseline FPS regression: DXGI flip-present now defaults to off, modern AMD GPUs (OpenGL 4.5+) stay on Native OpenGL, and Preset 0 steady-state per-frame sync overhead is eliminated.
3. Fixed silent animation interpolation fallback in dense scenes (e.g., Myth Words Bamboo Island): 4x pose crossfade pool (512), spike-clamped sampler, and raised shedding thresholds.
4. Fixed farm soil moisture overlay flicker by bypassing SetPercent-driven world overlays and settling finished tracks.
5. Fixed severe FPS drop during base planting grid-helper sweeps in Placer:UpdateAxisAlignedHelpers.

[v2.9.5 Full-Stack Milestone (HyperShade Visual Closed-Loop + Built-In Preset Gallery + M1-M4 + Multi-Light Shadows)]
1. Visual Closed-Loop (Zero External Tools - Built into dinput8.dll):
   - Copy any screenshot or image file (Win+Shift+S / Ctrl+C / ReShade / other games / reference art) and click "Match Clipboard" (or run c_hslook()) to analyze Oklab tonal/color features on a background thread and fit ONLY HyperShade's owned controllable pipeline parameters into a transactional HS1- share code (reproducible parameter state, editable sliders, 1-click Undo; never reads or copies third-party mod shaders, assets, or code).
2. 360-Degree Top-3 Near-Field Point-Light Shadows (Campfires, Firepits, Lanterns, Starcallers, Torches):
   - Radial 360-deg shadows for batched & skinned entities with smooth dusk/full-moon cross-fade, GL_MAX on Native GL & strength-prioritized DEPTH16 on ANGLE D3D9, and zero extra FBO VRAM.
3. True Planar Water Reflections (M3 - 0.00ms Inland & in Caves):
   - Dedicated 512x512 RGBA8+DEPTH16 ping-pong perspective-exact planar reflection FBO active ONLY on coastal ocean frames, wave-distorted via native ocean noise and lit by the game lightmap (default 0% in Vanilla Pure).
4. Strict Tag-Whitelist Foliage Wind-Sway & Player Parting Wake (M4 - 100% Rigid Buildings):
   - 16KB L1-cache O(1) spatial hash fed by official plant/tree tags (strictly excluding structures/walls/chests/players) driving GPU vertex sway & grass wake phase-locked to tree shadows; instant 0ms zero-out on Preset 0.
5. State-Cache Stencil/PolyOff Dedup + Crash Detective + Dual-Monitor Cursor Lock (M1 + M2):
   - Eliminates redundant Stencil/PolygonOffset batch flushes; writes bilingual hypersmooth_crash_report.txt with module+RVA stack & Lua Mod blame; opt-in ClipCursor in Tab 1 / c_hsclip() with guaranteed auto-unlock on Pause/Console/Alt+Tab/Crash.
6. Built-In 6-Preset Curated Gallery + 100% Untouched Vanilla Pure:
   - 1-click "Presets" gallery panel (Vanilla Pure / Eye-Care HD / Cinematic Storybook / Crisp Daylight / Cozy Firelight / Misty Canopy — 100% original HyperShade looks, editable & 1-click Undo); Vanilla Pure strictly keeps stock 0.25x BloomSampler & Magiluminescence brightness with zero added visual effects.
]])
author = "Hakimi Tech Lab"
version = "2.9.7-beta1"
dnh_github = "wangxu1428/DST-HyperSmooth-240FPS"
dnh_github_asset = "dinput8.dll"

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
            { description = L("电影绘本 (全特效推荐)", "Cinematic Storybook (Recommended)"), data = 2 },
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
