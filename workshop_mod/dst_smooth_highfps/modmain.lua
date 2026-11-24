local GLOBAL = GLOBAL
local TheSim = GLOBAL.TheSim
local TheInput = GLOBAL.TheInput
local KnownModIndex = GLOBAL.KnownModIndex
local Text = GLOBAL.require("widgets/text")
local TEMPLATES = GLOBAL.require("widgets/redux/templates")
local pcall = GLOBAL.pcall
local rawget = GLOBAL.rawget
local rawset = GLOBAL.rawset
local getmetatable = GLOBAL.getmetatable
local setmetatable = GLOBAL.setmetatable
local type = GLOBAL.type
local pairs = GLOBAL.pairs
local ipairs = GLOBAL.ipairs
local next = GLOBAL.next
local math = GLOBAL.math
local string = GLOBAL.string
local table = GLOBAL.table
local tonumber = GLOBAL.tonumber
local tostring = GLOBAL.tostring

if type(Asset) == "function" then
    Assets = {
        Asset("SHADER", "shaders/hs_kawase_down.ksh"),
        Asset("SHADER", "shaders/hs_kawase_up.ksh"),
        Asset("SHADER", "shaders/hs_postprocess_bloom.ksh"),
        Asset("SHADER", "shaders/hs_visual_final.ksh"),
    }
end

local TARGET_FPS = GetModConfigData("target_fps")
if type(TARGET_FPS) ~= "number" then TARGET_FPS = 240 end
local VISUAL_PRESET = GetModConfigData("visual_preset")
if type(VISUAL_PRESET) ~= "number" then VISUAL_PRESET = 0 end
if VISUAL_PRESET < 0 or VISUAL_PRESET > 3 then VISUAL_PRESET = 0 end

-- Player-First: 10 independent per-effect toggles (ALL default OFF -> 100% Vanilla Pure out of the box!)
local CFG_VIS_AF16         = GetModConfigData("visual_af16") == true
local CFG_VIS_LIGHTMAP     = GetModConfigData("visual_full_lightmap") == true
local CFG_VIS_CAS          = GetModConfigData("visual_cas") == true
local CFG_VIS_AO           = GetModConfigData("visual_contact_ao") == true
local CFG_VIS_ENV          = GetModConfigData("visual_calm_env") == true
local CFG_VIS_SOFT_BLOOM   = GetModConfigData("visual_soft_bloom") == true
local CFG_VIS_TONE         = GetModConfigData("visual_storybook_tone") == true
local CFG_VIS_MIP_SHARP    = GetModConfigData("visual_mip_sharp") == true
local CFG_VIS_DARK         = GetModConfigData("visual_dark_clarity") == true
local CFG_VIS_CLEAN_FILTER = GetModConfigData("visual_clean_filter") == true
local CFG_VIS_MASK = (CFG_VIS_AF16 and 1 or 0)
    + (CFG_VIS_LIGHTMAP and 2 or 0)
    + (CFG_VIS_CAS and 4 or 0)
    + (CFG_VIS_AO and 8 or 0)
    + (CFG_VIS_ENV and 16 or 0)
    + (CFG_VIS_SOFT_BLOOM and 32 or 0)
    + (CFG_VIS_TONE and 64 or 0)
    + (CFG_VIS_MIP_SHARP and 128 or 0)
    + (CFG_VIS_DARK and 256 or 0)
    + (CFG_VIS_CLEAN_FILTER and 512 or 0)
if VISUAL_PRESET == 0 and CFG_VIS_MASK > 0 then
    VISUAL_PRESET = 3 -- User enabled individual switches in Mod Config -> honor Custom mode directly!
end

local SKELETAL_TWEEN = GetModConfigData("skeletal_tween")
if SKELETAL_TWEEN == nil then SKELETAL_TWEEN = true end
local LOW_LATENCY_INPUT = GetModConfigData("low_latency_input")
if LOW_LATENCY_INPUT == nil then LOW_LATENCY_INPUT = true end
local SMOOTH_CAMERA = GetModConfigData("smooth_camera")
if SMOOTH_CAMERA == nil then SMOOTH_CAMERA = true end
local SMOOTH_UI_240HZ = GetModConfigData("smooth_ui_240hz")
if SMOOTH_UI_240HZ == nil then SMOOTH_UI_240HZ = true end
local REDUCE_240HZ_CHURN = GetModConfigData("reduce_240hz_churn")
if REDUCE_240HZ_CHURN == nil then REDUCE_240HZ_CHURN = true end
local KEEP_SHARD_ASSETS = GetModConfigData("keep_shard_assets")
if KEEP_SHARD_ASSETS == nil then KEEP_SHARD_ASSETS = true end
local FAST_MOD_SCAN = GetModConfigData("fast_mod_scan")
if FAST_MOD_SCAN == nil then FAST_MOD_SCAN = true end
local SKIP_TOP_MODS_QUERY = GetModConfigData("skip_top_mods_query")
if SKIP_TOP_MODS_QUERY == nil then SKIP_TOP_MODS_QUERY = true end
local SKIP_MOTD_DELAY = GetModConfigData("skip_motd_delay")
if SKIP_MOTD_DELAY == nil then SKIP_MOTD_DELAY = true end
local SHOW_FPS_HUD = GetModConfigData("show_fps_hud")
if SHOW_FPS_HUD == nil then SHOW_FPS_HUD = true end
local HUD_MODE = GetModConfigData("hud_mode")
if HUD_MODE ~= "detail" then HUD_MODE = "simple" end
local SHOW_1PCT_LOW = GetModConfigData("show_1pct_low")
if SHOW_1PCT_LOW == nil then SHOW_1PCT_LOW = true end
local AUTO_60HZ_SIM = GetModConfigData("auto_60hz_sim")
if AUTO_60HZ_SIM == nil then AUTO_60HZ_SIM = true end

local CFG_FILENAME = "dst_smooth_highfps_cfg"
local FPS_PRESETS = { 60, 120, 144, 165, 240, 360, 0 }
local current_target_fps = TARGET_FPS
local current_visual_preset = VISUAL_PRESET
local custom_vis_flags = {
    af16         = CFG_VIS_AF16,
    full_lm      = CFG_VIS_LIGHTMAP,
    cas          = CFG_VIS_CAS,
    ao           = CFG_VIS_AO,
    env          = CFG_VIS_ENV,
    soft_bloom   = CFG_VIS_SOFT_BLOOM,
    tone         = CFG_VIS_TONE,
    mip_sharp    = CFG_VIS_MIP_SHARP,
    dark_clarity = CFG_VIS_DARK,
    clean_filter = CFG_VIS_CLEAN_FILTER,
}

-- Full 80-knob parameter-driven visual studio registry (ZERO hardcoded runtime overrides!)
-- Presets 0/1/2 are purely curated starting snapshots; Preset 3 (Custom) + 3 User Save Slots hand 100% control to the player.
local TUNE_KEYS = {
    -- Sharpen / Ink Deconv / Bilateral / AURA / Texture (16)
    "sharp", "ink", "deconv_gate", "deconv_penetr",
    "anti_ring", "dark_prot", "chroma_prot", "noise_reduce",
    "range_sigma", "spatial_sigma", "noise_floor",
    "ar_thresh", "ar_l_over", "ar_d_over", "dither", "mip_bias",
    -- Multi-Level Glow Pyramid & Atmospheric FX (12)
    "bloom_str", "bloom_thresh", "bloom_knee", "bloom_chr", "bloom_prot",
    "bloom_warmth", "bloom_spread", "bloom_compress",
    "glow_tail", "light_wrap", "glow_rim", "god_rays",
    -- World AO / Env / Dark + Native 3D Surface Light + Fluid Ground Shadows v2 + Ocean Caustics (24)
    "ao_str", "env_str", "dark_str",
    "surface_light", "shade_warm", "shade_contrast", "shade_cool", "shade_dark",
    "shadow_density", "shadow_length", "shadow_soft", "shadow_fade", "shadow_sway", "shadow_smooth",
    "shadow_tint_r", "shadow_tint_g", "shadow_tint_b", "shadow_night", "shadow_hd", "shadow_skip_nightmare",
    "glint_str", "glint_density", "glint_grain", "glint_soft",
    -- Cinematic Color Grading + ASC CDL + Film Atmo (23)
    "exposure", "temp", "tint", "contrast", "lightness", "gamma",
    "sat", "vibrance", "filmic", "hl_desat", "sun_fill", "original_mix",
    "slope_r", "slope_g", "slope_b", "offset_r", "offset_g", "offset_b",
    "power_r", "power_g", "power_b", "vignette", "grain",
    -- Independent Reversible Vanilla Filters (5)
    "vanilla_cc", "snow_cap", "sanity_color", "distort_free", "sand_filter",
}
local TUNE_BOUNDS = {
    -- Sharpen / Ink / Bilateral / AURA / Texture
    sharp          = { min = 0.00,  max = 1.60,  default = 1.00 },
    ink            = { min = 0.00,  max = 1.60,  default = 1.00 },
    deconv_gate    = { min = 0.010, max = 0.080, default = 0.025, prec = 3 },
    deconv_penetr  = { min = 0.00,  max = 1.00,  default = 0.80 },
    anti_ring      = { min = 0.00,  max = 1.00,  default = 0.90 },
    dark_prot      = { min = 0.00,  max = 1.00,  default = 0.25 },
    chroma_prot    = { min = 0.00,  max = 1.00,  default = 1.00 },
    noise_reduce   = { min = 0.00,  max = 1.00,  default = 0.65 },
    range_sigma    = { min = 0.05,  max = 1.50,  default = 0.26 },
    spatial_sigma  = { min = 0.40,  max = 3.00,  default = 1.10 },
    noise_floor    = { min = 0.002, max = 0.050, default = 0.010, prec = 3 },
    ar_thresh      = { min = 0.000, max = 0.030, default = 0.000, prec = 3 },
    ar_l_over      = { min = 0.000, max = 0.080, default = 0.001, prec = 3 },
    ar_d_over      = { min = 0.000, max = 0.080, default = 0.001, prec = 3 },
    dither         = { min = 0.00,  max = 2.00,  default = 1.00 },
    mip_bias       = { min = -0.80, max = 0.00,  default = -0.45 },
    -- Glow Pyramid & Atmospheric FX
    bloom_str      = { min = 0.00,  max = 2.50,  default = 0.70 },
    bloom_thresh   = { min = 0.00,  max = 0.80,  default = 0.04 },
    bloom_knee     = { min = 0.00,  max = 1.00,  default = 0.55 },
    bloom_chr      = { min = -0.50, max = 0.60,  default = 0.12 },
    bloom_prot     = { min = 0.00,  max = 0.92,  default = 0.85 },
    bloom_warmth   = { min = -1.00, max = 1.00,  default = 0.25 },
    bloom_spread   = { min = 0.00,  max = 1.00,  default = 0.55 },
    bloom_compress = { min = 0.00,  max = 0.80,  default = 0.10 },
    glow_tail      = { min = 0.00,  max = 1.00,  default = 0.30 },
    light_wrap     = { min = 0.00,  max = 1.00,  default = 0.00 },
    glow_rim       = { min = 0.00,  max = 1.00,  default = 0.00 },
    god_rays       = { min = 0.00,  max = 2.00,  default = 0.00 },
    -- World AO / Env / Dark / 3D Relief Surface Light / Ground Shadows / Ocean Caustics
    ao_str         = { min = 0.00,  max = 1.50,  default = 1.00 },
    env_str        = { min = 0.00,  max = 1.50,  default = 1.00 },
    dark_str       = { min = 0.00,  max = 1.50,  default = 1.00 },
    surface_light  = { min = 0.00,  max = 1.50,  default = 0.00 },
    shade_warm     = { min = 0.00,  max = 1.00,  default = 0.50 },
    shade_contrast = { min = 0.00,  max = 1.00,  default = 0.50 },
    shade_cool     = { min = 0.00,  max = 1.00,  default = 0.50 },
    shade_dark     = { min = 0.00,  max = 1.00,  default = 0.50 },
    shadow_density = { min = 0.00,  max = 1.00,  default = 0.00 },
    shadow_length  = { min = 0.50,  max = 1.80,  default = 1.00 },
    shadow_soft    = { min = 0.40,  max = 2.00,  default = 1.20 },
    shadow_fade    = { min = 0.00,  max = 1.00,  default = 0.55 },
    shadow_sway    = { min = 0.00,  max = 1.20,  default = 0.45 },
    shadow_smooth  = { min = 0.00,  max = 1.00,  default = 0.65 },
    shadow_tint_r  = { min = 0.00,  max = 1.00,  default = 0.10 },
    shadow_tint_g  = { min = 0.00,  max = 1.00,  default = 0.12 },
    shadow_tint_b  = { min = 0.00,  max = 1.00,  default = 0.28 },
    shadow_night   = { min = 0,     max = 1,     default = 1, is_bool = true },
    shadow_hd      = { min = 0,     max = 1,     default = 1, is_bool = true },
    shadow_skip_nightmare = { min = 0, max = 1,  default = 1, is_bool = true },
    glint_str      = { min = 0.00,  max = 1.50,  default = 0.00 },
    glint_density  = { min = 0.50,  max = 0.99,  default = 0.92 },
    glint_grain    = { min = 0.40,  max = 2.20,  default = 0.85 },
    glint_soft     = { min = 0.00,  max = 1.00,  default = 0.85 },
    -- Cinematic Color Grading + ASC CDL + Film Atmo
    exposure       = { min = -0.80, max = 0.80,  default = 0.00 },
    temp           = { min = -1.00, max = 1.00,  default = 0.00 },
    tint           = { min = -1.00, max = 1.00,  default = 0.00 },
    contrast       = { min = -0.40, max = 0.60,  default = 0.00 },
    lightness      = { min = -0.30, max = 0.30,  default = 0.00 },
    gamma          = { min = 0.60,  max = 1.60,  default = 1.00 },
    sat            = { min = 0.30,  max = 1.80,  default = 1.00 },
    vibrance       = { min = -0.60, max = 0.80,  default = 0.00 },
    filmic         = { min = 0.00,  max = 1.00,  default = 0.00 },
    hl_desat       = { min = 0.00,  max = 1.00,  default = 0.00 },
    sun_fill       = { min = 0.00,  max = 1.00,  default = 0.00 },
    original_mix   = { min = 0.00,  max = 1.00,  default = 0.00 },
    slope_r        = { min = 0.50,  max = 1.60,  default = 1.00 },
    slope_g        = { min = 0.50,  max = 1.60,  default = 1.00 },
    slope_b        = { min = 0.50,  max = 1.60,  default = 1.00 },
    offset_r       = { min = -0.25, max = 0.25,  default = 0.00 },
    offset_g       = { min = -0.25, max = 0.25,  default = 0.00 },
    offset_b       = { min = -0.25, max = 0.25,  default = 0.00 },
    power_r        = { min = 0.50,  max = 1.60,  default = 1.00 },
    power_g        = { min = 0.50,  max = 1.60,  default = 1.00 },
    power_b        = { min = 0.50,  max = 1.60,  default = 1.00 },
    vignette       = { min = 0.00,  max = 0.50,  default = 0.00 },
    grain          = { min = 0.00,  max = 0.60,  default = 0.00 },
    -- Reversible Vanilla Filters
    vanilla_cc     = { min = 0.00,  max = 1.00,  default = 1.00 },
    snow_cap       = { min = 0.00,  max = 1.00,  default = 1.00 },
    sanity_color   = { min = 0,     max = 1,     default = 0, is_bool = true },
    distort_free   = { min = 0,     max = 1,     default = 0, is_bool = true },
    sand_filter    = { min = 0,     max = 1,     default = 0, is_bool = true },
}
local vis_tune = {}
for i = 1, #TUNE_KEYS do
    local k = TUNE_KEYS[i]
    vis_tune[k] = TUNE_BOUNDS[k].default
end
if CFG_VIS_CLEAN_FILTER then
    vis_tune.sanity_color = 1
    vis_tune.distort_free = 1
    vis_tune.snow_cap     = 0.65
    vis_tune.sand_filter  = 1
end

local function MakePresetTune(overrides)
    local t = {}
    for i = 1, #TUNE_KEYS do
        local k = TUNE_KEYS[i]
        t[k] = TUNE_BOUNDS[k].default
    end
    if type(overrides) == "table" then
        for k, v in pairs(overrides) do
            t[k] = v
        end
    end
    return t
end

local PRESET_TUNE = {
    -- Preset 0: 100% Vanilla Pure (zero shaders, zero shadows, zero caustics, zero filters)
    [0] = MakePresetTune({
        sharp = 0.00, ink = 0.00, dither = 0.00, mip_bias = 0.00,
        ao_str = 0.00, env_str = 0.00, dark_str = 0.00,
        surface_light = 0.00, shadow_density = 0.00, glint_str = 0.00,
        exposure = 0.00, temp = 0.00, tint = 0.00, contrast = 0.00, lightness = 0.00, gamma = 1.00,
        sat = 1.00, vibrance = 0.00, filmic = 0.00, hl_desat = 0.00, sun_fill = 0.00, original_mix = 0.00,
        vignette = 0.00, grain = 0.00,
        vanilla_cc = 1.00, snow_cap = 1.00, sanity_color = 0, distort_free = 0, sand_filter = 0,
    }),
    -- Preset 1: Eye-Care HD (crisp BCAS + 1.0x LM + gentle bloom + subtle ocean shimmer, vanilla colors)
    [1] = MakePresetTune({
        sharp = 0.85, ink = 0.85, anti_ring = 0.90, dither = 1.00, mip_bias = -0.45,
        bloom_str = 0.68, bloom_thresh = 0.04, bloom_knee = 0.55, bloom_chr = 0.10, bloom_prot = 0.85,
        bloom_warmth = 0.15, bloom_spread = 0.50, bloom_compress = 0.10, glow_tail = 0.25,
        ao_str = 1.00, env_str = 0.00, dark_str = 0.00,
        surface_light = 0.00, shadow_density = 0.00, glint_str = 0.00,
        vanilla_cc = 1.00, snow_cap = 1.00, sanity_color = 0, distort_free = 0, sand_filter = 0,
    }),
    -- Preset 2: Cinematic Storybook Studio (BCAS + Warm ACES + 3D Relief Lighting + Velvety Ground Shadows + Ocean Caustics!)
    [2] = MakePresetTune({
        sharp = 1.00, ink = 1.00, anti_ring = 0.90, dither = 1.00, mip_bias = -0.45,
        bloom_str = 0.82, bloom_thresh = 0.04, bloom_knee = 0.50, bloom_chr = 0.14, bloom_prot = 0.85,
        bloom_warmth = 0.45, bloom_spread = 0.62, bloom_compress = 0.10, glow_tail = 0.45, god_rays = 0.35,
        ao_str = 1.10, env_str = 1.00, dark_str = 0.00,
        surface_light = 0.75, shade_warm = 0.62, shade_contrast = 0.52, shade_cool = 0.72, shade_dark = 0.45,
        shadow_density = 0.45, shadow_length = 1.05, shadow_soft = 1.30, shadow_fade = 0.60, shadow_sway = 0.50, shadow_smooth = 0.65,
        shadow_tint_r = 0.10, shadow_tint_g = 0.14, shadow_tint_b = 0.38, shadow_night = 1, shadow_hd = 1, shadow_skip_nightmare = 1,
        glint_str = 0.45, glint_density = 0.92, glint_grain = 0.85, glint_soft = 0.88,
        exposure = 0.05, temp = 0.22, tint = -0.02, contrast = 0.03, lightness = 0.00, gamma = 1.00,
        sat = 1.04, vibrance = 0.22, filmic = 0.28, hl_desat = 0.35, sun_fill = 0.25, original_mix = 0.00,
        vignette = 0.06, grain = 0.00,
        vanilla_cc = 0.80, snow_cap = 1.00, sanity_color = 0, distort_free = 0, sand_filter = 0,
    }),
}

-- 3 Player Self-Save Custom Preset Slots (persisted independently as hypersmooth_slot_1..3.cfg)
local custom_slots = { [1] = nil, [2] = nil, [3] = nil }

local function ClampTuneValue(key, v)
    local b = TUNE_BOUNDS[key]
    if not b then return 0 end
    if b.is_bool then
        if v == true or (type(v) == "number" and v > 0.5) then return 1 end
        return 0
    end
    if type(v) ~= "number" then return b.default end
    if v < b.min then v = b.min end
    if v > b.max then v = b.max end
    if b.prec == 3 then
        return math.floor(v * 1000 + 0.5) * 0.001
    end
    return math.floor(v * 100 + 0.5) * 0.01
end

local function GetActiveVisTune()
    if current_visual_preset == 3 then
        return vis_tune
    end
    return PRESET_TUNE[current_visual_preset] or PRESET_TUNE[0]
end
local current_skeletal_tween = SKELETAL_TWEEN and true or false
local current_low_latency = LOW_LATENCY_INPUT and true or false
local current_show_fps_hud = SHOW_FPS_HUD and true or false
local current_mod_lang = "auto"
local current_hud_mode = HUD_MODE
local fps_hud_text = nil
local hud_dirty_now = false
local visual_bench_timer = 0
local visual_bench_pinned = false

local function EncodeHudMode(m)
    return (m == "detail") and 1 or 0
end
local function DecodeHudMode(v)
    return (v == 1) and "detail" or "simple"
end

local function IsGameChineseLocale()
    local lt = rawget(GLOBAL, "LanguageTranslator")
    if type(lt) == "table" and type(lt.defaultlang) == "string" and #lt.defaultlang > 0 then
        local dl = lt.defaultlang
        if dl == "zh" or dl == "zhr" or dl == "zht" then
            return true
        end
        if dl ~= "en" then
            return false
        end
    end
    local strings = rawget(GLOBAL, "STRINGS")
    local opt_str = type(strings) == "table" and type(strings.UI) == "table" and strings.UI.OPTIONS or nil
    if type(opt_str) == "table" and type(opt_str.ENABLED) == "string" then
        return opt_str.ENABLED == "启用" or opt_str.ENABLED == "啟用"
    end
    return false
end

local function ResolveUiIsZh()
    if current_mod_lang == "zh" then return true end
    if current_mod_lang == "en" then return false end
    return IsGameChineseLocale()
end

HS_IsUiZh = ResolveUiIsZh

local function IsRenderBackendOpenGL(val)
    return val == true or val == "opengl"
end

local latency_stats = {
    wasd_instant_rpcs = 0,
    walk_cancels      = 0,
    flick_refreshes   = 0,
    flick_rescues     = 0,
    mod_click_rescues = 0,
    cam240_frames     = 0,
    craft_rebuilds    = 0,
    craft_has_saved   = 0,
    craft_ui_deferred = 0,
    followtext_240    = 0,
    host_fastdumps    = 0,
    map_skip          = 0,
    placer_skip       = 0,
    deploy_skip       = 0,
}
local last_hud_avg_fps  = 0
local last_hud_low1_fps = 0

local visual_shaders_ready = false
local user_bloom_enabled = false
local orig_SetBloomEnabled = nil
local hd2d_smooth_wetness = 0.0

-- Reversible vanilla filter hook state (ColourCube strength via identity LUT swap, Sanity/Distortion, Snow Cap)
local vanilla_filter_hooked = false
local orig_SetColourCubeData = nil
local orig_SetColourCubeLerp = nil
local orig_SetDistortionFactor = nil
local orig_MapSetOverlayLerp = nil
local vf_cc_src0 = nil
local vf_cc_dst0 = nil
local vf_cc_lerp0 = nil
local vf_cc_lerp1 = nil
local vf_distort_last = nil
local vf_snow_map = nil
local vf_snow_last = nil
local vf_sandover_widgets = (type(setmetatable) == "function") and setmetatable({}, { __mode = "k" }) or {}
local IDENTITY_CC_TEX = "images/colour_cubes/identity_colourcube.tex"

-- Detect standalone DST-BCAS-Studio Workshop mod so HyperSmooth yields post-process/ColourCube cleanly
local function IsExternalBcasActive()
    if rawget(GLOBAL, "BCAS_State") ~= nil then return true end
    local ppe = rawget(GLOBAL, "PostProcessorEffects")
    if type(ppe) == "table" and (ppe.BCAS_Merged ~= nil or ppe.BCAS_Studio ~= nil or ppe.BCAS_Cinema ~= nil) then
        return true
    end
    return false
end

-- Player-First Presets (0 = 100% Vanilla Pure default; 1 = Eye-Care HD; 2 = Cinematic Storybook; 3 = Custom)
-- Note: dark_clarity & clean_filter stay false in ALL presets unless explicitly opted in by the player!
local VISUAL_PRESET_FLAGS = {
    [0] = { af16 = false, full_lm = false, cas = false, ao = false, env = false, soft_bloom = false, tone = false, mip_sharp = false, dark_clarity = false, clean_filter = false },
    [1] = { af16 = true,  full_lm = true,  cas = true,  ao = true,  env = false, soft_bloom = true,  tone = false, mip_sharp = true,  dark_clarity = false, clean_filter = false },
    [2] = { af16 = true,  full_lm = true,  cas = true,  ao = true,  env = true,  soft_bloom = true,  tone = true,  mip_sharp = true,  dark_clarity = false, clean_filter = false },
}

local function GetActiveVisualFlags()
    if current_visual_preset == 3 then
        return custom_vis_flags
    end
    return VISUAL_PRESET_FLAGS[current_visual_preset] or VISUAL_PRESET_FLAGS[0]
end

local function EncodeCustomVisMask(fl)
    return (fl.af16 and 1 or 0)
        + (fl.full_lm and 2 or 0)
        + (fl.cas and 4 or 0)
        + (fl.ao and 8 or 0)
        + (fl.env and 16 or 0)
        + (fl.soft_bloom and 32 or 0)
        + (fl.tone and 64 or 0)
        + (fl.mip_sharp and 128 or 0)
        + (fl.dark_clarity and 256 or 0)
        + (fl.clean_filter and 512 or 0)
end

local function DecodeCustomVisMask(m)
    if type(m) ~= "number" then return end
    local function has_bit(val, b)
        return math.floor(val / b) % 2 == 1
    end
    custom_vis_flags.af16         = has_bit(m, 1)
    custom_vis_flags.full_lm      = has_bit(m, 2)
    custom_vis_flags.cas          = has_bit(m, 4)
    custom_vis_flags.ao           = has_bit(m, 8)
    custom_vis_flags.env          = has_bit(m, 16)
    custom_vis_flags.soft_bloom   = has_bit(m, 32)
    custom_vis_flags.tone         = has_bit(m, 64)
    custom_vis_flags.mip_sharp    = has_bit(m, 128)
    custom_vis_flags.dark_clarity = has_bit(m, 256)
    custom_vis_flags.clean_filter = has_bit(m, 512)
end

local function GetEffectiveVanillaCc()
    if IsExternalBcasActive() or current_visual_preset == 0 then return 1.0 end
    local tn = GetActiveVisTune()
    return ClampTuneValue("vanilla_cc", tn.vanilla_cc)
end

local function GetEffectiveSnowCap()
    if IsExternalBcasActive() or current_visual_preset == 0 then return 1.0 end
    local tn = GetActiveVisTune()
    return ClampTuneValue("snow_cap", tn.snow_cap)
end

local function IsSanityColorClean()
    if IsExternalBcasActive() or current_visual_preset == 0 then return false end
    local tn = GetActiveVisTune()
    return (tn.sanity_color or 0) > 0.5
end

local function IsDistortFreeActive()
    if IsExternalBcasActive() or current_visual_preset == 0 then return false end
    local tn = GetActiveVisTune()
    return (tn.distort_free or 0) > 0.5
end

local function IsSandFilterActive()
    if IsExternalBcasActive() or current_visual_preset == 0 then return false end
    local tn = GetActiveVisTune()
    return (tn.sand_filter or 0) > 0.5
end

local function HookWorldSnowMap(world_inst)
    local mp = world_inst and world_inst.Map
    if not mp then return end
    vf_snow_map = mp
    local mt = getmetatable(mp)
    local idx = mt and mt.__index
    if type(idx) == "table" and type(idx.SetOverlayLerp) == "function" and not idx._hs_snow_hooked then
        idx._hs_snow_hooked = true
        orig_MapSetOverlayLerp = idx.SetOverlayLerp
        idx.SetOverlayLerp = function(self, level, ...)
            vf_snow_map = self
            vf_snow_last = level
            local cap = GetEffectiveSnowCap()
            if type(level) == "number" and cap < 0.999 and level > cap then
                level = cap
            end
            return orig_MapSetOverlayLerp(self, level, ...)
        end
    end
end

local function InstallVanillaFilterHooks()
    if vanilla_filter_hooked then return true end
    local pp = rawget(GLOBAL, "PostProcessor")
    if not pp then return false end
    local mt = getmetatable(pp)
    local idx = mt and mt.__index
    if type(idx) ~= "table" then return false end

    if type(idx.SetColourCubeData) == "function" and not idx._hs_ccdata_hooked then
        idx._hs_ccdata_hooked = true
        orig_SetColourCubeData = idx.SetColourCubeData
        idx.SetColourCubeData = function(self, index, src, dest, ...)
            if index == 0 then
                vf_cc_src0 = src
                vf_cc_dst0 = dest
                if GetEffectiveVanillaCc() < 0.999 then
                    -- Swap src to identity LUT so scaling lerp blends Neutral <-> Current LUT
                    -- instead of resurrecting last season's stale LUT! (BCAS-Studio MIT insight)
                    src = IDENTITY_CC_TEX
                end
            end
            return orig_SetColourCubeData(self, index, src, dest, ...)
        end
    end

    if type(idx.SetColourCubeLerp) == "function" and not idx._hs_cclerp_hooked then
        idx._hs_cclerp_hooked = true
        orig_SetColourCubeLerp = idx.SetColourCubeLerp
        idx.SetColourCubeLerp = function(self, index, lerp, ...)
            if index == 0 then
                vf_cc_lerp0 = lerp
                local vcc = GetEffectiveVanillaCc()
                if vcc < 0.999 and type(lerp) == "number" then
                    lerp = lerp * vcc
                end
            elseif index == 1 then
                vf_cc_lerp1 = lerp
                if IsSanityColorClean() then
                    lerp = 0
                end
            end
            return orig_SetColourCubeLerp(self, index, lerp, ...)
        end
    end

    if type(idx.SetDistortionFactor) == "function" and not idx._hs_distort_hooked then
        idx._hs_distort_hooked = true
        orig_SetDistortionFactor = idx.SetDistortionFactor
        idx.SetDistortionFactor = function(self, factor, ...)
            vf_distort_last = factor
            if IsDistortFreeActive() then
                factor = 1.0
            end
            return orig_SetDistortionFactor(self, factor, ...)
        end
    end

    local tw = rawget(GLOBAL, "TheWorld")
    if tw then HookWorldSnowMap(tw) end
    vanilla_filter_hooked = true
    return true
end

local function ApplyVanillaFilters()
    if IsExternalBcasActive() then return end
    local pp = rawget(GLOBAL, "PostProcessor")
    if pp and vanilla_filter_hooked then
        if orig_SetColourCubeData and vf_cc_src0 ~= nil and vf_cc_dst0 ~= nil then
            local src0 = (GetEffectiveVanillaCc() < 0.999) and IDENTITY_CC_TEX or vf_cc_src0
            pcall(orig_SetColourCubeData, pp, 0, src0, vf_cc_dst0)
        end
        if orig_SetColourCubeLerp then
            if vf_cc_lerp0 ~= nil then
                local vcc = GetEffectiveVanillaCc()
                pcall(orig_SetColourCubeLerp, pp, 0, (vcc < 0.999) and (vf_cc_lerp0 * vcc) or vf_cc_lerp0)
            end
            if vf_cc_lerp1 ~= nil then
                pcall(orig_SetColourCubeLerp, pp, 1, IsSanityColorClean() and 0 or vf_cc_lerp1)
            end
        end
        if orig_SetDistortionFactor and vf_distort_last ~= nil then
            pcall(orig_SetDistortionFactor, pp, IsDistortFreeActive() and 1.0 or vf_distort_last)
        end
    end
    local tw = rawget(GLOBAL, "TheWorld")
    if tw and tw.Map and vf_snow_map ~= tw.Map then
        HookWorldSnowMap(tw)
    end
    if orig_MapSetOverlayLerp and vf_snow_map ~= nil and vf_snow_last ~= nil then
        local cap = GetEffectiveSnowCap()
        local lvl = (type(vf_snow_last) == "number" and cap < 0.999 and vf_snow_last > cap) and cap or vf_snow_last
        pcall(orig_MapSetOverlayLerp, vf_snow_map, lvl)
    end
    local hide_sand = IsSandFilterActive()
    for w in pairs(vf_sandover_widgets) do
        if w and w.inst and w.inst.entity then
            if hide_sand then
                if w.shown then pcall(w.inst.entity.Hide, w.inst.entity, false) end
                if w.dust and w.dust.shown and w.dust.inst and w.dust.inst.entity then
                    pcall(w.dust.inst.entity.Hide, w.dust.inst.entity, false)
                end
            else
                if w.shown then pcall(w.inst.entity.Show, w.inst.entity, false) end
                if w.dust and w.dust.shown and w.dust.inst and w.dust.inst.entity then
                    pcall(w.dust.inst.entity.Show, w.dust.inst.entity, false)
                end
            end
        end
    end
end

-- Fluid Shadow & Lighting Engine v2:
-- Continuous WallUpdate dt-driven Sun/Dusk/Moon arc + Multi-Light Energy-Weighted Night Solver
-- + Frame-Rate-Independent Exponential Damping + Shadow-Creature Ethereal Exclusion (preloaded anim_bloom_ghost.ksh)
local studio_grain_clock = 0.0
local studio_wind_clock = 0.0
local studio_engine_bloom_rt = nil
local studio_bloom_sampler_scale = nil
local shadow_excluded_ents = (type(setmetatable) == "function") and setmetatable({}, { __mode = "k" }) or {}
local shadow_exclusion_last_state = nil

local studio_clock_interp = {
    phase = nil,
    last_raw = nil,
    smooth_prog = 0.5,
    rate = 1.0 / 240.0,
    since_tick = 0.0,
}
local studio_nl_cache = {
    accum = 999.0,
    wx = 0.707, wz = 0.707,
    presence = 0.0,
    stretch = 1.75,
}
local studio_light_init = false
local studio_light_state = {
    sun_wx = 0.707, sun_wz = 0.707,
    shad_wx = -0.707, shad_wz = -0.707,
    stretch = 1.35, elev_rad = 0.64,
    theta_cam = 0.785,
    day_factor = 1.0,
    eff_presence = 1.0,
    soft_mult = 1.0,
    sun_u = 0.22, sun_v = 0.14,
    sun_sx = 0.22, sun_sy = 0.14, sun_elev = 0.64,
}

local function Smoothstep01(x)
    if x <= 0.0 then return 0.0 end
    if x >= 1.0 then return 1.0 end
    return x * x * (3.0 - 2.0 * x)
end

local function IsGamePausedSafe()
    local fn = rawget(GLOBAL, "IsPaused")
    if type(fn) == "function" then
        local ok, p = pcall(fn)
        if ok and p then return true end
    end
    return false
end

local function IsShadowExclusionActive()
    if current_visual_preset == 0 or IsExternalBcasActive() then
        return false
    end
    local tn = GetActiveVisTune()
    return ClampTuneValue("shadow_density", tn.shadow_density) > 0.001
        and ClampTuneValue("shadow_skip_nightmare", tn.shadow_skip_nightmare) > 0.5
end

-- Stock engine shader preloaded unconditionally in scripts/prefabs/global.lua:L233 (zero INVALID_RESOURCE_HANDLE risk!).
-- Routes ethereal shadow beings out of anim_bloom shadow projection while dark shadow sprites emit ~0 bloom (bm.b - max(r,g) <= 0).
local function GetNoShadowShaderPath()
    return "shaders/anim_bloom_ghost.ksh"
end

local function ApplyShadowCreatureExclusionOne(inst, active)
    if not (inst and inst.AnimState) then return end
    if inst.IsValid and not inst:IsValid() then return end
    if inst.components and inst.components.bloomer then return end
    local cur = shadow_excluded_ents[inst]
    if active then
        local sh = GetNoShadowShaderPath()
        if sh and cur ~= true then
            pcall(inst.AnimState.SetBloomEffectHandle, inst.AnimState, sh)
            shadow_excluded_ents[inst] = true
        elseif cur == nil then
            shadow_excluded_ents[inst] = false
        end
    else
        if cur == true then
            pcall(inst.AnimState.ClearBloomEffectHandle, inst.AnimState)
        end
        shadow_excluded_ents[inst] = false
    end
end

local function RefreshAllShadowCreatureExclusions()
    local active = IsShadowExclusionActive()
    if shadow_exclusion_last_state == active then
        return
    end
    shadow_exclusion_last_state = active
    for inst in pairs(shadow_excluded_ents) do
        ApplyShadowCreatureExclusionOne(inst, active)
    end
end

local function ComputeWorldLightingState(allow_night_light, dt_step, smooth_lvl)
    local st = studio_light_state
    local world = rawget(GLOBAL, "TheWorld")
    local wstate = world and world.state or nil
    local is_cave = world ~= nil and world.HasTag ~= nil and world:HasTag("cave") or false
    local has_dt = (type(dt_step) == "number" and dt_step > 0.0 and dt_step < 2.0)
    local paused = has_dt and IsGamePausedSafe() or false

    local tgt_day = 0.0
    local tgt_stretch = 1.35
    local tgt_sun_wx, tgt_sun_wz = 0.707, 0.707
    local tgt_soft_mult = 1.0

    if not is_cave and wstate then
        local phase = wstate.phase or "day"
        local raw_prog = type(wstate.timeinphase) == "number" and wstate.timeinphase or 0.5
        if raw_prog < 0.0 then raw_prog = 0.0 elseif raw_prog > 1.0 then raw_prog = 1.0 end

        -- Continuous sub-tick clock extrapolation between 30Hz/60Hz SimTicks for silky 240Hz sun drift!
        local ci = studio_clock_interp
        local prog = raw_prog
        if has_dt then
            if ci.phase ~= phase or ci.last_raw == nil or math.abs(raw_prog - ci.last_raw) > 0.08 then
                ci.phase = phase
                ci.last_raw = raw_prog
                ci.smooth_prog = raw_prog
                ci.rate = 1.0 / 240.0
                ci.since_tick = 0.0
            else
                if not paused then
                    ci.since_tick = ci.since_tick + dt_step
                end
                if raw_prog ~= ci.last_raw then
                    local dp = raw_prog - ci.last_raw
                    if dp > 0.0 and ci.since_tick > 1e-4 then
                        local obs = dp / ci.since_tick
                        if obs > 0.05 then obs = 0.05 end
                        ci.rate = ci.rate * 0.75 + obs * 0.25
                    end
                    ci.last_raw = raw_prog
                    ci.since_tick = 0.0
                end
                if not paused then
                    ci.smooth_prog = ci.smooth_prog + ci.rate * dt_step
                    local pull = 1.0 - math.exp(-dt_step * 7.5)
                    ci.smooth_prog = ci.smooth_prog + (raw_prog - ci.smooth_prog) * pull
                    if ci.smooth_prog < 0.0 then ci.smooth_prog = 0.0 elseif ci.smooth_prog > 1.0 then ci.smooth_prog = 1.0 end
                end
            end
            prog = ci.smooth_prog
        else
            ci.phase = phase
            ci.last_raw = raw_prog
            ci.smooth_prog = raw_prog
            ci.since_tick = 0.0
        end

        if phase == "day" then
            -- Continuous East -> Noon -> West arc (leg: -2.3 -> +2.3)
            local leg = 4.6 * (prog - 0.5)
            local base_len = math.sqrt(leg * leg + 0.7225)
            local inv = 1.0 / math.max(base_len, 1e-4)
            tgt_sun_wx = -leg * inv
            tgt_sun_wz = -0.85 * inv
            tgt_stretch = base_len
            tgt_day = Smoothstep01(prog / 0.045)
            local noon_sin = math.sin(prog * math.pi)
            tgt_soft_mult = 1.0 + 0.16 * (1.0 - noon_sin)
        elseif phase == "dusk" then
            -- Continuous western sunset motion (leg: +2.3 -> +3.8; ZERO dusk freeze!)
            local leg = 2.3 + prog * 1.50
            local base_len = math.sqrt(leg * leg + 0.7225)
            local inv = 1.0 / math.max(base_len, 1e-4)
            tgt_sun_wx = -leg * inv
            tgt_sun_wz = -0.85 * inv
            tgt_stretch = math.min(2.95, base_len)
            local s = Smoothstep01(1.0 - prog)
            tgt_day = s
            tgt_soft_mult = 1.16 + 0.28 * (1.0 - s)
        elseif phase == "night" and wstate.isfullmoon then
            -- Continuous full-moon arc across the night sky
            local leg = 3.2 * (prog - 0.5)
            local base_len = math.sqrt(leg * leg + 0.81)
            local inv = 1.0 / math.max(base_len, 1e-4)
            tgt_sun_wx = -leg * inv
            tgt_sun_wz = -0.90 * inv
            tgt_stretch = math.min(2.40, base_len)
            local sp = math.sin(prog * math.pi)
            tgt_day = 0.68 * sp * math.sqrt(math.max(sp, 0.0))
            tgt_soft_mult = 1.28
        else
            tgt_day = 0.0
            tgt_stretch = 1.75
            tgt_soft_mult = 1.32
        end
    elseif not wstate then
        tgt_day = 1.0
        tgt_stretch = 1.35
        tgt_sun_wx, tgt_sun_wz = 0.707, 0.707
        tgt_soft_mult = 1.0
    else
        -- Cave: no sun, smooth handoff to nearby campfires/lanterns/torches
        tgt_day = 0.0
        tgt_stretch = 1.75
        tgt_soft_mult = 1.32
    end

    -- Multi-Light Energy-Weighted Night/Cave Solver (20Hz entity scan + 240Hz dt damping -> ZERO 180-deg flips!)
    local nlc = studio_nl_cache
    if allow_night_light and tgt_day < 0.90 then
        nlc.accum = has_dt and (nlc.accum + dt_step) or 999.0
        if nlc.accum >= 0.05 then
            nlc.accum = 0.0
            local sum_wx, sum_wz, sum_w, sum_dist_w = 0.0, 0.0, 0.0, 0.0
            local player = rawget(GLOBAL, "ThePlayer")
            local sim = rawget(GLOBAL, "TheSim")
            if player and player.Transform and sim and sim.FindEntities then
                local ok_p, px, _, pz = pcall(player.Transform.GetWorldPosition, player.Transform)
                if ok_p and type(px) == "number" and type(pz) == "number" then
                    local ok_f, ents = pcall(sim.FindEntities, sim, px, 0, pz, 18)
                    if ok_f and type(ents) == "table" then
                        for i = 1, #ents do
                            local e = ents[i]
                            if e and e ~= player and e.Light and e.Transform then
                                local ok_e, en = pcall(e.Light.IsEnabled, e.Light)
                                if ok_e and en then
                                    local ok_r, rad = pcall(e.Light.GetCalculatedRadius, e.Light)
                                    if not (ok_r and type(rad) == "number") then
                                        ok_r, rad = pcall(e.Light.GetRadius, e.Light)
                                    end
                                    rad = (ok_r and type(rad) == "number" and rad > 0.5) and rad or 0.0
                                    if rad > 0.5 then
                                        local ok_ep, ex, _, ez = pcall(e.Transform.GetWorldPosition, e.Transform)
                                        if ok_ep and type(ex) == "number" then
                                            local dx, dz = ex - px, ez - pz
                                            local d2 = dx * dx + dz * dz
                                            if d2 > 0.04 then
                                                local dl = math.sqrt(d2)
                                                local w = (rad * rad) / (d2 + 4.0)
                                                sum_wx = sum_wx + (dx / dl) * w
                                                sum_wz = sum_wz + (dz / dl) * w
                                                sum_w  = sum_w + w
                                                sum_dist_w = sum_dist_w + math.min(dl / math.max(rad, 1.0), 2.2) * w
                                            end
                                        end
                                    end
                                end
                            end
                        end
                    end
                end
            end
            local vlen = math.sqrt(sum_wx * sum_wx + sum_wz * sum_wz)
            if sum_w > 0.06 and vlen > 0.02 then
                local coh = math.min(1.0, vlen / math.max(sum_w, 1e-4))
                nlc.wx = sum_wx / vlen
                nlc.wz = sum_wz / vlen
                nlc.presence = math.min(1.0, sum_w * 0.45) * (0.25 + 0.75 * coh)
                local avg_d = sum_dist_w / math.max(sum_w, 1e-4)
                nlc.stretch = 1.35 + math.min(avg_d, 1.8) * 0.48
            else
                nlc.presence = 0.0
            end
        end
    else
        nlc.presence = 0.0
        nlc.accum = 999.0
    end

    local mix_w = Smoothstep01(tgt_day)
    local nl_w = (1.0 - mix_w) * (nlc.presence > 0.01 and 1.0 or 0.0)
    local lx = tgt_sun_wx * mix_w + nlc.wx * nl_w
    local lz = tgt_sun_wz * mix_w + nlc.wz * nl_w
    local llen = math.sqrt(lx * lx + lz * lz)
    if llen > 1e-4 then
        lx, lz = lx / llen, lz / llen
    else
        lx, lz = tgt_sun_wx, tgt_sun_wz
    end
    local tgt_eff_stretch = tgt_stretch * mix_w + nlc.stretch * (1.0 - mix_w)
    if nl_w <= 0.001 then tgt_eff_stretch = tgt_stretch end
    local tgt_eff_presence = math.max(tgt_day, nlc.presence)
    local tgt_eff_soft = tgt_soft_mult * mix_w + 1.32 * (1.0 - mix_w)

    -- Frame-rate-independent exponential damping across ALL geometric light & shadow attributes!
    local sm = (type(smooth_lvl) == "number") and smooth_lvl or 0.65
    if sm < 0.0 then sm = 0.0 elseif sm > 1.0 then sm = 1.0 end
    local use_damp = studio_light_init and has_dt and (not paused)
    if use_damp then
        local rate = 2.2 + (1.0 - sm) * (1.0 - sm) * 20.0
        local a = 1.0 - math.exp(-dt_step * rate)
        -- True circular angular damping (2D SLERP): constant smooth rotation with zero 180-deg chord stall!
        local cur_ang = math.atan2(st.sun_wz, st.sun_wx)
        local tgt_ang = math.atan2(lz, lx)
        local dang = math.atan2(math.sin(tgt_ang - cur_ang), math.cos(tgt_ang - cur_ang))
        local new_ang = cur_ang + dang * a
        st.sun_wx = math.cos(new_ang)
        st.sun_wz = math.sin(new_ang)
        st.stretch      = st.stretch + (tgt_eff_stretch - st.stretch) * a
        st.day_factor   = st.day_factor + (tgt_day - st.day_factor) * a
        st.eff_presence = st.eff_presence + (tgt_eff_presence - st.eff_presence) * a
        st.soft_mult    = st.soft_mult + (tgt_eff_soft - st.soft_mult) * a
    else
        st.sun_wx       = lx
        st.sun_wz       = lz
        st.stretch      = tgt_eff_stretch
        st.day_factor   = tgt_day
        st.eff_presence = tgt_eff_presence
        st.soft_mult    = tgt_eff_soft
        studio_light_init = true
    end

    -- Project damped light vector into camera-space azimuth theta_cam (shortest-arc damped for silky Q/E rotation!)
    local tgt_theta = 0.785
    local cam = rawget(GLOBAL, "TheCamera")
    if cam and cam.GetRightVec and cam.GetDownVec then
        local ok_r, rv = pcall(cam.GetRightVec, cam)
        local ok_d, dv = pcall(cam.GetDownVec, cam)
        if ok_r and ok_d and rv and dv and type(rv.x) == "number" and type(dv.x) == "number" then
            local rx, rz = rv.x, rv.z or 0
            local rlen = math.sqrt(rx * rx + rz * rz)
            if rlen > 1e-4 then rx, rz = rx / rlen, rz / rlen end
            local bx, bz = dv.x, dv.z or 0
            local blen = math.sqrt(bx * bx + bz * bz)
            if blen > 1e-4 then bx, bz = bx / blen, bz / blen end
            local cx = st.sun_wx * rx + st.sun_wz * rz
            local cf = st.sun_wx * bx + st.sun_wz * bz
            tgt_theta = math.atan2(cf, cx)
        end
    end
    if use_damp then
        local cam_rate = (2.2 + (1.0 - sm) * (1.0 - sm) * 20.0) * 1.65
        local ac = 1.0 - math.exp(-dt_step * cam_rate)
        local dth = math.atan2(math.sin(tgt_theta - st.theta_cam), math.cos(tgt_theta - st.theta_cam))
        st.theta_cam = st.theta_cam + dth * ac
    else
        st.theta_cam = tgt_theta
    end

    st.shad_wx  = -st.sun_wx
    st.shad_wz  = -st.sun_wz
    st.elev_rad = math.atan(1.0 / math.max(st.stretch, 0.40))
    st.sun_u    = math.max(0.05, math.min(0.95, 0.5 + math.cos(st.theta_cam) * 0.36))
    st.sun_v    = math.max(0.05, math.min(0.65, 0.20 + math.sin(st.theta_cam) * 0.14))
    st.sun_sx   = st.sun_u
    st.sun_sy   = st.sun_v
    st.sun_elev = st.elev_rad
    return st
end

local function HasNativeWorldStudio()
    return type(rawget(GLOBAL, "HyperSmooth_SetWorldStudio")) == "function"
end

local function SyncBloomSamplerResolution(pp)
    if not (pp and type(pp.SetBloomSamplerParams) == "function") then return end
    local SS = rawget(GLOBAL, "SamplerSizes")
    local SCM = rawget(GLOBAL, "SamplerColourMode")
    if not (SS and SCM) then return end
    local tn = GetActiveVisTune()
    local want_scale = (ClampTuneValue("shadow_hd", tn.shadow_hd) > 0.5) and 1.0 or 0.5
    if studio_bloom_sampler_scale ~= want_scale then
        studio_bloom_sampler_scale = want_scale
        pcall(pp.SetBloomSamplerParams, pp, SS.Relative, want_scale, want_scale, SCM.RGB)
    end
end

-- Zero-allocation independent World/ColourCube/Lighting/3D-Relief/Ocean/Shadow Shader uniform sync:
-- When all strengths are 0.0 -> bit-for-bit identical vanilla fragment output!
local function PushHd2dEnvToNative(dt_step, precomputed_lst)
    local fl = GetActiveVisualFlags()
    local tn = GetActiveVisTune()
    local is_van = (current_visual_preset == 0)

    local fnHd2d = rawget(GLOBAL, "HyperSmooth_SetHd2d")
    if type(fnHd2d) == "function" then
        local ao_str       = fl.ao and ClampTuneValue("ao_str", tn.ao_str) or 0.0
        local env_str      = fl.env and ClampTuneValue("env_str", tn.env_str) or 0.0
        local dark_str     = fl.dark_clarity and ClampTuneValue("dark_str", tn.dark_str) or 0.0
        local light_dither = (fl.full_lm or (fl.cas and ClampTuneValue("dither", tn.dither) > 0.001)) and 1.0 or 0.0
        if ao_str <= 0.001 and env_str <= 0.001 and dark_str <= 0.001 and light_dither <= 0.001 then
            pcall(fnHd2d, 0.0, 0.0, 0.0, 0.70, 0.707, 0.707, 0.0, -1.0, 1.0, 0.94, 0.82, 0.0)
        else
            local target_wet = 0.0
            if env_str > 0.001 then
                local world = rawget(GLOBAL, "TheWorld")
                local state = world and world.state or nil
                local is_cave = world ~= nil and world.HasTag ~= nil and world:HasTag("cave") or false
                if not is_cave and state then
                    if state.israining then
                        local rate = type(state.precipitationrate) == "number" and state.precipitationrate or 0.6
                        target_wet = math.min(1.0, math.max(0.35, rate))
                    elseif type(state.wetness) == "number" and state.wetness > 5 then
                        target_wet = math.min(1.0, state.wetness / 60.0)
                    end
                end
            end
            local step = (type(dt_step) == "number" and dt_step > 0) and (1.0 - math.exp(-dt_step * 0.55)) or 1.0
            hd2d_smooth_wetness = hd2d_smooth_wetness + (target_wet - hd2d_smooth_wetness) * step
            pcall(fnHd2d, ao_str, env_str, dark_str, 0.70, 0.707, 0.707, light_dither, -1.0, 1.0, 0.94, 0.82, hd2d_smooth_wetness)
        end
    end

    -- Native C++ World Studio Bus: 3D Entity Surface Lighting + Procedural Ocean Caustics + Fluid Ground Shadows v2
    local fnWs = rawget(GLOBAL, "HyperSmooth_SetWorldStudio")
    local fnSd = rawget(GLOBAL, "HyperSmooth_SetShadowDyn")
    if type(fnWs) == "function" then
        if is_van or IsExternalBcasActive() then
            pcall(fnWs, 0.0, 0.5, 0.5, 0.5, 0.5, 0.785, 0.75, 0.0, 0.0, 0.92, 0.85, 0.85, -0.707, -0.707, 1.35, 0.0)
            if type(fnSd) == "function" then
                pcall(fnSd, studio_wind_clock, 0.0, 0.0, 1.0)
            end
        else
            local sl_str = ClampTuneValue("surface_light", tn.surface_light)
            local sh_den = ClampTuneValue("shadow_density", tn.shadow_density)
            local sh_len = ClampTuneValue("shadow_length", tn.shadow_length)
            local gl_str = ClampTuneValue("glint_str", tn.glint_str)
            local allow_nl = (tn.shadow_night or 0) > 0.5
            local lst = precomputed_lst or ComputeWorldLightingState(allow_nl, dt_step, ClampTuneValue("shadow_smooth", tn.shadow_smooth))
            local proj_on = (sh_den > 0.001 and lst.eff_presence > 0.005) and 1.0 or 0.0
            local eff_stretch = math.max(0.35, math.min(4.20, lst.stretch * sh_len))
            pcall(fnWs,
                sl_str,
                ClampTuneValue("shade_warm", tn.shade_warm),
                ClampTuneValue("shade_contrast", tn.shade_contrast),
                ClampTuneValue("shade_cool", tn.shade_cool),
                ClampTuneValue("shade_dark", tn.shade_dark),
                lst.theta_cam,
                lst.elev_rad,
                lst.eff_presence,
                gl_str,
                ClampTuneValue("glint_density", tn.glint_density),
                ClampTuneValue("glint_grain", tn.glint_grain),
                ClampTuneValue("glint_soft", tn.glint_soft),
                lst.shad_wx,
                lst.shad_wz,
                eff_stretch,
                proj_on)
            if type(fnSd) == "function" then
                pcall(fnSd,
                    studio_wind_clock,
                    ClampTuneValue("shadow_sway", tn.shadow_sway),
                    ClampTuneValue("shadow_fade", tn.shadow_fade),
                    lst.soft_mult or 1.0)
            end
        end
    end
end

local function ApplyVisualPostProcess()
    local pp = rawget(GLOBAL, "PostProcessor")
    local ppe = rawget(GLOBAL, "PostProcessorEffects")
    local uv = rawget(GLOBAL, "UniformVariables")
    ApplyVanillaFilters()
    if not (visual_shaders_ready and pp and ppe and uv) then
        return
    end
    -- Auto-yield post-processing when standalone DST-BCAS-Studio is co-loaded (prevents double-sharpen/bloom!)
    if IsExternalBcasActive() then
        RefreshAllShadowCreatureExclusions()
        if ppe.HS_VisualFinal ~= nil then
            pcall(pp.EnablePostProcessEffect, pp, ppe.HS_VisualFinal, false)
        end
        if ppe.HS_Bloom ~= nil then
            pcall(pp.EnablePostProcessEffect, pp, ppe.HS_Bloom, false)
        end
        return
    end

    local fl = GetActiveVisualFlags()
    local tn = GetActiveVisTune()
    local is_van = (current_visual_preset == 0)

    local allow_nl = (tn.shadow_night or 0) > 0.5
    local lst = ComputeWorldLightingState(allow_nl, nil, ClampTuneValue("shadow_smooth", tn.shadow_smooth))
    SyncBloomSamplerResolution(pp)
    RefreshAllShadowCreatureExclusions()

    -- 1. 100% parameter-driven Multi-Level Glow Pyramid & Atmospheric FX (zero hardcodes!)
    local bloom_str      = ClampTuneValue("bloom_str", tn.bloom_str)
    local bloom_knee     = ClampTuneValue("bloom_knee", tn.bloom_knee)
    local bloom_chr      = ClampTuneValue("bloom_chr", tn.bloom_chr)
    local bloom_prot     = ClampTuneValue("bloom_prot", tn.bloom_prot)
    local bloom_thresh   = ClampTuneValue("bloom_thresh", tn.bloom_thresh)
    local bloom_warmth   = ClampTuneValue("bloom_warmth", tn.bloom_warmth)
    local bloom_spread   = ClampTuneValue("bloom_spread", tn.bloom_spread)
    local glow_tail      = ClampTuneValue("glow_tail", tn.glow_tail)
    local light_wrap     = ClampTuneValue("light_wrap", tn.light_wrap)
    local glow_rim       = ClampTuneValue("glow_rim", tn.glow_rim)
    local god_rays       = ClampTuneValue("god_rays", tn.god_rays)
    local bloom_compress = ClampTuneValue("bloom_compress", tn.bloom_compress)

    -- 2. 100% parameter-driven BCAS Pro Sharpening + Ink Deconv + Bilateral + AURA
    local bcas_val      = fl.cas and (ClampTuneValue("sharp", tn.sharp) * 5.20) or 0.0
    local deconv_val    = fl.cas and (ClampTuneValue("ink", tn.ink) * 1.25) or 0.0
    local dither_val    = fl.cas and ClampTuneValue("dither", tn.dither) or 0.0
    local anti_ring     = ClampTuneValue("anti_ring", tn.anti_ring)
    local dark_prot     = ClampTuneValue("dark_prot", tn.dark_prot)
    local chroma_prot   = ClampTuneValue("chroma_prot", tn.chroma_prot)
    local noise_reduce  = ClampTuneValue("noise_reduce", tn.noise_reduce)
    local deconv_gate   = ClampTuneValue("deconv_gate", tn.deconv_gate)
    local deconv_penetr = ClampTuneValue("deconv_penetr", tn.deconv_penetr)
    local range_sigma   = ClampTuneValue("range_sigma", tn.range_sigma)
    local noise_floor   = ClampTuneValue("noise_floor", tn.noise_floor)
    local ar_thresh     = ClampTuneValue("ar_thresh", tn.ar_thresh)
    local ar_l_over     = ClampTuneValue("ar_l_over", tn.ar_l_over)
    local ar_d_over     = ClampTuneValue("ar_d_over", tn.ar_d_over)
    local spatial_sigma = ClampTuneValue("spatial_sigma", tn.spatial_sigma)

    -- 3. 100% parameter-driven Single-Pass Cinematic Grading + ASC CDL + Film Atmo
    local exp_ev     = fl.tone and ClampTuneValue("exposure", tn.exposure) or 0.0
    local wb_temp    = fl.tone and ClampTuneValue("temp", tn.temp) or 0.0
    local wb_tint    = fl.tone and ClampTuneValue("tint", tn.tint) or 0.0
    local sat_val    = fl.tone and ClampTuneValue("sat", tn.sat) or 1.0
    local vib_val    = fl.tone and ClampTuneValue("vibrance", tn.vibrance) or 0.0
    local con_val    = fl.tone and ClampTuneValue("contrast", tn.contrast) or 0.0
    local film_val   = fl.tone and ClampTuneValue("filmic", tn.filmic) or 0.0
    local hl_desat   = fl.tone and ClampTuneValue("hl_desat", tn.hl_desat) or 0.0
    local light_o    = fl.tone and ClampTuneValue("lightness", tn.lightness) or 0.0
    local gam_val    = fl.tone and ClampTuneValue("gamma", tn.gamma) or 1.0
    local sun_fill   = fl.tone and ClampTuneValue("sun_fill", tn.sun_fill) or 0.0
    local orig_mix   = fl.tone and ClampTuneValue("original_mix", tn.original_mix) or 0.0
    local slope_r    = fl.tone and ClampTuneValue("slope_r", tn.slope_r) or 1.0
    local slope_g    = fl.tone and ClampTuneValue("slope_g", tn.slope_g) or 1.0
    local slope_b    = fl.tone and ClampTuneValue("slope_b", tn.slope_b) or 1.0
    local offset_r   = fl.tone and ClampTuneValue("offset_r", tn.offset_r) or 0.0
    local offset_g   = fl.tone and ClampTuneValue("offset_g", tn.offset_g) or 0.0
    local offset_b   = fl.tone and ClampTuneValue("offset_b", tn.offset_b) or 0.0
    local power_r    = fl.tone and ClampTuneValue("power_r", tn.power_r) or 1.0
    local power_g    = fl.tone and ClampTuneValue("power_g", tn.power_g) or 1.0
    local power_b    = fl.tone and ClampTuneValue("power_b", tn.power_b) or 1.0
    local vig_val    = fl.tone and ClampTuneValue("vignette", tn.vignette) or 0.0
    local grain_val  = fl.tone and ClampTuneValue("grain", tn.grain) or 0.0

    -- 4. Native Ground Shadow Composite (Contract v17 blue marker in BloomSampler)
    local has_native_ws = HasNativeWorldStudio()
    local shadow_raw = (not is_van) and ClampTuneValue("shadow_density", tn.shadow_density) or 0.0
    local shadow_eff = (has_native_ws and shadow_raw > 0.001) and (shadow_raw * lst.eff_presence) or 0.0
    local shadow_soft = math.max(0.40, math.min(2.00, ClampTuneValue("shadow_soft", tn.shadow_soft) * (lst.soft_mult or 1.0)))
    local shad_r = ClampTuneValue("shadow_tint_r", tn.shadow_tint_r)
    local shad_g = ClampTuneValue("shadow_tint_g", tn.shadow_tint_g)
    local shad_b = ClampTuneValue("shadow_tint_b", tn.shadow_tint_b)

    local cdl_act = math.abs(slope_r - 1.0) + math.abs(slope_g - 1.0) + math.abs(slope_b - 1.0)
        + math.abs(offset_r) + math.abs(offset_g) + math.abs(offset_b)
        + math.abs(power_r - 1.0) + math.abs(power_g - 1.0) + math.abs(power_b - 1.0)
    local has_sharp = (bcas_val > 0.001) or (deconv_val > 0.001) or (dither_val > 0.001)
    local has_grade = (math.abs(exp_ev) + math.abs(wb_temp) + math.abs(wb_tint)
        + math.abs(sat_val - 1.0) + math.abs(vib_val) + math.abs(con_val)
        + film_val + hl_desat + math.abs(light_o) + math.abs(gam_val - 1.0)
        + sun_fill + vig_val + grain_val + cdl_act) > 0.001
    local use_final = has_sharp or has_grade or (shadow_eff > 0.001)

    if uv.HS_BLOOM_PARAMS then pcall(pp.SetUniformVariable, pp, uv.HS_BLOOM_PARAMS, bloom_str, bloom_knee, bloom_chr, bloom_prot) end
    if uv.HS_BLOOM_EX     then pcall(pp.SetUniformVariable, pp, uv.HS_BLOOM_EX, bloom_thresh, bloom_warmth, bloom_spread, glow_tail) end
    if uv.HS_BLOOM_FX     then pcall(pp.SetUniformVariable, pp, uv.HS_BLOOM_FX, light_wrap, glow_rim, god_rays, bloom_compress) end
    if uv.HS_BLOOM_SUN    then pcall(pp.SetUniformVariable, pp, uv.HS_BLOOM_SUN, lst.sun_u, lst.sun_v, lst.day_factor, 0.0) end

    if uv.HS_VISUAL_PARAMS then pcall(pp.SetUniformVariable, pp, uv.HS_VISUAL_PARAMS, bcas_val, deconv_val, dither_val, vig_val) end
    if uv.HS_SHARP_EX      then pcall(pp.SetUniformVariable, pp, uv.HS_SHARP_EX, anti_ring, dark_prot, chroma_prot, noise_reduce) end
    if uv.HS_SHARP_PRO     then pcall(pp.SetUniformVariable, pp, uv.HS_SHARP_PRO, deconv_gate, deconv_penetr, range_sigma, noise_floor) end
    if uv.HS_AURA_EX       then pcall(pp.SetUniformVariable, pp, uv.HS_AURA_EX, ar_thresh, ar_l_over, ar_d_over, spatial_sigma) end
    if uv.HS_GRADE_A       then pcall(pp.SetUniformVariable, pp, uv.HS_GRADE_A, exp_ev, wb_temp, wb_tint, sat_val) end
    if uv.HS_GRADE_B       then pcall(pp.SetUniformVariable, pp, uv.HS_GRADE_B, vib_val, con_val, film_val, hl_desat) end
    if uv.HS_GRADE_C       then pcall(pp.SetUniformVariable, pp, uv.HS_GRADE_C, light_o, gam_val, sun_fill, orig_mix) end
    if uv.HS_CDL_S         then pcall(pp.SetUniformVariable, pp, uv.HS_CDL_S, slope_r, slope_g, slope_b, studio_grain_clock) end
    if uv.HS_CDL_O         then pcall(pp.SetUniformVariable, pp, uv.HS_CDL_O, offset_r, offset_g, offset_b, shadow_soft) end
    if uv.HS_CDL_P         then pcall(pp.SetUniformVariable, pp, uv.HS_CDL_P, power_r, power_g, power_b, grain_val) end
    if uv.HS_SHADOW_CFG    then pcall(pp.SetUniformVariable, pp, uv.HS_SHADOW_CFG, shad_r, shad_g, shad_b, shadow_eff) end

    -- Populate BloomSampler silently when projected shadows are active even if player turned game Bloom OFF
    -- (keeps BOTH ppe.Bloom and ppe.HS_Bloom OFF so zero unwanted bloom appears, and restores exact vanilla state when OFF!)
    local want_shadow_rt = (shadow_eff > 0.001)
    local need_engine_rt = user_bloom_enabled or want_shadow_rt
    if orig_SetBloomEnabled and studio_engine_bloom_rt ~= need_engine_rt then
        studio_engine_bloom_rt = need_engine_rt
        pcall(orig_SetBloomEnabled, pp, need_engine_rt)
    end

    -- Mutual exclusion: never forces Bloom ON if player disabled game Bloom; restores vanilla Bloom when OFF!
    if ppe.Bloom ~= nil and ppe.HS_Bloom ~= nil then
        local use_hs_bloom = fl.soft_bloom and user_bloom_enabled
        local use_vanilla_bloom = (not fl.soft_bloom) and user_bloom_enabled
        pcall(pp.EnablePostProcessEffect, pp, ppe.Bloom, use_vanilla_bloom)
        pcall(pp.EnablePostProcessEffect, pp, ppe.HS_Bloom, use_hs_bloom)
    end
    if ppe.HS_VisualFinal ~= nil then
        pcall(pp.EnablePostProcessEffect, pp, ppe.HS_VisualFinal, use_final)
    end
end

-- Zero-allocation 240Hz per-frame PostProcess dynamic uniform sync (sun, soft penumbra, shadow presence, film grain)
local function PushDynamicPostUniforms(lst)
    local pp = rawget(GLOBAL, "PostProcessor")
    local ppe = rawget(GLOBAL, "PostProcessorEffects")
    local uv = rawget(GLOBAL, "UniformVariables")
    if not (visual_shaders_ready and pp and ppe and uv) then
        return
    end
    local fl = GetActiveVisualFlags()
    local tn = GetActiveVisTune()
    local is_van = (current_visual_preset == 0)
    local has_native_ws = HasNativeWorldStudio()
    local shadow_raw = (not is_van) and ClampTuneValue("shadow_density", tn.shadow_density) or 0.0
    local shadow_eff = (has_native_ws and shadow_raw > 0.001) and (shadow_raw * (lst.eff_presence or 0.0)) or 0.0
    local shadow_soft = math.max(0.40, math.min(2.00, ClampTuneValue("shadow_soft", tn.shadow_soft) * (lst.soft_mult or 1.0)))
    local shad_r = ClampTuneValue("shadow_tint_r", tn.shadow_tint_r)
    local shad_g = ClampTuneValue("shadow_tint_g", tn.shadow_tint_g)
    local shad_b = ClampTuneValue("shadow_tint_b", tn.shadow_tint_b)
    local offset_r = fl.tone and ClampTuneValue("offset_r", tn.offset_r) or 0.0
    local offset_g = fl.tone and ClampTuneValue("offset_g", tn.offset_g) or 0.0
    local offset_b = fl.tone and ClampTuneValue("offset_b", tn.offset_b) or 0.0

    if uv.HS_BLOOM_SUN  then pcall(pp.SetUniformVariable, pp, uv.HS_BLOOM_SUN, lst.sun_u, lst.sun_v, lst.day_factor, 0.0) end
    if uv.HS_CDL_O      then pcall(pp.SetUniformVariable, pp, uv.HS_CDL_O, offset_r, offset_g, offset_b, shadow_soft) end
    if uv.HS_SHADOW_CFG then pcall(pp.SetUniformVariable, pp, uv.HS_SHADOW_CFG, shad_r, shad_g, shad_b, shadow_eff) end

    if fl.tone and ClampTuneValue("grain", tn.grain) > 0.001 then
        local slope_r = ClampTuneValue("slope_r", tn.slope_r)
        local slope_g = ClampTuneValue("slope_g", tn.slope_g)
        local slope_b = ClampTuneValue("slope_b", tn.slope_b)
        if uv.HS_CDL_S then pcall(pp.SetUniformVariable, pp, uv.HS_CDL_S, slope_r, slope_g, slope_b, studio_grain_clock) end
    end

    -- Silently toggle BloomSampler RT only when shadow visibility actually crosses 0 <-> active
    local want_shadow_rt = (shadow_eff > 0.001)
    local need_engine_rt = user_bloom_enabled or want_shadow_rt
    if orig_SetBloomEnabled and studio_engine_bloom_rt ~= need_engine_rt then
        studio_engine_bloom_rt = need_engine_rt
        pcall(orig_SetBloomEnabled, pp, need_engine_rt)
        ApplyVisualPostProcess()
    end
end

local function TickVisualStudioDynamic(dt_step)
    local has_dt = (type(dt_step) == "number" and dt_step > 0.0 and dt_step < 0.5)
    local paused = has_dt and IsGamePausedSafe() or false
    if has_dt and not paused then
        studio_wind_clock = (studio_wind_clock + dt_step) % 6283.18530718
    end

    local tn = GetActiveVisTune()
    local allow_nl = (tn.shadow_night or 0) > 0.5
    local smooth_lvl = ClampTuneValue("shadow_smooth", tn.shadow_smooth)
    -- Single continuous 240Hz dt-driven lighting update shared by C++ shaders, PostFX, and HS_EXT_V1 plugins
    local lst = ComputeWorldLightingState(allow_nl, has_dt and dt_step or nil, smooth_lvl)

    PushHd2dEnvToNative(dt_step, lst)
    if type(HS_ExtRuntime) == "table" and type(HS_ExtRuntime.Tick) == "function" then
        HS_ExtRuntime.Tick(dt_step, lst)
    end
    if current_visual_preset == 0 or IsExternalBcasActive() then
        return
    end
    local fl = GetActiveVisualFlags()
    if fl.tone and ClampTuneValue("grain", tn.grain) > 0.001 and not paused then
        studio_grain_clock = (studio_grain_clock + (has_dt and (dt_step * 60.0) or 1.0)) % 256.0
    end
    if ClampTuneValue("shadow_density", tn.shadow_density) > 0.001
        or (fl.soft_bloom and ClampTuneValue("god_rays", tn.god_rays) > 0.001)
        or (fl.tone and ClampTuneValue("grain", tn.grain) > 0.001) then
        PushDynamicPostUniforms(lst)
    end
end

local function HasNativePlugin()
    return type(rawget(GLOBAL, "HyperSmooth_SetFps")) == "function"
end

-- Minimum required native bin64/dinput8.dll semantic version (decoupled from Lua hotfix suffix e.g. 2.8.0.1)
local EXPECTED_DLL_VERSION = "2.8.0"

local function ParseSemVer(str)
    if type(str) ~= "string" then return nil, nil end
    local maj, min, pat = str:match("(%d+)%.(%d+)%.(%d+)")
    if not maj then return nil, nil end
    return tonumber(maj) * 10000 + tonumber(min) * 100 + tonumber(pat),
           string.format("%s.%s.%s", maj, min, pat)
end

local function GetDllVersionStatus()
    if not HasNativePlugin() then
        return "missing", nil, EXPECTED_DLL_VERSION
    end
    local fnVer = rawget(GLOBAL, "HyperSmooth_Version")
    if type(fnVer) ~= "function" then
        -- v2.2.1 ~ v2.5.x (has Lua bridge, predates HyperSmooth_Version export)
        return "outdated", "<2.8.0", EXPECTED_DLL_VERSION
    end
    local ok, raw_ver = pcall(fnVer)
    local dll_num, dll_clean = ParseSemVer(ok and raw_ver or nil)
    local exp_num = ParseSemVer(EXPECTED_DLL_VERSION) or 20800
    if not dll_num then
        return "outdated", "unknown", EXPECTED_DLL_VERSION
    end
    if dll_num < exp_num then
        return "outdated", dll_clean, EXPECTED_DLL_VERSION
    end
    return "ok", dll_clean, EXPECTED_DLL_VERSION
end

rawset(GLOBAL, "HyperSmooth_GetDllStatus", GetDllVersionStatus)

-- Dynamic Lua 5.1 32-bit unsigned bitmask builder for causal 3-channel RPC QoS
local function RegisterDynamicRpcQosMasks()
    local fnSetMasks = rawget(GLOBAL, "HyperSmooth_SetRpcChannelMasks")
    if type(fnSetMasks) ~= "function" then return end

    local srv_non_combat_names = {
        "SetWriteableText", "ToggleController", "OpenGift", "ClosePopup",
        "RecievePopupMessage", "GetChatHistory", "SetSkillActivatedState",
        "AddSkillXP", "OnScrapbookDataTaught", "SetClientAuthoritativeSetting",
    }
    local cli_non_entity_names = {
        "ShowPopup", "RecievePopupMessage", "LearnRecipe", "LearnFoodStats",
        "LearnPlantStage", "LearnFertilizerStage", "TakeOversizedPicture",
        "RecieveChatHistory", "LearnBuilderRecipe", "UpdateAccomplishment",
        "UpdateCountAccomplishment", "SetSkillActivatedState", "AddSkillXP",
    }

    local srv_tbl = rawget(GLOBAL, "RPC")
    local cli_tbl = rawget(GLOBAL, "CLIENT_RPC")
    local srv0, srv1, srv2, cli0 = 0, 0, 0, 0

    if type(srv_tbl) == "table" then
        local seen = {}
        for _, name in ipairs(srv_non_combat_names) do
            local id = srv_tbl[name]
            if type(id) == "number" and id >= 1 and id < 96 and math.floor(id) == id and not seen[id] then
                seen[id] = true
                if id < 32 then
                    srv0 = srv0 + (2 ^ id)
                elseif id < 64 then
                    srv1 = srv1 + (2 ^ (id - 32))
                else
                    srv2 = srv2 + (2 ^ (id - 64))
                end
            end
        end
    end
    if type(cli_tbl) == "table" then
        local seen = {}
        for _, name in ipairs(cli_non_entity_names) do
            local id = cli_tbl[name]
            if type(id) == "number" and id >= 1 and id < 32 and math.floor(id) == id and not seen[id] then
                seen[id] = true
                cli0 = cli0 + (2 ^ id)
            end
        end
    end
    pcall(fnSetMasks, srv0, srv1, srv2, cli0)
end

rawset(GLOBAL, "HyperSmooth_RegisterDynamicRpcQosMasks", RegisterDynamicRpcQosMasks)
RegisterDynamicRpcQosMasks()
AddSimPostInit(RegisterDynamicRpcQosMasks)

local dll_outdated_toast_shown = false
local toast_is_warning = false

local function ApplyHudVisibility()
    if fps_hud_text and fps_hud_text.inst and fps_hud_text.inst:IsValid() then
        if current_show_fps_hud then
            fps_hud_text:Show()
            hud_dirty_now = true
        else
            fps_hud_text:Hide()
        end
    end
end

local function SerializeTuneEntries(parts, tn)
    for i = 1, #TUNE_KEYS do
        local tk = TUNE_KEYS[i]
        local b = TUNE_BOUNDS[tk]
        if b and b.is_bool then
            parts[#parts + 1] = string.format("t_%s=%d", tk, (tn[tk] or 0) > 0.5 and 1 or 0)
        elseif b and b.prec == 3 then
            parts[#parts + 1] = string.format("t_%s=%.3f", tk, ClampTuneValue(tk, tn[tk]))
        else
            parts[#parts + 1] = string.format("t_%s=%.2f", tk, ClampTuneValue(tk, tn[tk]))
        end
    end
end

local function ParseTuneEntries(str, dst_tn)
    for i = 1, #TUNE_KEYS do
        local tk = TUNE_KEYS[i]
        local nv = tonumber(str:match(";t_" .. tk .. "=([%-%d%.]+)"))
        if nv ~= nil then
            dst_tn[tk] = ClampTuneValue(tk, nv)
        end
    end
end

local SyncToNativePlugin

local function SaveUserCustomSlot(slot_idx)
    if type(slot_idx) ~= "number" or slot_idx < 1 or slot_idx > 3 then return false end
    local fl = GetActiveVisualFlags()
    local tn = GetActiveVisTune()
    local copy_tn = {}
    for i = 1, #TUNE_KEYS do
        local tk = TUNE_KEYS[i]
        copy_tn[tk] = ClampTuneValue(tk, tn[tk])
    end
    local mask = EncodeCustomVisMask(fl)
    custom_slots[slot_idx] = { mask = mask, tune = copy_tn }
    if TheSim and TheSim.SetPersistentString then
        local parts = { string.format("vm=%d", mask) }
        SerializeTuneEntries(parts, copy_tn)
        TheSim:SetPersistentString(string.format("hypersmooth_slot_%d.cfg", slot_idx), table.concat(parts, ";"), false)
    end
    return true
end

local function LoadUserCustomSlot(slot_idx)
    if type(slot_idx) ~= "number" or slot_idx < 1 or slot_idx > 3 then return false end
    local sl = custom_slots[slot_idx]
    if type(sl) ~= "table" or type(sl.tune) ~= "table" then return false end
    DecodeCustomVisMask(sl.mask or 0)
    for i = 1, #TUNE_KEYS do
        local tk = TUNE_KEYS[i]
        vis_tune[tk] = ClampTuneValue(tk, sl.tune[tk])
    end
    current_visual_preset = 3
    visual_bench_timer = 12.0
    SyncToNativePlugin(true)
    hud_dirty_now = true
    return true
end

SyncToNativePlugin = function(save_persist)
    -- Primary path (v2.2.1+): 0ms direct C++ <-> Lua VM memory bridge (immune to
    -- Documents/OneDrive/WeGame/Steam Cloud path mismatches!)
    local fnFps = rawget(GLOBAL, "HyperSmooth_SetFps")
    if type(fnFps) == "function" then
        pcall(fnFps, current_target_fps)
    end
    local fnSkel = rawget(GLOBAL, "HyperSmooth_SetSkeletal")
    if type(fnSkel) == "function" then
        pcall(fnSkel, current_skeletal_tween)
    end
    local fnVfx = rawget(GLOBAL, "HyperSmooth_SetVfxSmooth")
    if type(fnVfx) == "function" then
        pcall(fnVfx, current_skeletal_tween)
    end
    local fnLowLat = rawget(GLOBAL, "HyperSmooth_SetLowLatency")
    if type(fnLowLat) == "function" then
        pcall(fnLowLat, current_low_latency)
    end
    local fnVis = rawget(GLOBAL, "HyperSmooth_SetVisual")
    if type(fnVis) == "function" then
        local fl = GetActiveVisualFlags()
        local tn = GetActiveVisTune()
        pcall(fnVis, fl.af16 and 16 or 1, fl.full_lm, fl.mip_sharp and ClampTuneValue("mip_bias", tn.mip_bias) or 0.0)
    end
    PushHd2dEnvToNative(0)
    ApplyVisualPostProcess()
    ApplyHudVisibility()
    if type(HS_PanelNotifySync) == "function" then
        HS_PanelNotifySync()
    end
    -- Persist user choices (and modinfo fingerprints mf/ms/mh/mvp/mvm/mll/mhm so explicit modinfo changes win cleanly)
    if save_persist ~= false and TheSim and TheSim.SetPersistentString then
        local parts = {
            string.format(
                "fps=%d;skeletal=%d;hud=%d;vp=%d;vm=%d;ll=%d;hm=%d;mf=%d;ms=%d;mh=%d;mvp=%d;mvm=%d;mll=%d;mhm=%d",
                current_target_fps,
                current_skeletal_tween and 1 or 0,
                current_show_fps_hud and 1 or 0,
                current_visual_preset,
                EncodeCustomVisMask(custom_vis_flags),
                current_low_latency and 1 or 0,
                EncodeHudMode(current_hud_mode),
                TARGET_FPS,
                SKELETAL_TWEEN and 1 or 0,
                SHOW_FPS_HUD and 1 or 0,
                VISUAL_PRESET,
                CFG_VIS_MASK,
                LOW_LATENCY_INPUT and 1 or 0,
                EncodeHudMode(HUD_MODE))
        }
        SerializeTuneEntries(parts, vis_tune)
        TheSim:SetPersistentString(CFG_FILENAME, table.concat(parts, ";"), false)
    end
end

-- Restore last in-game OptionsScreen choices & 3 player custom slots unless user explicitly changed modinfo.lua config
if TheSim and TheSim.GetPersistentString then
    for sidx = 1, 3 do
        local idx_cap = sidx
        TheSim:GetPersistentString(string.format("hypersmooth_slot_%d.cfg", idx_cap), function(ok_s, sstr)
            if ok_s and type(sstr) == "string" and #sstr > 0 then
                local smask = tonumber(sstr:match("vm=(%d+)"))
                if smask ~= nil then
                    local stn = MakePresetTune()
                    ParseTuneEntries(sstr, stn)
                    custom_slots[idx_cap] = { mask = smask, tune = stn }
                end
            end
        end)
    end
    TheSim:GetPersistentString(CFG_FILENAME, function(load_success, str)
        if load_success and type(str) == "string" and #str > 0 then
            local sf = tonumber(str:match("fps=(%d+)"))
            local ss = tonumber(str:match("skeletal=(%d+)"))
            local sh = tonumber(str:match("hud=(%d+)"))
            local svp = tonumber(str:match("vp=(%d+)"))
            local svm = tonumber(str:match("vm=(%d+)"))
            local sll = tonumber(str:match("ll=(%d+)"))
            local shm = tonumber(str:match("hm=(%d+)"))
            local mf = tonumber(str:match("mf=(%d+)"))
            local ms = tonumber(str:match("ms=(%d+)"))
            local mh = tonumber(str:match("mh=(%d+)"))
            local mvp = tonumber(str:match("mvp=(%d+)"))
            local mvm = tonumber(str:match("mvm=(%d+)"))
            local mll = tonumber(str:match("mll=(%d+)"))
            local mhm = tonumber(str:match("mhm=(%d+)"))
            if sf ~= nil and mf == TARGET_FPS then
                current_target_fps = sf
            end
            if ss ~= nil and ms == (SKELETAL_TWEEN and 1 or 0) then
                current_skeletal_tween = (ss ~= 0)
            end
            if sh ~= nil and mh == (SHOW_FPS_HUD and 1 or 0) then
                current_show_fps_hud = (sh ~= 0)
            end
            if sll ~= nil and mll == (LOW_LATENCY_INPUT and 1 or 0) then
                current_low_latency = (sll ~= 0)
            end
            if shm ~= nil and mhm == EncodeHudMode(HUD_MODE) then
                current_hud_mode = DecodeHudMode(shm)
            end
            if svp ~= nil and svp >= 0 and svp <= 3 and mvp == VISUAL_PRESET and mvm == CFG_VIS_MASK then
                current_visual_preset = svp
                if svm ~= nil then
                    DecodeCustomVisMask(svm)
                end
            end
            -- Backward-compatible migration for legacy 7-key short format (ts/ti/te/tt/tsa/tf/tv)
            local legacy_map = {
                ts = "sharp", ti = "ink", te = "exposure", tt = "temp",
                tsa = "sat", tf = "filmic", tv = "vanilla_cc",
            }
            for lk, tk in pairs(legacy_map) do
                local lv = tonumber(str:match(";" .. lk .. "=([%-%d%.]+)"))
                if lv ~= nil then
                    vis_tune[tk] = ClampTuneValue(tk, lv)
                end
            end
            -- Full 74-key parameter persistence
            ParseTuneEntries(str, vis_tune)
        end
        SyncToNativePlugin(true)
    end)
else
    SyncToNativePlugin(true)
end

--------------------------------------------------------------------------------
-- 0. Player-First Opt-In Visual Fidelity Pipeline (v2.8.0) + HS_EXT_V1 Core
--    - Default state is 100% Vanilla Pure (ZERO forced post-processing, ZERO visual burden!)
--    - Every effect is independently toggleable (Mod Config or live c_hsopt)
--    - Disabling restores 100% vanilla rendering cleanly
--------------------------------------------------------------------------------
if type(modimport) == "function" then
    pcall(modimport, "hs_ext")
end

local function ResolveModShaderPath(rel)
    local rfp = rawget(GLOBAL, "resolvefilepath")
    if type(rfp) == "function" then
        local ok, resolved = pcall(rfp, rel)
        if ok and type(resolved) == "string" and #resolved > 0 then
            return resolved
        end
    end
    return (MODROOT or "") .. rel
end

if type(AddModShadersInit) == "function" and type(AddModShadersSortAndEnable) == "function" then
    AddModShadersInit(function()
        local net = rawget(GLOBAL, "TheNet")
        if net and net.IsDedicated and net:IsDedicated() then return end
        local pp = rawget(GLOBAL, "PostProcessor")
        local ppe = rawget(GLOBAL, "PostProcessorEffects")
        local se = rawget(GLOBAL, "SamplerEffects")
        local uv = rawget(GLOBAL, "UniformVariables")
        local SS = rawget(GLOBAL, "SamplerSizes")
        local SCM = rawget(GLOBAL, "SamplerColourMode")
        local SEB = rawget(GLOBAL, "SamplerEffectBase")
        local FM = rawget(GLOBAL, "FILTER_MODE")
        local MFM = rawget(GLOBAL, "MIP_FILTER_MODE")
        local WM = rawget(GLOBAL, "WRAP_MODE")
        if not (pp and ppe and se and uv and SS and SCM and SEB and FM and MFM and WM) then
            return
        end

        local ok_build = pcall(function()
            local sh_down  = ResolveModShaderPath("shaders/hs_kawase_down.ksh")
            local sh_up    = ResolveModShaderPath("shaders/hs_kawase_up.ksh")
            local sh_bloom = ResolveModShaderPath("shaders/hs_postprocess_bloom.ksh")
            local sh_final = ResolveModShaderPath("shaders/hs_visual_final.ksh")

            -- Fluid Shadow Engine v2: upgrade BloomSampler from Klei's coarse 0.25x (480x270 = 4x4 texel stepping)
            -- to crisp 1.0x (16x pixel density) or 0.5x so sun drift & foliage sway slide smoothly with zero stutter!
            -- Costs 0.00ms when Preset 0 + Bloom OFF (BloomSampler pass is skipped by engine).
            SyncBloomSamplerResolution(pp)

            uv.HS_BLOOM_PARAMS  = pp:AddUniformVariable("HS_BLOOM_PARAMS", 4)
            uv.HS_BLOOM_EX      = pp:AddUniformVariable("HS_BLOOM_EX", 4)
            uv.HS_BLOOM_FX      = pp:AddUniformVariable("HS_BLOOM_FX", 4)
            uv.HS_BLOOM_SUN     = pp:AddUniformVariable("HS_BLOOM_SUN", 4)
            uv.HS_VISUAL_PARAMS = pp:AddUniformVariable("HS_VISUAL_PARAMS", 4)
            uv.HS_SHARP_EX      = pp:AddUniformVariable("HS_SHARP_EX", 4)
            uv.HS_SHARP_PRO     = pp:AddUniformVariable("HS_SHARP_PRO", 4)
            uv.HS_AURA_EX       = pp:AddUniformVariable("HS_AURA_EX", 4)
            uv.HS_GRADE_A       = pp:AddUniformVariable("HS_GRADE_A", 4)
            uv.HS_GRADE_B       = pp:AddUniformVariable("HS_GRADE_B", 4)
            uv.HS_GRADE_C       = pp:AddUniformVariable("HS_GRADE_C", 4)
            uv.HS_CDL_S         = pp:AddUniformVariable("HS_CDL_S", 4)
            uv.HS_CDL_O         = pp:AddUniformVariable("HS_CDL_O", 4)
            uv.HS_CDL_P         = pp:AddUniformVariable("HS_CDL_P", 4)
            uv.HS_SHADOW_CFG    = pp:AddUniformVariable("HS_SHADOW_CFG", 4)

            se.HS_KawaseDown1 = pp:AddSamplerEffect(sh_down, SS.Relative, 0.5, 0.5, SCM.RGB, SEB.BloomSampler)
            pp:SetEffectUniformVariables(se.HS_KawaseDown1, uv.SAMPLER_PARAMS)
            pp:SetSamplerEffectState(se.HS_KawaseDown1, WM.CLAMP_TO_EDGE)
            pp:SetSamplerEffectFilter(se.HS_KawaseDown1, FM.LINEAR, FM.LINEAR, MFM.NONE)

            se.HS_KawaseDown2 = pp:AddSamplerEffect(sh_down, SS.Relative, 0.25, 0.25, SCM.RGB, SEB.Shader, se.HS_KawaseDown1)
            pp:SetEffectUniformVariables(se.HS_KawaseDown2, uv.SAMPLER_PARAMS)
            pp:SetSamplerEffectState(se.HS_KawaseDown2, WM.CLAMP_TO_EDGE)
            pp:SetSamplerEffectFilter(se.HS_KawaseDown2, FM.LINEAR, FM.LINEAR, MFM.NONE)

            se.HS_KawaseDown3 = pp:AddSamplerEffect(sh_down, SS.Relative, 0.125, 0.125, SCM.RGB, SEB.Shader, se.HS_KawaseDown2)
            pp:SetEffectUniformVariables(se.HS_KawaseDown3, uv.SAMPLER_PARAMS)
            pp:SetSamplerEffectState(se.HS_KawaseDown3, WM.CLAMP_TO_EDGE)
            pp:SetSamplerEffectFilter(se.HS_KawaseDown3, FM.LINEAR, FM.LINEAR, MFM.NONE)

            se.HS_KawaseUp = pp:AddSamplerEffect(sh_up, SS.Relative, 0.5, 0.5, SCM.RGB, SEB.Shader, se.HS_KawaseDown1)
            pp:AddSampler(se.HS_KawaseUp, SEB.Shader, se.HS_KawaseDown2)
            pp:AddSampler(se.HS_KawaseUp, SEB.Shader, se.HS_KawaseDown3)
            pp:SetEffectUniformVariables(se.HS_KawaseUp, uv.SAMPLER_PARAMS)
            pp:SetSamplerEffectState(se.HS_KawaseUp, WM.CLAMP_TO_EDGE)
            pp:SetSamplerEffectFilter(se.HS_KawaseUp, FM.LINEAR, FM.LINEAR, MFM.NONE)

            ppe.HS_Bloom = pp:AddPostProcessEffect(sh_bloom)
            pp:AddSampler(ppe.HS_Bloom, SEB.Shader, se.HS_KawaseDown1)
            pp:AddSampler(ppe.HS_Bloom, SEB.Shader, se.HS_KawaseDown2)
            pp:AddSampler(ppe.HS_Bloom, SEB.Shader, se.HS_KawaseUp)
            pp:SetEffectUniformVariables(ppe.HS_Bloom, uv.HS_BLOOM_PARAMS, uv.HS_BLOOM_EX, uv.HS_BLOOM_FX, uv.HS_BLOOM_SUN)

            ppe.HS_VisualFinal = pp:AddPostProcessEffect(sh_final)
            pp:AddSampler(ppe.HS_VisualFinal, SEB.BloomSampler)
            pp:SetEffectUniformVariables(ppe.HS_VisualFinal,
                uv.HS_VISUAL_PARAMS, uv.HS_SHARP_EX, uv.HS_SHARP_PRO, uv.HS_AURA_EX,
                uv.HS_GRADE_A, uv.HS_GRADE_B, uv.HS_GRADE_C,
                uv.HS_CDL_S, uv.HS_CDL_O, uv.HS_CDL_P, uv.HS_SHADOW_CFG)

            visual_shaders_ready = true
        end)

        if not ok_build then
            visual_shaders_ready = false
        end
        if type(HS_ExtRuntime) == "table" and type(HS_ExtRuntime.BuildShaders) == "function" then
            HS_ExtRuntime.BuildShaders()
        end
    end)

    AddModShadersSortAndEnable(function()
        if not visual_shaders_ready then return end
        local pp = rawget(GLOBAL, "PostProcessor")
        local ppe = rawget(GLOBAL, "PostProcessorEffects")
        if not (pp and ppe) then return end

        pcall(function()
            if ppe.HS_Bloom and ppe.Bloom then
                pp:SetPostProcessEffectAfter(ppe.HS_Bloom, ppe.Bloom)
            end
            local tail_anchor = ppe.MoonPulseGrading or ppe.MoonPulse or ppe.Lunacy or ppe.ColourCube
            if ppe.HS_VisualFinal and tail_anchor then
                pp:SetPostProcessEffectAfter(ppe.HS_VisualFinal, tail_anchor)
            end

            InstallVanillaFilterHooks()

            -- Sync with vanilla Graphics -> Bloom toggle so both stay strictly mutually exclusive
            local mt = getmetatable(pp)
            local idx = mt and mt.__index
            if type(idx) == "table" and type(idx.SetBloomEnabled) == "function" and not idx._hs_bloom_hooked then
                idx._hs_bloom_hooked = true
                orig_SetBloomEnabled = idx.SetBloomEnabled
                if type(idx.IsBloomEnabled) == "function" then
                    local ok_b, cur_b = pcall(idx.IsBloomEnabled, pp)
                    if ok_b and cur_b ~= nil then
                        user_bloom_enabled = cur_b and true or false
                        studio_engine_bloom_rt = user_bloom_enabled
                    end
                end
                idx.SetBloomEnabled = function(self, enabled)
                    user_bloom_enabled = enabled and true or false
                    studio_engine_bloom_rt = nil
                    ApplyVisualPostProcess()
                end
            end

            ApplyVisualPostProcess()
            if type(HS_ExtRuntime) == "table" and type(HS_ExtRuntime.SortAndEnable) == "function" then
                HS_ExtRuntime.SortAndEnable()
            end
        end)
    end)
end

if type(AddPrefabPostInit) == "function" then
    AddPrefabPostInit("world", function(inst)
        HookWorldSnowMap(inst)
    end)

    -- Pure Ethereal Exclusion: shadow creatures / nightmare beings ARE shadows in DST lore
    -- and carry no DynamicShadow in vanilla -> bind zero-overdraw hs_no_shadow.ksh (100% reversible!)
    local SHADOW_BEING_PREFABS = {
        "crawlinghorror", "terrorbeak", "crawlingnightmare", "nightmarebeak",
        "oceanhorror", "ruinsnightmare", "ruinsnightmare_horn_attack",
        "shadow_rook", "shadow_knight", "shadow_bishop", "shadow_bishop_fx",
        "shadowwaxwell", "shadowlumber", "shadowminer", "shadowdigger",
        "shadowduelist", "shadowworker", "shadowprotector",
        "waxwell_shadowstriker", "quagmire_shadowwaxwell",
        "shadowtentacle", "bigshadowtentacle", "shadowchanneler",
        "shadowhand", "shadowhand_arm", "shadowwatcher", "shadowskittish",
        "shadow_leech", "shadow_pillar", "shadow_pillar_base_fx",
        "shadow_trap", "shadow_glob_fx", "stalker_bulb", "tophat_shadow_fx",
        "fused_shadeling", "fused_shadeling_bomb", "nightmarefuel",
        "shadow_despawn", "shadow_teleport_in",
    }
    local function OnShadowBeingSpawn(inst)
        ApplyShadowCreatureExclusionOne(inst, IsShadowExclusionActive())
    end
    for i = 1, #SHADOW_BEING_PREFABS do
        AddPrefabPostInit(SHADOW_BEING_PREFABS[i], OnShadowBeingSpawn)
    end
end

if type(AddPrefabPostInitAny) == "function" then
    AddPrefabPostInitAny(function(inst)
        if inst and inst.AnimState and not inst.DynamicShadow and inst.HasTag
            and shadow_excluded_ents[inst] == nil then
            if inst:HasTag("shadowcreature") or inst:HasTag("nightmarecreature")
                or inst:HasTag("shadowminion") or inst:HasTag("shadowchesspiece") then
                ApplyShadowCreatureExclusionOne(inst, IsShadowExclusionActive())
            end
        end
    end)
end

if type(AddClassPostConstruct) == "function" then
    AddClassPostConstruct("widgets/sandover", function(w)
        vf_sandover_widgets[w] = true
        if type(w.OnUpdate) == "function" and not w._hs_sand_hooked then
            w._hs_sand_hooked = true
            local orig_onupdate = w.OnUpdate
            w.OnUpdate = function(self, ...)
                local r = orig_onupdate(self, ...)
                if IsSandFilterActive() then
                    if self.inst and self.inst.entity then
                        pcall(self.inst.entity.Hide, self.inst.entity, false)
                    end
                    if self.dust and self.dust.inst and self.dust.inst.entity then
                        pcall(self.dust.inst.entity.Hide, self.dust.inst.entity, false)
                    end
                end
                return r
            end
        end
        if IsSandFilterActive() then
            if w.inst and w.inst.entity then
                pcall(w.inst.entity.Hide, w.inst.entity, false)
            end
            if w.dust and w.dust.inst and w.dust.inst.entity then
                pcall(w.dust.inst.entity.Hide, w.dust.inst.entity, false)
            end
        end
    end)
end

-- Diagnostic & live per-effect console helpers:
--   c_hsvisual()                  - print active toggles + live GPU/VRAM telemetry
--   c_hslatency()                 - print D3D9Ex queue latency + 240Hz VFX + flick-click rescue stats
--   c_hsopt("af16"|...| "off", [true|false])
--   c_hsbench([true|false])       - pin/unpin compact HUD benchmark strip
--   c_hshud([mode|lang])          - switch HUD compact/detail/on/off & auto/zh/en live
local function PrintVisualReport()
    local ui_zh = ResolveUiIsZh()
    local names = ui_zh
        and { [0] = "原版纯净(0)", [1] = "护眼高清(1)", [2] = "柔和绘本(2)", [3] = "自定义(3)" }
        or { [0] = "Vanilla(0)", [1] = "EyeCareHD(1)", [2] = "SoftStorybook(2)", [3] = "Custom(3)" }
    local fl = GetActiveVisualFlags()
    local fnV = rawget(GLOBAL, "HyperSmooth_GetVisualStats")
    local fnP = rawget(GLOBAL, "HyperSmooth_GetVisualPerf")
    local line1 = ui_zh
        and string.format(
            "[HyperSmooth 画质] 方案=%s | 开关(AF16=%s 光照=%s 防虚=%s 锐化=%s 接地影=%s 近岸=%s 柔光=%s 色调=%s 暗夜=%s) | 游戏泛光=%s",
            names[current_visual_preset] or tostring(current_visual_preset),
            tostring(fl.af16), tostring(fl.full_lm), tostring(fl.mip_sharp), tostring(fl.cas), tostring(fl.ao),
            tostring(fl.env), tostring(fl.soft_bloom), tostring(fl.tone), tostring(fl.dark_clarity), tostring(user_bloom_enabled))
        or string.format(
            "[HyperSmooth Visual] mode=%s | flags(af16=%s lm=%s mip=%s cas=%s ao=%s env=%s softBloom=%s tone=%s dark=%s) | gameBloom=%s",
            names[current_visual_preset] or tostring(current_visual_preset),
            tostring(fl.af16), tostring(fl.full_lm), tostring(fl.mip_sharp), tostring(fl.cas), tostring(fl.ao),
            tostring(fl.env), tostring(fl.soft_bloom), tostring(fl.tone), tostring(fl.dark_clarity), tostring(user_bloom_enabled))
    if type(fnV) == "function" then
        local ok, af, hw, hits, lmp, lms, mipOk, mipVal, mipHits = pcall(fnV)
        if ok then
            line1 = line1 .. (ui_zh
                and string.format(" | 过滤=%dx(硬件上限%.0fx 命中=%d) 光照=%.1fx 纹理偏移=%.2f(生效=%s 命中=%d)",
                    af or 1, hw or 1, hits or 0, lms or 0.5, mipVal or 0.0, tostring(mipOk == true), mipHits or 0)
                or string.format(" | AF=%dx (hw=%.0fx hits=%d) LM=%.1fx MipBias=%.2f(ok=%s hits=%d)",
                    af or 1, hw or 1, hits or 0, lms or 0.5, mipVal or 0.0, tostring(mipOk == true), mipHits or 0))
        end
    end
    if type(fnP) == "function" then
        local ok, hdAct, progs, mStr, wMs, pMs, bMs, gMs, vCur, vPk, vBud = pcall(fnP)
        if ok then
            GLOBAL.print(line1)
            GLOBAL.print(ui_zh
                and string.format(
                    "[HyperSmooth 性能] 世界着色器=%d (强度=%.2f) | 总忙时=%.2fms (世界=%.2fms 后处理=%.2fms GPU排空=%.2fms) | 显存=%.0fMB (峰值=%.0fMB / 预算=%.0fMB)",
                    progs or 0, mStr or 0,
                    bMs or 0, wMs or 0, pMs or 0, gMs or 0, vCur or 0, vPk or 0, vBud or 0)
                or string.format(
                    "[HyperSmooth Perf] world_progs=%d (active_str=%.2f) | Busy=%.2fms (World=%.2fms Post=%.2fms GpuDrain=%.2fms) | VRAM=%.0fMB (Peak=%.0fMB / Budget=%.0fMB)",
                    progs or 0, mStr or 0,
                    bMs or 0, wMs or 0, pMs or 0, gMs or 0, vCur or 0, vPk or 0, vBud or 0))
            return
        end
    end
    GLOBAL.print(line1 .. (ui_zh and " (未加载 dinput8.dll 桥接)" or " (no dinput8.dll bridge)"))
end

local function PrintLatencyReport()
    local ui_zh = ResolveUiIsZh()
    local fnL = rawget(GLOBAL, "HyperSmooth_GetLatencyStats")
    local fnH = rawget(GLOBAL, "HyperSmooth_GetHostPerf")
    local luaLine = ui_zh
        and string.format(
            "[HyperSmooth 延迟/输入] 低延迟=%s | 操作(瞬时走位=%d 走位停顿=%d 急甩刷新=%d 急甩防漏=%d 改键点击=%d 240镜头=%d 飘字=%d) | 制作缓存(重建=%d 省Has=%d 延后UI=%d) | 跳过冗余(地图=%d 放置=%d 部署=%d)",
            tostring(current_low_latency),
            latency_stats.wasd_instant_rpcs,
            latency_stats.walk_cancels,
            latency_stats.flick_refreshes,
            latency_stats.flick_rescues,
            latency_stats.mod_click_rescues,
            latency_stats.cam240_frames,
            latency_stats.followtext_240,
            latency_stats.craft_rebuilds,
            latency_stats.craft_has_saved,
            latency_stats.craft_ui_deferred,
            latency_stats.map_skip,
            latency_stats.placer_skip,
            latency_stats.deploy_skip)
        or string.format(
            "[HyperSmooth Latency] LowLat=%s | Lua(WASD_instant=%d, WalkCancel=%d, FlickRefresh=%d, FlickRescued=%d, ModClickRescued=%d, Cam240=%d, FollowText240=%d) | CraftZeroStall(rebuilds=%d hasSaved=%d uiDeferred=%d) | UiSkip(map=%d placer=%d deploy=%d)",
            tostring(current_low_latency),
            latency_stats.wasd_instant_rpcs,
            latency_stats.walk_cancels,
            latency_stats.flick_refreshes,
            latency_stats.flick_rescues,
            latency_stats.mod_click_rescues,
            latency_stats.cam240_frames,
            latency_stats.followtext_240,
            latency_stats.craft_rebuilds,
            latency_stats.craft_has_saved,
            latency_stats.craft_ui_deferred,
            latency_stats.map_skip,
            latency_stats.placer_skip,
            latency_stats.deploy_skip)
    if type(fnL) == "function" then
        local ok, exAct, curLat, origLat, gpuMs, gpuDrains, vfxOk, vfxCmds, vfxPts, mipOk, mipVal, lightOk, lightPts, lmSwapOk = pcall(fnL)
        if ok then
            GLOBAL.print(luaLine)
            GLOBAL.print(ui_zh
                and string.format(
                    "[HyperSmooth 原生队列] D3D9Ex低延迟=%s (帧队列=%d/%d) | GPU排空=%.2fms (%d次) | 240Hz粒子=%s (%d命令/%d点) | 240Hz光源=%s (%d个) | 光照图热切=%s | Mip偏移=%s (%.2f)",
                    tostring(exAct == true), curLat or 0, origLat or 0,
                    gpuMs or 0, gpuDrains or 0,
                    tostring(vfxOk == true), vfxCmds or 0, vfxPts or 0,
                    tostring(lightOk == true), lightPts or 0,
                    tostring(lmSwapOk == true),
                    tostring(mipOk == true), mipVal or 0.0)
                or string.format(
                    "[HyperSmooth Native] D3D9ExReflex=%s (MaxFrameLat=%d/%d) | GpuDrain=%.2fms (cnt=%d) | VFX240=%s (cmds=%d pts=%d) | Light240=%s (cnt=%d) | LMHotSwap=%s | MipHook=%s (bias=%.2f)",
                    tostring(exAct == true), curLat or 0, origLat or 0,
                    gpuMs or 0, gpuDrains or 0,
                    tostring(vfxOk == true), vfxCmds or 0, vfxPts or 0,
                    tostring(lightOk == true), lightPts or 0,
                    tostring(lmSwapOk == true),
                    tostring(mipOk == true), mipVal or 0.0))
            if type(fnH) == "function" then
                local okH, fdOk, fdCalls, fdMs, fdMB, fdFb, c2Ok, c2Calls, c2MB, c2Ms, asOk, asInits, asSteps, asMs, asFb = pcall(fnH)
                if okH then
                    GLOBAL.print(ui_zh
                        and string.format(
                            "[HyperSmooth 宿主加速] 极速存档(%s)=%d次(%.1fMB %.2fms 回退=%d) | 解压(%s)=%d次(%.1fMB %.2fms) | 极速寻路(%s)=%d次/%d步(%.2fms 回退=%d)",
                            tostring(fdOk == true), fdCalls or 0, fdMB or 0, fdMs or 0, fdFb or 0,
                            tostring(c2Ok == true), c2Calls or 0, c2MB or 0, c2Ms or 0,
                            tostring(asOk == true), asInits or 0, asSteps or 0, asMs or 0, asFb or 0)
                        or string.format(
                            "[HyperSmooth Host] FastDump(%s): calls=%d (%.1fMB %.2fms fb=%d) | Compress2(%s): calls=%d (%.1fMB %.2fms) | FastAStar(%s): inits=%d steps=%d (%.2fms fb=%d)",
                            tostring(fdOk == true), fdCalls or 0, fdMB or 0, fdMs or 0, fdFb or 0,
                            tostring(c2Ok == true), c2Calls or 0, c2MB or 0, c2Ms or 0,
                            tostring(asOk == true), asInits or 0, asSteps or 0, asMs or 0, asFb or 0))
                end
            end
            local fnR = rawget(GLOBAL, "HyperSmooth_GetRenderOptStats")
            if type(fnR) == "function" then
                local okR, glC, glH, uC, uH, fsQ, fsH, djH, djPk = pcall(fnR)
                if okR then
                    local glPct = (glC and glC > 0) and (100.0 * (glH or 0) / glC) or 0.0
                    local uPct = (uC and uC > 0) and (100.0 * (uH or 0) / uC) or 0.0
                    GLOBAL.print(ui_zh
                        and string.format(
                            "[HyperSmooth 渲染优化] GL状态缓存=%.1f%%(%d/%d) | Uniform缓存=%.1f%%(%d/%d) | 负缓存=%d/%d | 去抖动=%d(峰值=%.2fms)",
                            glPct, glH or 0, glC or 0, uPct, uH or 0, uC or 0, fsH or 0, fsQ or 0, djH or 0, djPk or 0.0)
                        or string.format(
                            "[HyperSmooth RenderOpt] GLState=%.1f%% (%d/%d) | GLUniform=%.1f%% (%d/%d) | FsNeg=%d/%d | DeJitter=%d (peak=%.2fms)",
                            glPct, glH or 0, glC or 0, uPct, uH or 0, uC or 0, fsH or 0, fsQ or 0, djH or 0, djPk or 0.0))
                end
            end
            local fnB = rawget(GLOBAL, "HyperSmooth_GetRenderBackend")
            local backendStr = "D3D9"
            if type(fnB) == "function" then
                local okB, rawBe, renderer = pcall(fnB)
                if okB then
                    local isGl = IsRenderBackendOpenGL(rawBe)
                    if ui_zh then
                        backendStr = (isGl and "原生 OpenGL ES2" or "ANGLE D3D9") .. (type(renderer) == "string" and (" (" .. renderer .. ")") or "")
                    else
                        backendStr = (isGl and "OpenGL ES2" or "ANGLE D3D9") .. (type(renderer) == "string" and (" (" .. renderer .. ")") or "")
                    end
                end
            end
            local fnVb = rawget(GLOBAL, "HyperSmooth_GetVbPoolStats")
            if type(fnVb) == "function" then
                local okV, vh, vm, vrec, vact, vcnt, vkb = pcall(fnVb)
                if okV then
                    local vtot = (vh or 0) + (vm or 0)
                    local vpct = (vtot > 0) and (100.0 * (vh or 0) / vtot) or 0.0
                    GLOBAL.print(ui_zh
                        and string.format(
                            "[HyperSmooth 渲染/缓冲池] 后端=%s | VBPool命中=%.1f%% (%d/%d 回收=%d 活跃=%d 池=%d个 %.0fKB)",
                            backendStr, vpct, vh or 0, vtot, vrec or 0, vact or 0, vcnt or 0, vkb or 0.0)
                        or string.format(
                            "[HyperSmooth Render/VBPool] Backend=%s | VBPool=%.1f%% (%d/%d rec=%d act=%d pool=%d %.0fKB)",
                            backendStr, vpct, vh or 0, vtot, vrec or 0, vact or 0, vcnt or 0, vkb or 0.0))
                end
            else
                GLOBAL.print((ui_zh and "[HyperSmooth 渲染] 后端=" or "[HyperSmooth Render] Backend=") .. backendStr)
            end
            local fnSb = rawget(GLOBAL, "HyperSmooth_GetSpriteBatchStats")
            if type(fnSb) == "function" then
                local okSb, sbAct, sbIn, sbBat, sbFlu, sbVerts, sbByp, sbProgs = pcall(fnSb)
                if okSb then
                    local sbRed = (sbBat and sbBat > 0 and sbBat >= (sbFlu or 0))
                        and (100.0 * (sbBat - (sbFlu or 0)) / sbBat) or 0.0
                    local avgPerFlush = (sbFlu and sbFlu > 0) and ((sbBat or 0) / sbFlu) or 0.0
                    GLOBAL.print(ui_zh
                        and string.format(
                            "[HyperSmooth 2D合批] 启用=%s (着色器=%d) | 合并=%d -> %d次DrawCall (削减=%.1f%%, 均=%.1f精灵/批) | 顶点=%d 旁路=%d",
                            tostring(sbAct == true), sbProgs or 0, sbBat or 0, sbFlu or 0, sbRed, avgPerFlush, sbVerts or 0, sbByp or 0)
                        or string.format(
                            "[HyperSmooth SpriteBatch] Active=%s (progs=%d) | Coalesced=%d -> %d draws (-%.1f%%, avg=%.1f/batch) | verts=%d bypass=%d",
                            tostring(sbAct == true), sbProgs or 0, sbBat or 0, sbFlu or 0, sbRed, avgPerFlush, sbVerts or 0, sbByp or 0))
                end
            end
            local fnNet = rawget(GLOBAL, "HyperSmooth_GetNetStats")
            if type(fnNet) == "function" then
                local okN, nConn, pAvg, pLast, lCur, lAvg, resend, txBps, rxBps, rImm, rOff = pcall(fnNet)
                if okN then
                    GLOBAL.print(ui_zh
                        and string.format(
                            "[HyperSmooth 网络/QoS] 已连接=%s | 延迟(均=%dms 瞬=%dms) | 丢包(当前=%.1f%% 平均=%.1f%%) | 重传=%d 上行=%.1fKB/s 下行=%.1fKB/s | 优先RPC(直达Ch0=%d 分流=%d)",
                            tostring(nConn == true), pAvg or -1, pLast or -1, lCur or 0.0, lAvg or 0.0,
                            resend or 0, (txBps or 0) / 1024.0, (rxBps or 0) / 1024.0, rImm or 0, rOff or 0)
                        or string.format(
                            "[HyperSmooth Net/QoS] Connected=%s | Ping(avg=%dms last=%dms) | Loss(cur=%.1f%% avg=%.1f%%) | Resend=%d Tx=%.1fKB/s Rx=%.1fKB/s | RpcQoS(imm=%d offCh0=%d)",
                            tostring(nConn == true), pAvg or -1, pLast or -1, lCur or 0.0, lAvg or 0.0,
                            resend or 0, (txBps or 0) / 1024.0, (rxBps or 0) / 1024.0, rImm or 0, rOff or 0))
                end
            end
            local fnSav = rawget(GLOBAL, "HyperSmooth_GetAsyncSaveStats")
            if type(fnSav) == "function" then
                local okS, sAct, sEnq, sCom, sBytes, sComp, sCompMs, sFlu, sFb, sErr, sIoMs = pcall(fnSav)
                if okS then
                    GLOBAL.print(ui_zh
                        and string.format(
                            "[HyperSmooth 异步存盘] 启用=%s | 入队=%d 完成=%d (%.2fMB) | 后台压缩=%d (%.1fms) | 刷盘=%d 回退=%d 错误=%d IO耗时=%.1fms",
                            tostring(sAct == true), sEnq or 0, sCom or 0, (sBytes or 0) / (1024.0 * 1024.0),
                            sComp or 0, sCompMs or 0.0, sFlu or 0, sFb or 0, sErr or 0, sIoMs or 0.0)
                        or string.format(
                            "[HyperSmooth AsyncSave] Active=%s | enq=%d ok=%d (%.2fMB) | comp=%d (%.1fms) | flush=%d fb=%d err=%d io=%.1fms",
                            tostring(sAct == true), sEnq or 0, sCom or 0, (sBytes or 0) / (1024.0 * 1024.0),
                            sComp or 0, sCompMs or 0.0, sFlu or 0, sFb or 0, sErr or 0, sIoMs or 0.0))
                end
            end
            return
        end
    end
    GLOBAL.print(luaLine .. (ui_zh and " (未加载 dinput8.dll 桥接)" or " (no dinput8.dll bridge)"))
end

rawset(GLOBAL, "c_hsvisual", PrintVisualReport)
rawset(GLOBAL, "c_hslatency", PrintLatencyReport)
rawset(GLOBAL, "c_hsbatch", function(enabled)
    local fnSet = rawget(GLOBAL, "HyperSmooth_SetSpriteBatch")
    if type(fnSet) ~= "function" then
        GLOBAL.print("[HyperSmooth] dinput8.dll SpriteBatch bridge not loaded.")
        return false
    end
    if enabled == nil then
        local fnGet = rawget(GLOBAL, "HyperSmooth_GetSpriteBatchStats")
        local ok, cur = pcall(fnGet)
        enabled = not (ok and cur == true)
    end
    local ok, res = pcall(fnSet, enabled and true or false)
    GLOBAL.print(string.format("[HyperSmooth] 2D SpriteBatch -> %s", (ok and res == true) and "ON" or "OFF"))
    return ok and res == true
end)

local OPT_ALIASES = {
    af16 = "af16", af = "af16",
    lightmap = "full_lm", lm = "full_lm", full_lm = "full_lm",
    mip = "mip_sharp", mip_sharp = "mip_sharp", lod = "mip_sharp",
    cas = "cas", sharpen = "cas", bcas = "cas",
    ao = "ao", contact_ao = "ao",
    env = "env", calm_env = "env",
    bloom = "soft_bloom", soft_bloom = "soft_bloom",
    tone = "tone", storybook = "tone", grade = "tone",
    dark = "dark_clarity", dark_clarity = "dark_clarity", night = "dark_clarity",
    filter = "clean_filter", clean = "clean_filter", clean_filter = "clean_filter", sanity = "clean_filter",
}

local function HasAnyCleanFilterTune(tn)
    return (tn.sanity_color or 0) > 0.5
        or (tn.distort_free or 0) > 0.5
        or (tn.sand_filter or 0) > 0.5
        or ClampTuneValue("snow_cap", tn.snow_cap) < 0.999
end

local function HasAnyGradingTune(tn)
    return math.abs(ClampTuneValue("exposure", tn.exposure)) > 0.001
        or math.abs(ClampTuneValue("temp", tn.temp)) > 0.001
        or math.abs(ClampTuneValue("tint", tn.tint)) > 0.001
        or math.abs(ClampTuneValue("contrast", tn.contrast)) > 0.001
        or math.abs(ClampTuneValue("lightness", tn.lightness)) > 0.001
        or math.abs(ClampTuneValue("gamma", tn.gamma) - 1.0) > 0.001
        or math.abs(ClampTuneValue("sat", tn.sat) - 1.0) > 0.001
        or math.abs(ClampTuneValue("vibrance", tn.vibrance)) > 0.001
        or ClampTuneValue("filmic", tn.filmic) > 0.001
        or ClampTuneValue("hl_desat", tn.hl_desat) > 0.001
        or ClampTuneValue("sun_fill", tn.sun_fill) > 0.001
        or ClampTuneValue("vignette", tn.vignette) > 0.001
        or ClampTuneValue("grain", tn.grain) > 0.001
        or math.abs(ClampTuneValue("slope_r", tn.slope_r) - 1.0) > 0.001
        or math.abs(ClampTuneValue("slope_g", tn.slope_g) - 1.0) > 0.001
        or math.abs(ClampTuneValue("slope_b", tn.slope_b) - 1.0) > 0.001
        or math.abs(ClampTuneValue("offset_r", tn.offset_r)) > 0.001
        or math.abs(ClampTuneValue("offset_g", tn.offset_g)) > 0.001
        or math.abs(ClampTuneValue("offset_b", tn.offset_b)) > 0.001
        or math.abs(ClampTuneValue("power_r", tn.power_r) - 1.0) > 0.001
        or math.abs(ClampTuneValue("power_g", tn.power_g) - 1.0) > 0.001
        or math.abs(ClampTuneValue("power_b", tn.power_b) - 1.0) > 0.001
end

local function HasAnyWorldStudioTune(tn)
    return ClampTuneValue("surface_light", tn.surface_light) > 0.001
        or ClampTuneValue("shadow_density", tn.shadow_density) > 0.001
        or ClampTuneValue("glint_str", tn.glint_str) > 0.001
end

local function HasAnyCustomActive()
    return EncodeCustomVisMask(custom_vis_flags) > 0
        or ClampTuneValue("vanilla_cc", vis_tune.vanilla_cc) < 0.999
        or HasAnyCleanFilterTune(vis_tune)
        or HasAnyWorldStudioTune(vis_tune)
end

local function SnapshotFlagsToCustom()
    if current_visual_preset ~= 3 then
        local cur = GetActiveVisualFlags()
        local cur_tn = GetActiveVisTune()
        custom_vis_flags.af16         = cur.af16
        custom_vis_flags.full_lm      = cur.full_lm
        custom_vis_flags.cas          = cur.cas
        custom_vis_flags.ao           = cur.ao
        custom_vis_flags.env          = cur.env
        custom_vis_flags.soft_bloom   = cur.soft_bloom
        custom_vis_flags.tone         = cur.tone
        custom_vis_flags.mip_sharp    = cur.mip_sharp
        custom_vis_flags.dark_clarity = cur.dark_clarity
        custom_vis_flags.clean_filter = cur.clean_filter
        for i = 1, #TUNE_KEYS do
            local tk = TUNE_KEYS[i]
            vis_tune[tk] = cur_tn[tk]
        end
    end
end

local function EnsureFlagTuneOnToggle(field, enabled)
    if field == "clean_filter" then
        if enabled then
            vis_tune.sanity_color = 1
            vis_tune.distort_free = 1
            vis_tune.sand_filter  = 1
            if ClampTuneValue("snow_cap", vis_tune.snow_cap) >= 0.999 then
                vis_tune.snow_cap = 0.65
            end
        else
            vis_tune.sanity_color = 0
            vis_tune.distort_free = 0
            vis_tune.sand_filter  = 0
            vis_tune.snow_cap     = 1.00
        end
        return
    end
    if not enabled then return end
    if field == "cas" then
        if ClampTuneValue("sharp", vis_tune.sharp) <= 0.001
            and ClampTuneValue("ink", vis_tune.ink) <= 0.001
            and ClampTuneValue("dither", vis_tune.dither) <= 0.001 then
            vis_tune.sharp  = TUNE_BOUNDS.sharp.default
            vis_tune.ink    = TUNE_BOUNDS.ink.default
            vis_tune.dither = TUNE_BOUNDS.dither.default
        end
    elseif field == "mip_sharp" then
        if ClampTuneValue("mip_bias", vis_tune.mip_bias) > -0.01 then
            vis_tune.mip_bias = TUNE_BOUNDS.mip_bias.default
        end
    elseif field == "ao" then
        if ClampTuneValue("ao_str", vis_tune.ao_str) <= 0.001 then
            vis_tune.ao_str = TUNE_BOUNDS.ao_str.default
        end
    elseif field == "env" then
        if ClampTuneValue("env_str", vis_tune.env_str) <= 0.001 then
            vis_tune.env_str = TUNE_BOUNDS.env_str.default
        end
    elseif field == "dark_clarity" then
        if ClampTuneValue("dark_str", vis_tune.dark_str) <= 0.001 then
            vis_tune.dark_str = TUNE_BOUNDS.dark_str.default
        end
    elseif field == "soft_bloom" then
        if ClampTuneValue("bloom_str", vis_tune.bloom_str) <= 0.001 then
            vis_tune.bloom_str = TUNE_BOUNDS.bloom_str.default
        end
    elseif field == "tone" then
        if not HasAnyGradingTune(vis_tune) then
            local p2 = PRESET_TUNE[2]
            vis_tune.exposure = p2.exposure
            vis_tune.temp     = p2.temp
            vis_tune.tint     = p2.tint
            vis_tune.contrast = p2.contrast
            vis_tune.sat      = p2.sat
            vis_tune.vibrance = p2.vibrance
            vis_tune.filmic   = p2.filmic
            vis_tune.hl_desat = p2.hl_desat
            vis_tune.vignette = p2.vignette
        end
    end
end

local GRADE_KEY_SET = {
    exposure = true, temp = true, tint = true, contrast = true, lightness = true, gamma = true,
    sat = true, vibrance = true, filmic = true, hl_desat = true, sun_fill = true, original_mix = true,
    slope_r = true, slope_g = true, slope_b = true, offset_r = true, offset_g = true, offset_b = true,
    power_r = true, power_g = true, power_b = true, vignette = true, grain = true,
}
local BLOOM_KEY_SET = {
    bloom_str = true, bloom_thresh = true, bloom_knee = true, bloom_chr = true, bloom_prot = true,
    bloom_warmth = true, bloom_spread = true, bloom_compress = true,
    glow_tail = true, light_wrap = true, glow_rim = true, god_rays = true,
}
local FILTER_KEY_SET = {
    snow_cap = true, sanity_color = true, distort_free = true, sand_filter = true,
}

local function ApplyTuneValue(field, val)
    if type(field) ~= "string" or TUNE_BOUNDS[field] == nil then return false end
    SnapshotFlagsToCustom()
    local clamped = ClampTuneValue(field, val)
    vis_tune[field] = clamped
    if (field == "sharp" or field == "ink" or field == "dither") and clamped > 0.001 then
        custom_vis_flags.cas = true
    elseif field == "mip_bias" then
        custom_vis_flags.mip_sharp = (clamped < -0.001)
    elseif BLOOM_KEY_SET[field] then
        custom_vis_flags.soft_bloom = true
    elseif field == "ao_str" then
        custom_vis_flags.ao = (clamped > 0.001)
    elseif field == "env_str" then
        custom_vis_flags.env = (clamped > 0.001)
    elseif field == "dark_str" then
        custom_vis_flags.dark_clarity = (clamped > 0.001)
    elseif GRADE_KEY_SET[field] then
        custom_vis_flags.tone = HasAnyGradingTune(vis_tune)
    elseif FILTER_KEY_SET[field] then
        custom_vis_flags.clean_filter = HasAnyCleanFilterTune(vis_tune)
    end
    current_visual_preset = HasAnyCustomActive() and 3 or 0
    visual_bench_timer = 12.0
    SyncToNativePlugin(true)
    hud_dirty_now = true
    return true
end

rawset(GLOBAL, "c_hsopt", function(key, val)
    local ui_zh = ResolveUiIsZh()
    if type(key) ~= "string" then
        GLOBAL.print(ui_zh
            and "[HyperSmooth] 用法: c_hsopt('af16'|'lightmap'|'mip'|'cas'|'ao'|'env'|'bloom'|'tone'|'dark'|'filter'|'off', true|false)"
            or "[HyperSmooth] Usage: c_hsopt('af16'|'lightmap'|'mip'|'cas'|'ao'|'env'|'bloom'|'tone'|'dark'|'filter'|'off', true|false)")
        PrintVisualReport()
        return
    end
    local k = key:lower()
    if k == "off" or k == "vanilla" or k == "reset" then
        current_visual_preset = 0
        visual_bench_timer = 12.0
        SyncToNativePlugin(true)
        hud_dirty_now = true
        PrintVisualReport()
        return
    end
    local field = OPT_ALIASES[k]
    if not field then
        GLOBAL.print(ui_zh
            and ("[HyperSmooth] 未知选项 '" .. tostring(key) .. "'。可选: af16, lightmap, mip, cas, ao, env, bloom, tone, dark, filter, off")
            or ("[HyperSmooth] Unknown option '" .. tostring(key) .. "'. Valid: af16, lightmap, mip, cas, ao, env, bloom, tone, dark, filter, off"))
        return
    end
    SnapshotFlagsToCustom()
    if val == nil then
        custom_vis_flags[field] = not custom_vis_flags[field]
    else
        custom_vis_flags[field] = (val == true or val == 1)
    end
    EnsureFlagTuneOnToggle(field, custom_vis_flags[field])
    current_visual_preset = HasAnyCustomActive() and 3 or 0
    visual_bench_timer = 12.0
    SyncToNativePlugin(true)
    hud_dirty_now = true
    PrintVisualReport()
end)

local TUNE_ALIASES = {
    -- Sharpen / Ink / Bilateral / AURA / Texture
    sharp = "sharp", bcas = "sharp",
    ink = "ink", deconv = "ink",
    deconv_gate = "deconv_gate", gate = "deconv_gate",
    deconv_penetr = "deconv_penetr", penetr = "deconv_penetr",
    anti_ring = "anti_ring", ring = "anti_ring",
    dark_prot = "dark_prot", darkprot = "dark_prot",
    chroma_prot = "chroma_prot", chromaprot = "chroma_prot",
    noise_reduce = "noise_reduce", noise = "noise_reduce",
    range_sigma = "range_sigma", rsigma = "range_sigma",
    spatial_sigma = "spatial_sigma", ssigma = "spatial_sigma",
    noise_floor = "noise_floor", nfloor = "noise_floor",
    ar_thresh = "ar_thresh",
    ar_l_over = "ar_l_over", ar_l = "ar_l_over",
    ar_d_over = "ar_d_over", ar_d = "ar_d_over",
    dither = "dither",
    mip_bias = "mip_bias", mip = "mip_bias", lod = "mip_bias",
    -- Glow Pyramid & Air FX
    bloom_str = "bloom_str", bloom = "bloom_str",
    bloom_thresh = "bloom_thresh", thresh = "bloom_thresh",
    bloom_knee = "bloom_knee", knee = "bloom_knee",
    bloom_chr = "bloom_chr", bloom_chroma = "bloom_chr",
    bloom_prot = "bloom_prot",
    bloom_warmth = "bloom_warmth", warmth = "bloom_warmth",
    bloom_spread = "bloom_spread", spread = "bloom_spread",
    bloom_compress = "bloom_compress", compress = "bloom_compress",
    glow_tail = "glow_tail", tail = "glow_tail",
    light_wrap = "light_wrap", wrap = "light_wrap",
    glow_rim = "glow_rim", rim = "glow_rim",
    god_rays = "god_rays", rays = "god_rays",
    -- World Studio: AO/Env/Dark + 3D Surface Light + Ground Shadows + Ocean Caustics
    ao_str = "ao_str", ao = "ao_str",
    env_str = "env_str", env = "env_str",
    dark_str = "dark_str", dark = "dark_str",
    surface_light = "surface_light", sl = "surface_light",
    shade_warm = "shade_warm", swarm = "shade_warm",
    shade_contrast = "shade_contrast", scon = "shade_contrast",
    shade_cool = "shade_cool", scool = "shade_cool",
    shade_dark = "shade_dark", sdark = "shade_dark",
    shadow_density = "shadow_density", shadow = "shadow_density",
    shadow_length = "shadow_length", slen = "shadow_length",
    shadow_soft = "shadow_soft", ssoft = "shadow_soft",
    shadow_fade = "shadow_fade", sfade = "shadow_fade",
    shadow_sway = "shadow_sway", sway = "shadow_sway",
    shadow_smooth = "shadow_smooth", ssmooth = "shadow_smooth",
    shadow_tint_r = "shadow_tint_r", sh_r = "shadow_tint_r",
    shadow_tint_g = "shadow_tint_g", sh_g = "shadow_tint_g",
    shadow_tint_b = "shadow_tint_b", sh_b = "shadow_tint_b",
    shadow_night = "shadow_night", sh_night = "shadow_night",
    shadow_hd = "shadow_hd", shd = "shadow_hd",
    shadow_skip_nightmare = "shadow_skip_nightmare", snightmare = "shadow_skip_nightmare",
    glint_str = "glint_str", glint = "glint_str",
    glint_density = "glint_density",
    glint_grain = "glint_grain",
    glint_soft = "glint_soft",
    -- Cinematic Color Grading + ASC CDL + Film Atmo
    exp = "exposure", exposure = "exposure", ev = "exposure",
    temp = "temp", wb = "temp",
    tint = "tint",
    contrast = "contrast", con = "contrast",
    lightness = "lightness", light = "lightness",
    gamma = "gamma", gam = "gamma",
    sat = "sat", saturation = "sat",
    vibrance = "vibrance", vib = "vibrance",
    filmic = "filmic", aces = "filmic",
    hl_desat = "hl_desat", hldesat = "hl_desat",
    sun_fill = "sun_fill", sunfill = "sun_fill",
    original_mix = "original_mix", origmix = "original_mix",
    slope_r = "slope_r", slope_g = "slope_g", slope_b = "slope_b",
    offset_r = "offset_r", offset_g = "offset_g", offset_b = "offset_b",
    power_r = "power_r", power_g = "power_g", power_b = "power_b",
    vignette = "vignette", vig = "vignette",
    grain = "grain",
    -- Reversible Vanilla Filters
    cc = "vanilla_cc", vanilla_cc = "vanilla_cc", lut = "vanilla_cc",
    snow = "snow_cap", snow_cap = "snow_cap",
    sanity = "sanity_color", sanity_color = "sanity_color",
    distort = "distort_free", distort_free = "distort_free",
    sand = "sand_filter", sand_filter = "sand_filter",
}

rawset(GLOBAL, "c_hstune", function(key, val)
    local ui_zh = ResolveUiIsZh()
    if type(key) ~= "string" then
        local tn = GetActiveVisTune()
        GLOBAL.print(string.format(
            "[HyperSmooth Studio] sharp=%.2f ink=%.2f ring=%.2f mip=%.2f | bloom=%.2f warm=%+.2f rays=%.2f | 3d=%.2f shad=%.2f glint=%.2f | ev=%+.2f temp=%+.2f tint=%+.2f gam=%.2f sat=%.2f aces=%.2f | cc=%.2f snow=%.2f (coexist=%s)",
            tn.sharp, tn.ink, tn.anti_ring, tn.mip_bias, tn.bloom_str, tn.bloom_warmth, tn.god_rays,
            tn.surface_light, tn.shadow_density, tn.glint_str,
            tn.exposure, tn.temp, tn.tint, tn.gamma, tn.sat, tn.filmic,
            tn.vanilla_cc, tn.snow_cap, tostring(IsExternalBcasActive())))
        return
    end
    local k = key:lower()
    if k == "reset" then
        for tk, b in pairs(TUNE_BOUNDS) do vis_tune[tk] = b.default end
        SyncToNativePlugin(true)
        GLOBAL.print(ui_zh and "[HyperSmooth] 全部 80 项画质/光影/调色参数已重置为默认" or "[HyperSmooth] All 80 studio parameters reset to defaults")
        return
    end
    local field = TUNE_ALIASES[k]
    if not field then
        GLOBAL.print(ui_zh
            and "[HyperSmooth] 未知参数。支持全部 80 项，例如: sharp/ink/exposure/temp/tint/gamma/sun_fill/slope_r/bloom_warmth/god_rays/surface_light/shade_cool/shadow_density/shadow_length/shadow_sway/shadow_fade/shadow_smooth/glint_str/vanilla_cc/snow_cap/reset"
            or "[HyperSmooth] Unknown param. Supports all 80 knobs e.g. sharp, ink, exposure, temp, tint, gamma, sun_fill, slope_r, bloom_warmth, god_rays, surface_light, shade_cool, shadow_density, shadow_length, shadow_sway, shadow_fade, shadow_smooth, glint_str, vanilla_cc, snow_cap, reset")
        return
    end
    local b = TUNE_BOUNDS[field]
    if b.is_bool then
        if type(val) == "boolean" then
            val = val and 1 or 0
        elseif val == nil then
            val = ((GetActiveVisTune()[field] or 0) > 0.5) and 0 or 1
        end
    end
    if type(val) ~= "number" then
        GLOBAL.print(string.format("[HyperSmooth Studio] %s = %.3f (range %.3f .. %.3f)", field, GetActiveVisTune()[field] or 0, b.min, b.max))
        return
    end
    ApplyTuneValue(field, val)
    GLOBAL.print(string.format("[HyperSmooth Studio] %s -> %.3f", field, vis_tune[field]))
end)

rawset(GLOBAL, "c_hsslot", function(action, idx)
    local ui_zh = ResolveUiIsZh()
    local n = tonumber(idx)
    if type(action) == "string" and (action:lower() == "save" or action:lower() == "s") and n then
        local ok = SaveUserCustomSlot(n)
        GLOBAL.print(ok
            and (ui_zh and ("[HyperSmooth] 已保存当前方案到自定义槽位 " .. n) or ("[HyperSmooth] Saved current look to Custom Slot " .. n))
            or "[HyperSmooth] Invalid slot (1..3)")
        return
    elseif type(action) == "string" and (action:lower() == "load" or action:lower() == "l") and n then
        local ok = LoadUserCustomSlot(n)
        GLOBAL.print(ok
            and (ui_zh and ("[HyperSmooth] 已载入自定义槽位 " .. n) or ("[HyperSmooth] Loaded Custom Slot " .. n))
            or (ui_zh and ("[HyperSmooth] 槽位 " .. n .. " 为空") or ("[HyperSmooth] Custom Slot " .. n .. " is empty")))
        return
    end
    GLOBAL.print(ui_zh
        and "[HyperSmooth] 用法: c_hsslot('save'|'load', 1|2|3)"
        or "[HyperSmooth] Usage: c_hsslot('save'|'load', 1|2|3)")
end)

--------------------------------------------------------------------------------
-- Transactional HS1 Share Code (Sparse-Delta Binary + Optional UTF-8 Title +
-- CRC-16/CCITT) & Human-Readable JSON Preset Library (hypersmooth_presets.json)
-- Strict 3-Phase Import: (1) Pure Validate -> (2) Atomic Apply -> (3) Persist
-- A corrupted/truncated/out-of-range code NEVER modifies live config or disk!
--------------------------------------------------------------------------------
local JSON_LIB_FILENAME = "hypersmooth_presets.json"
local FLAG_KEYS_ORDER = {
    "af16", "full_lm", "cas", "ao", "env",
    "soft_bloom", "tone", "mip_sharp", "dark_clarity", "clean_filter",
}
local SHARE_KEY_TO_ID = {}
for i = 1, #TUNE_KEYS do
    SHARE_KEY_TO_ID[TUNE_KEYS[i]] = i
end

local current_scheme_name = ""
local last_import_undo = nil
local json_named_library = {} -- ordered array of { name, mask, tune }

local B64_ENC = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
local B64_DEC = {}
for i = 1, 64 do
    B64_DEC[B64_ENC:byte(i)] = i - 1
end

local function Base64UrlEncode(bytes)
    local out = {}
    local n = #bytes
    local i = 1
    while i <= n do
        local b1 = bytes[i] or 0
        local b2 = bytes[i + 1]
        local b3 = bytes[i + 2]
        local v1 = math.floor(b1 / 4)
        local v2 = (b1 % 4) * 16 + (b2 and math.floor(b2 / 16) or 0)
        out[#out + 1] = B64_ENC:sub(v1 + 1, v1 + 1)
        out[#out + 1] = B64_ENC:sub(v2 + 1, v2 + 1)
        if b2 ~= nil then
            local v3 = (b2 % 16) * 4 + (b3 and math.floor(b3 / 64) or 0)
            out[#out + 1] = B64_ENC:sub(v3 + 1, v3 + 1)
            if b3 ~= nil then
                local v4 = b3 % 64
                out[#out + 1] = B64_ENC:sub(v4 + 1, v4 + 1)
            end
        end
        i = i + 3
    end
    return table.concat(out)
end

local function Base64UrlDecode(str)
    local n = #str
    if n < 2 or (n % 4) == 1 then return nil end
    local out = {}
    local i = 1
    while i <= n do
        local c1 = B64_DEC[str:byte(i)]
        local c2 = B64_DEC[str:byte(i + 1)]
        if c1 == nil or c2 == nil then return nil end
        out[#out + 1] = c1 * 4 + math.floor(c2 / 16)
        if i + 2 <= n then
            local c3 = B64_DEC[str:byte(i + 2)]
            if c3 == nil then return nil end
            out[#out + 1] = (c2 % 16) * 16 + math.floor(c3 / 4)
            if i + 3 <= n then
                local c4 = B64_DEC[str:byte(i + 3)]
                if c4 == nil then return nil end
                out[#out + 1] = (c3 % 4) * 64 + c4
            elseif (c3 % 4) ~= 0 then
                return nil -- non-zero trailing pad bits -> corrupted
            end
        elseif (c2 % 16) ~= 0 then
            return nil -- non-zero trailing pad bits -> corrupted
        end
        i = i + 4
    end
    return out
end

local function BitXor16(a, b)
    local res, p = 0, 1
    for _ = 1, 16 do
        local ba, bb = a % 2, b % 2
        if ba ~= bb then res = res + p end
        a = math.floor(a * 0.5)
        b = math.floor(b * 0.5)
        p = p * 2
    end
    return res
end

local function Crc16Ccitt(bytes, n)
    local crc = 0xFFFF
    for i = 1, n do
        crc = BitXor16(crc, bytes[i] * 256)
        for _ = 1, 8 do
            if crc >= 0x8000 then
                crc = BitXor16((crc * 2) % 65536, 0x1021)
            else
                crc = (crc * 2) % 65536
            end
        end
    end
    return crc
end

local function SanitizeSchemeName(name)
    if type(name) ~= "string" then return "" end
    local s = name:gsub("[%c]", ""):match("^%s*(.-)%s*$") or ""
    if #s <= 36 then return s end
    -- Truncate on valid UTF-8 character boundary (never slice mid-multibyte char!)
    local cut = 36
    while cut > 0 do
        local b = s:byte(cut + 1)
        if b == nil or b < 128 or b >= 192 then
            break
        end
        cut = cut - 1
    end
    return s:sub(1, cut)
end

local function DecodeFlagsMaskToTable(m)
    local function has_bit(val, b)
        return math.floor(val / b) % 2 == 1
    end
    return {
        af16         = has_bit(m, 1),
        full_lm      = has_bit(m, 2),
        cas          = has_bit(m, 4),
        ao           = has_bit(m, 8),
        env          = has_bit(m, 16),
        soft_bloom   = has_bit(m, 32),
        tone         = has_bit(m, 64),
        mip_sharp    = has_bit(m, 128),
        dark_clarity = has_bit(m, 256),
        clean_filter = has_bit(m, 512),
    }
end

local function EncodeShareCodeFromState(fl, tn, scheme_name)
    local clean_name = SanitizeSchemeName(scheme_name)
    local mask = EncodeCustomVisMask(fl)

    -- Pick whichever curated preset (0/1/2) produces the FEWEST delta entries -> ultra-short code!
    local best_base = 0
    local best_diffs = nil
    for b_id = 0, 2 do
        local base_tn = PRESET_TUNE[b_id]
        local diffs = {}
        for i = 1, #TUNE_KEYS do
            local tk = TUNE_KEYS[i]
            local cur_v = ClampTuneValue(tk, tn[tk])
            local ref_v = ClampTuneValue(tk, base_tn[tk])
            if math.abs(cur_v - ref_v) > 0.0004 then
                diffs[#diffs + 1] = { id = i, key = tk, val = cur_v }
            end
        end
        if best_diffs == nil or #diffs < #best_diffs then
            best_base = b_id
            best_diffs = diffs
        end
    end

    local has_name = (#clean_name > 0) and 1 or 0
    local bytes = {}
    bytes[#bytes + 1] = 1 + best_base * 32 + has_name * 128
    bytes[#bytes + 1] = math.floor(mask / 256) % 256
    bytes[#bytes + 1] = mask % 256
    if has_name == 1 then
        bytes[#bytes + 1] = #clean_name
        for i = 1, #clean_name do
            bytes[#bytes + 1] = clean_name:byte(i)
        end
    end
    bytes[#bytes + 1] = #best_diffs
    for i = 1, #best_diffs do
        local d = best_diffs[i]
        local b = TUNE_BOUNDS[d.key]
        local q16
        if b.is_bool then
            q16 = (d.val > 0.5) and 65535 or 0
        else
            local span = math.max(b.max - b.min, 1e-6)
            local frac = math.max(0.0, math.min(1.0, (d.val - b.min) / span))
            q16 = math.floor(frac * 65535.0 + 0.5)
        end
        bytes[#bytes + 1] = d.id
        bytes[#bytes + 1] = math.floor(q16 / 256) % 256
        bytes[#bytes + 1] = q16 % 256
    end
    local crc = Crc16Ccitt(bytes, #bytes)
    bytes[#bytes + 1] = math.floor(crc / 256) % 256
    bytes[#bytes + 1] = crc % 256

    return "HS1-" .. Base64UrlEncode(bytes), #best_diffs, clean_name, best_base
end

local function EncodeCurrentShareCode(optional_name)
    local name = (optional_name ~= nil) and optional_name or current_scheme_name
    return EncodeShareCodeFromState(GetActiveVisualFlags(), GetActiveVisTune(), name)
end

local function TryDecodeJsonText(raw)
    local fnFast = rawget(GLOBAL, "HyperSmooth_FastJsonDecode")
    if type(fnFast) == "function" then
        local ok_f, ok_dec, tbl = pcall(fnFast, raw)
        if ok_f and ok_dec and type(tbl) == "table" then
            return true, tbl
        end
    end
    local gjson = rawget(GLOBAL, "json")
    if type(gjson) == "table" and type(gjson.decode) == "function" then
        local ok_j, tbl = pcall(gjson.decode, raw)
        if ok_j and type(tbl) == "table" then
            return true, tbl
        end
    end
    return false, nil
end

local function IsFiniteNumber(v)
    return type(v) == "number" and v == v and v ~= math.huge and v ~= -math.huge
end

-- Phase 1 (PURE READ-ONLY): Validates either an HS1-... share code OR a JSON scheme object.
-- Mutates ZERO live state. Returns (ok, err_zh_or_en, staged_result).
local function ValidateSharePayload(raw_input)
    local ui_zh = ResolveUiIsZh()
    if type(raw_input) ~= "string" then
        return false, ui_zh and "校验失败：分享内容为空" or "Validation failed: empty input", nil
    end
    local trimmed = raw_input:match("^%s*(.-)%s*$") or ""
    if #trimmed == 0 then
        return false, ui_zh and "校验失败：分享内容为空" or "Validation failed: empty input", nil
    end

    -- Branch A: Raw JSON preset object ({ "flags": {...}, "tune": {...} } or { "active": {...} })
    if trimmed:sub(1, 1) == "{" then
        local ok_j, obj = TryDecodeJsonText(trimmed)
        if not ok_j or type(obj) ~= "table" then
            return false, ui_zh and "校验失败：JSON 语法损坏（未修改当前配置）" or "Validation failed: malformed JSON (config unchanged)", nil
        end
        local src = (type(obj.active) == "table") and obj.active or obj
        if type(src.tune) ~= "table" and type(src.flags) ~= "table" then
            return false, ui_zh and "校验失败：JSON 缺少 tune/flags 字段" or "Validation failed: JSON missing tune/flags", nil
        end
        local staged_flags = DecodeFlagsMaskToTable(0)
        if type(src.flags) == "table" then
            for k, v in pairs(src.flags) do
                if staged_flags[k] == nil then
                    return false, (ui_zh and "校验失败：未知开关字段 " or "Validation failed: unknown flag ") .. tostring(k), nil
                end
                if type(v) ~= "boolean" then
                    return false, (ui_zh and "校验失败：开关类型非法 " or "Validation failed: non-boolean flag ") .. tostring(k), nil
                end
                staged_flags[k] = v
            end
        end
        local staged_tune = MakePresetTune()
        local diff_count = 0
        if type(src.tune) == "table" then
            for k, v in pairs(src.tune) do
                local b = TUNE_BOUNDS[k]
                if not b then
                    return false, (ui_zh and "校验失败：未知参数 " or "Validation failed: unknown param ") .. tostring(k), nil
                end
                if b.is_bool then
                    if type(v) == "boolean" then v = v and 1 or 0 end
                    if not IsFiniteNumber(v) or (v ~= 0 and v ~= 1) then
                        return false, (ui_zh and "校验失败：布尔参数越界 " or "Validation failed: invalid bool ") .. tostring(k), nil
                    end
                    staged_tune[k] = v
                else
                    if not IsFiniteNumber(v) or v < (b.min - 1e-4) or v > (b.max + 1e-4) then
                        return false, string.format(ui_zh and "校验失败：参数 %s 越界 (%.3f..%.3f)" or "Validation failed: %s out of range (%.3f..%.3f)", k, b.min, b.max), nil
                    end
                    staged_tune[k] = ClampTuneValue(k, v)
                end
                diff_count = diff_count + 1
            end
        end
        return true, nil, {
            format     = "json",
            base_id    = 0,
            flags_mask = EncodeCustomVisMask(staged_flags),
            flags      = staged_flags,
            tune       = staged_tune,
            name       = SanitizeSchemeName(src.name or ""),
            diff_count = diff_count,
        }
    end

    -- Branch B: Compact HS1-<base64url> share code
    local prefix, body = trimmed:match("^(HS%d+)%-([A-Za-z0-9_%-]+)$")
    if not prefix or not body then
        return false, ui_zh and "校验失败：格式非法（应以 HS1- 开头且不含乱码）" or "Validation failed: invalid format (expected HS1-...)", nil
    end
    if prefix ~= "HS1" then
        return false, (ui_zh and "校验失败：不支持的分享码版本 " or "Validation failed: unsupported version ") .. prefix, nil
    end
    local bytes = Base64UrlDecode(body)
    if not bytes or #bytes < 6 then
        return false, ui_zh and "校验失败：编码损坏或被截断（未修改当前配置）" or "Validation failed: truncated/corrupt Base64 (config unchanged)", nil
    end
    local n = #bytes
    local expected_crc = bytes[n - 1] * 256 + bytes[n]
    local actual_crc = Crc16Ccitt(bytes, n - 2)
    if actual_crc ~= expected_crc then
        return false, ui_zh and "校验失败：CRC16 校验不匹配（复制不完整或字符被改）" or "Validation failed: CRC16 checksum mismatch (corrupted code)", nil
    end

    local hdr = bytes[1]
    local ver = hdr % 32
    local base_id = math.floor(hdr / 32) % 4
    local has_name = math.floor(hdr / 128) % 2
    if ver ~= 1 or base_id > 2 then
        return false, ui_zh and "校验失败：头部版本或基底预设非法" or "Validation failed: invalid header version or base preset", nil
    end
    local mask = bytes[2] * 256 + bytes[3]
    if mask > 1023 then
        return false, ui_zh and "校验失败：开关掩码超出合法范围" or "Validation failed: flag mask out of range", nil
    end

    local pos = 4
    local scheme_name = ""
    if has_name == 1 then
        if pos > n - 2 then
            return false, ui_zh and "校验失败：标题字段截断" or "Validation failed: truncated title header", nil
        end
        local name_len = bytes[pos]
        pos = pos + 1
        if name_len < 1 or name_len > 36 or (pos + name_len - 1) > (n - 3) then
            return false, ui_zh and "校验失败：标题长度越界" or "Validation failed: invalid title length", nil
        end
        local chars = {}
        for i = 0, name_len - 1 do
            chars[#chars + 1] = string.char(bytes[pos + i])
        end
        scheme_name = SanitizeSchemeName(table.concat(chars))
        pos = pos + name_len
    end

    if pos > n - 2 then
        return false, ui_zh and "校验失败：参数计数缺失" or "Validation failed: missing param count", nil
    end
    local count = bytes[pos]
    pos = pos + 1
    if count > #TUNE_KEYS or (pos + count * 3 - 1) ~= (n - 2) then
        return false, ui_zh and "校验失败：参数长度与校验尾不符" or "Validation failed: payload length mismatch", nil
    end

    -- Build brand-new isolated staging tables (live config 100% untouched!)
    local staged_flags = DecodeFlagsMaskToTable(mask)
    local staged_tune = MakePresetTune(PRESET_TUNE[base_id])
    local seen_ids = {}
    for _ = 1, count do
        local pid = bytes[pos]
        local q16 = bytes[pos + 1] * 256 + bytes[pos + 2]
        pos = pos + 3
        local tk = TUNE_KEYS[pid]
        if not tk or seen_ids[pid] then
            return false, ui_zh and "校验失败：参数 ID 重复或未知" or "Validation failed: duplicate or unknown param ID", nil
        end
        seen_ids[pid] = true
        local b = TUNE_BOUNDS[tk]
        local val
        if b.is_bool then
            val = (q16 >= 32768) and 1 or 0
        else
            val = ClampTuneValue(tk, b.min + (q16 / 65535.0) * (b.max - b.min))
        end
        if not IsFiniteNumber(val) then
            return false, ui_zh and "校验失败：解码数值异常" or "Validation failed: non-finite decoded value", nil
        end
        staged_tune[tk] = val
    end

    return true, nil, {
        format     = "hs1",
        base_id    = base_id,
        flags_mask = mask,
        flags      = staged_flags,
        tune       = staged_tune,
        name       = scheme_name,
        diff_count = count,
    }
end

-- Pretty human-readable JSON serializer (2-space indented, deterministic key order, editable in Notepad/VSCode!)
local function JsonEscapeStr(s)
    return (tostring(s or "")
        :gsub("\\", "\\\\")
        :gsub('"', '\\"')
        :gsub("\n", "\\n")
        :gsub("\r", "\\r")
        :gsub("\t", "\\t"))
end

local function FormatSchemeJsonBlock(indent, name, fl, tn, preset_id, include_code)
    local sp = indent .. "  "
    local sp2 = sp .. "  "
    local lines = { indent .. "{" }
    lines[#lines + 1] = string.format('%s"name": "%s",', sp, JsonEscapeStr(name or ""))
    if preset_id ~= nil then
        lines[#lines + 1] = string.format('%s"preset": %d,', sp, preset_id)
    end
    if include_code then
        local code = EncodeShareCodeFromState(fl, tn, name)
        lines[#lines + 1] = string.format('%s"code": "%s",', sp, JsonEscapeStr(code))
    end
    lines[#lines + 1] = sp .. '"flags": {'
    for i = 1, #FLAG_KEYS_ORDER do
        local fk = FLAG_KEYS_ORDER[i]
        local comma = (i < #FLAG_KEYS_ORDER) and "," or ""
        lines[#lines + 1] = string.format('%s"%s": %s%s', sp2, fk, fl[fk] and "true" or "false", comma)
    end
    lines[#lines + 1] = sp .. "},"
    lines[#lines + 1] = sp .. '"tune": {'
    for i = 1, #TUNE_KEYS do
        local tk = TUNE_KEYS[i]
        local b = TUNE_BOUNDS[tk]
        local comma = (i < #TUNE_KEYS) and "," or ""
        if b.is_bool then
            lines[#lines + 1] = string.format('%s"%s": %d%s', sp2, tk, (tn[tk] or 0) > 0.5 and 1 or 0, comma)
        elseif b.prec == 3 then
            lines[#lines + 1] = string.format('%s"%s": %.3f%s', sp2, tk, ClampTuneValue(tk, tn[tk]), comma)
        else
            lines[#lines + 1] = string.format('%s"%s": %.2f%s', sp2, tk, ClampTuneValue(tk, tn[tk]), comma)
        end
    end
    lines[#lines + 1] = sp .. "}"
    lines[#lines + 1] = indent .. "}"
    return table.concat(lines, "\n")
end

local function ExportJsonPresetLibrary()
    local lines = {
        "{",
        '  "schema": "hypersmooth_visual_library_v1",',
        '  "active": ' .. FormatSchemeJsonBlock("  ", current_scheme_name, GetActiveVisualFlags(), GetActiveVisTune(), current_visual_preset, true):gsub("^  ", "") .. ",",
        '  "library": [',
    }
    local entries = {}
    for sidx = 1, 3 do
        local sl = custom_slots[sidx]
        if type(sl) == "table" and type(sl.tune) == "table" then
            entries[#entries + 1] = {
                name = sl.name or ("Slot_" .. sidx),
                flags = DecodeFlagsMaskToTable(sl.mask or 0),
                tune = sl.tune,
            }
        end
    end
    for i = 1, #json_named_library do
        local item = json_named_library[i]
        if type(item) == "table" and type(item.tune) == "table" then
            entries[#entries + 1] = {
                name = item.name or ("Preset_" .. i),
                flags = DecodeFlagsMaskToTable(item.mask or 0),
                tune = item.tune,
            }
        end
    end
    for i = 1, #entries do
        local blk = FormatSchemeJsonBlock("    ", entries[i].name, entries[i].flags, entries[i].tune, nil, true)
        if i < #entries then blk = blk .. "," end
        lines[#lines + 1] = blk
    end
    lines[#lines + 1] = "  ],"
    -- HS_EXT_V1: Persist third-party shader plugin states & parameters cleanly in the same JSON library
    local ext_map = (type(HS_ExtRuntime) == "table" and type(HS_ExtRuntime.ExportPersistedMap) == "function")
        and HS_ExtRuntime.ExportPersistedMap() or {}
    local ext_ids = {}
    for pid in pairs(ext_map) do ext_ids[#ext_ids + 1] = pid end
    table.sort(ext_ids)
    if #ext_ids == 0 then
        lines[#lines + 1] = '  "plugins": {}'
    else
        lines[#lines + 1] = '  "plugins": {'
        for pi = 1, #ext_ids do
            local pid = ext_ids[pi]
            local pcfg = ext_map[pid]
            local pcomma = (pi < #ext_ids) and "," or ""
            local pkeys = {}
            if type(pcfg.params) == "table" then
                for pk in pairs(pcfg.params) do pkeys[#pkeys + 1] = pk end
                table.sort(pkeys)
            end
            lines[#lines + 1] = string.format('    "%s": {', JsonEscapeStr(pid))
            lines[#lines + 1] = string.format('      "enabled": %s,', (pcfg.enabled == true) and "true" or "false")
            if #pkeys == 0 then
                lines[#lines + 1] = '      "params": {}'
            else
                lines[#lines + 1] = '      "params": {'
                for ki = 1, #pkeys do
                    local pk = pkeys[ki]
                    local pv = pcfg.params[pk]
                    local kcomma = (ki < #pkeys) and "," or ""
                    local vstr = (type(pv) == "boolean") and (pv and "true" or "false") or string.format("%.4f", tonumber(pv) or 0)
                    lines[#lines + 1] = string.format('        "%s": %s%s', JsonEscapeStr(pk), vstr, kcomma)
                end
                lines[#lines + 1] = '      }'
            end
            lines[#lines + 1] = '    }' .. pcomma
        end
        lines[#lines + 1] = "  }"
    end
    lines[#lines + 1] = "}"
    local payload = table.concat(lines, "\n")
    if TheSim and TheSim.SetPersistentString then
        TheSim:SetPersistentString(JSON_LIB_FILENAME, payload, false)
    end
    return payload, #entries
end

local function ParseAndLoadJsonLibraryString(raw_json)
    local ok_j, doc = TryDecodeJsonText(raw_json)
    if not ok_j or type(doc) ~= "table" or type(doc.library) ~= "table" then
        return false, 0
    end
    local loaded_extra = {}
    local loaded_count = 0
    for i = 1, #doc.library do
        local item = doc.library[i]
        if type(item) == "table" then
            local valid, _, staged = false, nil, nil
            if type(item.code) == "string" and #item.code > 4 then
                valid, _, staged = ValidateSharePayload(item.code)
            end
            if not valid and (type(item.tune) == "table" or type(item.flags) == "table") then
                -- Validate inline JSON fields strictly
                local fl_ok = true
                local st_fl = DecodeFlagsMaskToTable(0)
                if type(item.flags) == "table" then
                    for fk, fv in pairs(item.flags) do
                        if st_fl[fk] == nil or type(fv) ~= "boolean" then fl_ok = false; break end
                        st_fl[fk] = fv
                    end
                end
                local st_tn = MakePresetTune()
                if fl_ok and type(item.tune) == "table" then
                    for tk, tv in pairs(item.tune) do
                        local b = TUNE_BOUNDS[tk]
                        if not b then fl_ok = false; break end
                        if b.is_bool and type(tv) == "boolean" then tv = tv and 1 or 0 end
                        if not IsFiniteNumber(tv) or tv < (b.min - 1e-4) or tv > (b.max + 1e-4) then fl_ok = false; break end
                        st_tn[tk] = ClampTuneValue(tk, tv)
                    end
                end
                if fl_ok then
                    valid = true
                    staged = { flags_mask = EncodeCustomVisMask(st_fl), tune = st_tn, name = SanitizeSchemeName(item.name or "") }
                end
            end
            if valid and staged then
                loaded_count = loaded_count + 1
                local sname = SanitizeSchemeName(item.name or staged.name or ("Preset_" .. i))
                local slot_num = tonumber(sname:match("^Slot_(%d+)$"))
                if slot_num and slot_num >= 1 and slot_num <= 3 then
                    if custom_slots[slot_num] == nil then
                        custom_slots[slot_num] = { mask = staged.flags_mask, tune = staged.tune, name = sname }
                    end
                else
                    loaded_extra[#loaded_extra + 1] = { name = sname, mask = staged.flags_mask, tune = staged.tune }
                end
            end
        end
    end
    json_named_library = loaded_extra
    if type(doc.plugins) == "table" and type(HS_ExtRuntime) == "table" and type(HS_ExtRuntime.ApplyPersistedMap) == "function" then
        HS_ExtRuntime.ApplyPersistedMap(doc.plugins)
    end
    return true, loaded_count
end

if type(HS_ExtRuntime) == "table" and type(HS_ExtRuntime.SetHooks) == "function" then
    HS_ExtRuntime.SetHooks(function()
        ExportJsonPresetLibrary()
        if type(HS_PanelNotifySync) == "function" then HS_PanelNotifySync() end
    end, ResolveUiIsZh)
end

local function ReloadJsonPresetLibrary(cb)
    if not (TheSim and TheSim.GetPersistentString) then
        if type(cb) == "function" then cb(false, 0) end
        return false
    end
    TheSim:GetPersistentString(JSON_LIB_FILENAME, function(ok_r, raw_json)
        if ok_r and type(raw_json) == "string" and #raw_json > 2 then
            local ok_p, cnt = ParseAndLoadJsonLibraryString(raw_json)
            if type(cb) == "function" then cb(ok_p, cnt) end
        else
            if type(cb) == "function" then cb(false, 0) end
        end
    end)
    return true
end

-- Auto-load JSON preset library on startup (non-destructive: validates every entry!)
ReloadJsonPresetLibrary()

-- Phase 2 + Phase 3: Strict 3-Phase Transactional Import (Validate -> Atomic Apply -> Persist)
local function ImportSharePayloadTransactional(raw_input)
    local ui_zh = ResolveUiIsZh()
    -- PHASE 1: Pure read-only validation (zero mutation on failure!)
    local ok_val, err_msg, staged = ValidateSharePayload(raw_input)
    if not ok_val or not staged then
        return false, err_msg or (ui_zh and "校验失败" or "Validation failed"), nil
    end

    -- Snapshot previous live state for 1-click Undo AND atomic rollback on any apply error
    local prev_flags = DecodeFlagsMaskToTable(EncodeCustomVisMask(custom_vis_flags))
    local prev_tune = {}
    for i = 1, #TUNE_KEYS do
        prev_tune[TUNE_KEYS[i]] = vis_tune[TUNE_KEYS[i]]
    end
    local prev_preset = current_visual_preset
    local prev_name = current_scheme_name

    -- PHASE 2: Atomic in-memory apply (save_persist = false until GPU sync succeeds!)
    for i = 1, #FLAG_KEYS_ORDER do
        local fk = FLAG_KEYS_ORDER[i]
        custom_vis_flags[fk] = staged.flags[fk] == true
    end
    for i = 1, #TUNE_KEYS do
        local tk = TUNE_KEYS[i]
        vis_tune[tk] = ClampTuneValue(tk, staged.tune[tk])
    end
    current_scheme_name = staged.name or ""
    current_visual_preset = HasAnyCustomActive() and 3 or 0
    visual_bench_timer = 12.0

    local ok_apply = pcall(SyncToNativePlugin, false)
    if not ok_apply then
        -- Roll back atomically -> corrupted/failed apply leaves prior config 100% intact!
        for i = 1, #FLAG_KEYS_ORDER do
            local fk = FLAG_KEYS_ORDER[i]
            custom_vis_flags[fk] = prev_flags[fk]
        end
        for i = 1, #TUNE_KEYS do
            local tk = TUNE_KEYS[i]
            vis_tune[tk] = prev_tune[tk]
        end
        current_visual_preset = prev_preset
        current_scheme_name = prev_name
        pcall(SyncToNativePlugin, false)
        return false, ui_zh and "应用失败：已自动回滚原配置" or "Apply failed: rolled back to previous config", nil
    end

    -- Commit undo record & PHASE 3: Persist to both .cfg and hypersmooth_presets.json!
    last_import_undo = {
        flags = prev_flags,
        tune = prev_tune,
        preset = prev_preset,
        name = prev_name,
    }
    SyncToNativePlugin(true)
    ExportJsonPresetLibrary()
    hud_dirty_now = true
    return true, nil, staged
end

local function UndoLastShareImport()
    if type(last_import_undo) ~= "table" then return false end
    local u = last_import_undo
    last_import_undo = nil
    for i = 1, #FLAG_KEYS_ORDER do
        local fk = FLAG_KEYS_ORDER[i]
        custom_vis_flags[fk] = u.flags[fk] == true
    end
    for i = 1, #TUNE_KEYS do
        local tk = TUNE_KEYS[i]
        vis_tune[tk] = ClampTuneValue(tk, u.tune[tk])
    end
    current_visual_preset = u.preset or 0
    current_scheme_name = u.name or ""
    visual_bench_timer = 12.0
    SyncToNativePlugin(true)
    ExportJsonPresetLibrary()
    hud_dirty_now = true
    return true
end

local function TryWriteClipboard(str)
    local fnCb = rawget(GLOBAL, "HyperSmooth_SetClipboard")
    if type(fnCb) == "function" then
        local ok, res = pcall(fnCb, str)
        if ok and res == true then return true end
    end
    return false
end

local function TryReadClipboard()
    if TheSim and type(TheSim.GetClipboardData) == "function" then
        local ok, txt = pcall(TheSim.GetClipboardData, TheSim)
        if ok and type(txt) == "string" then
            return txt
        end
    end
    return ""
end

rawset(GLOBAL, "c_hsshare", function(arg1, arg2)
    local ui_zh = ResolveUiIsZh()
    if type(arg1) == "string" then
        local a1_trim = arg1:match("^%s*(.-)%s*$") or ""
        local a1_lo = a1_trim:lower()
        if a1_lo == "undo" then
            local ok = UndoLastShareImport()
            GLOBAL.print(ok
                and (ui_zh and "[HyperSmooth 分享码] 已撤销上次导入，恢复原配置" or "[HyperSmooth Share] Undid last import")
                or (ui_zh and "[HyperSmooth 分享码] 没有可撤销的导入记录" or "[HyperSmooth Share] Nothing to undo"))
            return ok
        elseif a1_lo == "check" and type(arg2) == "string" then
            local ok, err, st = ValidateSharePayload(arg2)
            if ok and st then
                local title_s = (#st.name > 0) and (" [" .. st.name .. "]") or ""
                GLOBAL.print(ui_zh
                    and string.format("[HyperSmooth 校验通过]%s 基底=预设%d 改动=%d项（未覆盖当前配置）", title_s, st.base_id, st.diff_count)
                    or string.format("[HyperSmooth VALID]%s base=preset%d diffs=%d (dry-run)", title_s, st.base_id, st.diff_count))
            else
                GLOBAL.print("[HyperSmooth] " .. tostring(err))
            end
            return ok
        elseif a1_trim:sub(1, 3) == "HS1" or a1_trim:sub(1, 2) == "HS" or a1_trim:sub(1, 1) == "{" then
            local ok, err, st = ImportSharePayloadTransactional(a1_trim)
            if ok and st then
                local title_s = (#st.name > 0) and (" [" .. st.name .. "]") or ""
                GLOBAL.print(ui_zh
                    and string.format("[HyperSmooth 分享码] 已校验并导入%s（应用 %d 项参数并保存；可 c_hsshare('undo') 撤销）", title_s, st.diff_count)
                    or string.format("[HyperSmooth Share] Validated & imported%s (%d params saved; c_hsshare('undo') to revert)", title_s, st.diff_count))
            else
                GLOBAL.print("[HyperSmooth] " .. tostring(err))
            end
            return ok
        end
    end
    -- Export current look (optional arg1 = scheme title e.g. c_hsshare("暗黑童话"))
    local code, diffs, cname = EncodeCurrentShareCode(type(arg1) == "string" and arg1 or nil)
    if #cname > 0 then current_scheme_name = cname end
    local copied = TryWriteClipboard(code)
    GLOBAL.print(string.format("[HyperSmooth Share] %s (%d chars, %d diffs%s)",
        code, #code, diffs, copied and ", COPIED TO CLIPBOARD" or ""))
    return code
end)

rawset(GLOBAL, "c_hsjson", function(action, name_arg)
    local ui_zh = ResolveUiIsZh()
    local act = type(action) == "string" and action:lower() or "export"
    if act == "export" or act == "save" then
        if act == "save" and type(name_arg) == "string" and #SanitizeSchemeName(name_arg) > 0 then
            local cname = SanitizeSchemeName(name_arg)
            current_scheme_name = cname
            local fl = GetActiveVisualFlags()
            local tn = GetActiveVisTune()
            local copy_tn = {}
            for i = 1, #TUNE_KEYS do copy_tn[TUNE_KEYS[i]] = ClampTuneValue(TUNE_KEYS[i], tn[TUNE_KEYS[i]]) end
            local entry = { name = cname, mask = EncodeCustomVisMask(fl), tune = copy_tn }
            local replaced = false
            for i = 1, #json_named_library do
                if json_named_library[i].name == cname then
                    json_named_library[i] = entry
                    replaced = true
                    break
                end
            end
            if not replaced then json_named_library[#json_named_library + 1] = entry end
        end
        local _, cnt = ExportJsonPresetLibrary()
        GLOBAL.print(ui_zh
            and string.format("[HyperSmooth JSON] 已写入 %s（共 %d 套方案）", JSON_LIB_FILENAME, cnt)
            or string.format("[HyperSmooth JSON] Exported %s (%d presets)", JSON_LIB_FILENAME, cnt))
        return true
    elseif act == "reload" then
        ReloadJsonPresetLibrary(function(ok, cnt)
            GLOBAL.print(ok
                and (ui_zh and string.format("[HyperSmooth JSON] 已热重载 %s（校验载入 %d 套方案）", JSON_LIB_FILENAME, cnt)
                          or string.format("[HyperSmooth JSON] Reloaded %s (%d valid presets)", JSON_LIB_FILENAME, cnt))
                or (ui_zh and ("[HyperSmooth JSON] 未找到或格式损坏：" .. JSON_LIB_FILENAME)
                          or ("[HyperSmooth JSON] Missing or invalid: " .. JSON_LIB_FILENAME)))
        end)
        return true
    elseif act == "list" then
        local names = {}
        for sidx = 1, 3 do
            if custom_slots[sidx] then names[#names + 1] = custom_slots[sidx].name or ("Slot_" .. sidx) end
        end
        for i = 1, #json_named_library do
            names[#names + 1] = json_named_library[i].name
        end
        GLOBAL.print("[HyperSmooth JSON Library] " .. (#names > 0 and table.concat(names, ", ") or "(empty)"))
        return names
    elseif act == "load" and type(name_arg) == "string" then
        local target = SanitizeSchemeName(name_arg)
        local found = nil
        for sidx = 1, 3 do
            local sl = custom_slots[sidx]
            if sl and ((sl.name or ("Slot_" .. sidx)) == target) then found = sl; break end
        end
        if not found then
            for i = 1, #json_named_library do
                if json_named_library[i].name == target then found = json_named_library[i]; break end
            end
        end
        if not found then
            GLOBAL.print((ui_zh and "[HyperSmooth JSON] 未找到方案: " or "[HyperSmooth JSON] Preset not found: ") .. target)
            return false
        end
        local code = EncodeShareCodeFromState(DecodeFlagsMaskToTable(found.mask or 0), found.tune, target)
        local ok, err = ImportSharePayloadTransactional(code)
        GLOBAL.print(ok
            and ((ui_zh and "[HyperSmooth JSON] 已载入方案: " or "[HyperSmooth JSON] Loaded preset: ") .. target)
            or ("[HyperSmooth JSON] " .. tostring(err)))
        return ok
    end
    GLOBAL.print(ui_zh
        and "[HyperSmooth JSON] 用法: c_hsjson('export'|'reload'|'list'|'save'|'load', [方案名])"
        or "[HyperSmooth JSON] Usage: c_hsjson('export'|'reload'|'list'|'save'|'load', [name])")
end)

rawset(GLOBAL, "c_hsbench", function(on)
    if on == nil then
        visual_bench_pinned = not visual_bench_pinned
    else
        visual_bench_pinned = on and true or false
    end
    hud_dirty_now = true
    local ui_zh = ResolveUiIsZh()
    GLOBAL.print(ui_zh
        and ("[HyperSmooth] HUD 显卡/显存测试条 -> " .. (visual_bench_pinned and "常驻开启" or "仅切画质时显示"))
        or ("[HyperSmooth] HUD GPU/VRAM benchmark strip -> " .. (visual_bench_pinned and "PINNED ON" or "AUTO")))
end)

--------------------------------------------------------------------------------
-- 1. Fast Mod Scan: cache parsed modinfo for disabled mods so switching menus
--    doesn't re-read & re-compile 70+ unused modinfo.lua files from disk.
--    v2.2.1: preserves savedata.known_mods[modname].modinfo write-back and
--    bypasses cache on ServerCreationScreen / game_modes mods (100% DSA safe).
--    v2.6.0.2: NEVER intercept UpdateSingleModInfo / DoesModExist / server-join
--    verification (networking.lua:L413,L440,L443) where disabled server mods
--    are refreshed from disk after Workshop download -> fixes false
--    SERVER_MODS_WORKSHOP_VERSION_MISMATCH ("服务器正在运行旧版本模组")!
--------------------------------------------------------------------------------
if FAST_MOD_SCAN and KnownModIndex and KnownModIndex.LoadModInfo then
    local fast_scan_bypass = 0
    local function WrapBypass(fn_name)
        local orig = KnownModIndex[fn_name]
        if type(orig) ~= "function" then return end
        KnownModIndex[fn_name] = function(self, ...)
            fast_scan_bypass = fast_scan_bypass + 1
            local ok, r1, r2, r3, r4 = pcall(orig, self, ...)
            fast_scan_bypass = fast_scan_bypass - 1
            if not ok then
                GLOBAL.error(r1)
            end
            return r1, r2, r3, r4
        end
    end
    WrapBypass("UpdateSingleModInfo")
    WrapBypass("DoesModExist")

    local old_LoadModInfo = KnownModIndex.LoadModInfo
    KnownModIndex.LoadModInfo = function(self, modname, prev_info)
        if fast_scan_bypass == 0 and prev_info ~= nil and modname ~= nil
            and prev_info.version ~= nil and prev_info.game_modes == nil then
            local fe = GLOBAL.TheFrontEnd
            local scr = fe and fe:GetActiveScreen()
            local sname = scr ~= nil and scr.name or nil
            -- Only use cached modinfo on idle menu screens (MainScreen / ModsScreen).
            -- Always read fresh modinfo.lua on ServerListingScreen, ServerCreationScreen,
            -- ConnectingToGamePopup, PopupDialogScreen ("Downloading Mods"), or in-game!
            local allow_menu_cache = (sname == "MainScreen" or sname == "ModsScreen")
            if allow_menu_cache and modname ~= "workshop-2657513551" then
                local enabled = (self.IsModEnabledAny and self:IsModEnabledAny(modname))
                    or (self.IsModTempEnabled and self:IsModTempEnabled(modname))
                    or (self.IsModForceEnabled and self:IsModForceEnabled(modname))
                if not enabled then
                    if self.savedata and self.savedata.known_mods and self.savedata.known_mods[modname] then
                        self.savedata.known_mods[modname].modinfo = prev_info
                    end
                    return prev_info
                end
            end
        end
        return old_LoadModInfo(self, modname, prev_info)
    end
end

--------------------------------------------------------------------------------
-- 2. Skip Top Popular Mods Steam Query ("Getting top mod details...", 3-13s)
--------------------------------------------------------------------------------
if SKIP_TOP_MODS_QUERY and TheSim and getmetatable then
    pcall(function()
        local mt = getmetatable(TheSim)
        local idx = mt and mt.__index
        if type(idx) == "table" and type(idx.QueryTopMods) == "function" then
            idx.QueryTopMods = function(_, cb)
                if type(cb) == "function" then
                    pcall(cb, {}, false, 0)
                end
            end
        end
    end)
end

--------------------------------------------------------------------------------
-- 3. Pure-Client Zero-Reload Cave <-> Surface Acceleration (Cross-SimReset!)
--    v2.4.1 FIX: Hook SimReset() to persist _hakimi_kept_worlds AND
--    _hakimi_kept_chars inside instanceparameters across TheSim:Reset()!
--    Keeps vanilla "forest"/"cave" AND active character prefabs hot in client
--    RAM across BACKEND shard migrations; cleanly unloads ALL on FRONTEND.
--------------------------------------------------------------------------------
if KEEP_SHARD_ASSETS and TheSim and getmetatable then
    pcall(function()
        local mt = getmetatable(TheSim)
        local idx = mt and mt.__index
        if type(idx) ~= "table" or type(idx.LoadPrefabs) ~= "function" or type(idx.UnloadPrefabs) ~= "function" then
            return
        end

        local VANILLA_WORLDS = { forest = true, cave = true }
        local Settings = GLOBAL.Settings
        if Settings then
            if type(Settings._hakimi_kept_worlds) ~= "table" then Settings._hakimi_kept_worlds = {} end
            if type(Settings._hakimi_kept_chars)  ~= "table" then Settings._hakimi_kept_chars  = {} end
        end

        -- CRITICAL: Forward kept asset maps through SimReset(instanceparameters) so
        -- TheSim:Reset() VM rebuild NEVER wipes them!
        local orig_SimReset = rawget(GLOBAL, "SimReset")
        if type(orig_SimReset) == "function" then
            rawset(GLOBAL, "SimReset", function(instanceparameters, ...)
                if instanceparameters == nil then
                    instanceparameters = {}
                end
                local s = GLOBAL.Settings
                if s and s.current_asset_set ~= "FORCERESET" then
                    if type(s._hakimi_kept_worlds) == "table" then
                        instanceparameters._hakimi_kept_worlds = s._hakimi_kept_worlds
                    end
                    if type(s._hakimi_kept_chars) == "table" then
                        instanceparameters._hakimi_kept_chars = s._hakimi_kept_chars
                    end
                end
                return orig_SimReset(instanceparameters, ...)
            end)
        end

        local old_LoadPrefabs = idx.LoadPrefabs
        local old_UnloadPrefabs = idx.UnloadPrefabs

        local function IsPureMultiplayerClient(s)
            if not s or s.one_player_mode then
                return false
            end
            local net = GLOBAL.TheNet
            if net and net.GetIsServer and net:GetIsServer() then
                return false
            end
            return true
        end

        local function UnloadKeptChars(sim, s, exclude_list)
            if type(s._hakimi_kept_chars) ~= "table" then return end
            local seen = {}
            if type(exclude_list) == "table" then
                for i = 1, #exclude_list do seen[exclude_list[i]] = true end
            end
            local extras = {}
            for kc in pairs(s._hakimi_kept_chars) do
                if not seen[kc] then
                    seen[kc] = true
                    extras[#extras + 1] = kc
                end
            end
            s._hakimi_kept_chars = {}
            if #extras > 0 then
                pcall(old_UnloadPrefabs, sim, extras)
            end
        end

        local function UnloadAllKeptWorldsAndChars(sim, s, exclude_world)
            local extras = {}
            local seen = {}
            if exclude_world then seen[exclude_world] = true end
            if type(s._hakimi_kept_worlds) == "table" then
                for kw in pairs(s._hakimi_kept_worlds) do
                    if not seen[kw] then
                        seen[kw] = true
                        extras[#extras + 1] = kw
                    end
                end
                s._hakimi_kept_worlds = {}
            end
            if type(s._hakimi_kept_chars) == "table" then
                for kc in pairs(s._hakimi_kept_chars) do
                    if not seen[kc] then
                        seen[kc] = true
                        extras[#extras + 1] = kc
                    end
                end
                s._hakimi_kept_chars = {}
            end
            if #extras > 0 then
                pcall(old_UnloadPrefabs, sim, extras)
            end
        end

        idx.LoadPrefabs = function(sim, prefabs, ...)
            local s = GLOBAL.Settings
            if IsPureMultiplayerClient(s) and s.current_asset_set == "BACKEND" and type(prefabs) == "table" then
                -- 1) World layer (forest / cave): skip completely if already hot in RAM!
                if #prefabs == 1 and VANILLA_WORLDS[prefabs[1]] then
                    local w = prefabs[1]
                    s._hakimi_kept_worlds = (type(s._hakimi_kept_worlds) == "table") and s._hakimi_kept_worlds or {}
                    if s._hakimi_kept_worlds[w] then
                        return
                    end
                    s._hakimi_kept_worlds[w] = true
                    return old_LoadPrefabs(sim, prefabs, ...)
                end
                -- 2) Active character prefabs (gamelogic.lua:L426): filter out already-kept chars!
                if type(s._hakimi_kept_chars) == "table" and next(s._hakimi_kept_chars) ~= nil and #prefabs > 0 then
                    local any_kept = false
                    for i = 1, #prefabs do
                        if s._hakimi_kept_chars[prefabs[i]] then
                            any_kept = true
                            break
                        end
                    end
                    if any_kept then
                        local remaining = {}
                        for i = 1, #prefabs do
                            local p = prefabs[i]
                            if not s._hakimi_kept_chars[p] then
                                remaining[#remaining + 1] = p
                            end
                        end
                        if #remaining == 0 then
                            return
                        end
                        local ret = old_LoadPrefabs(sim, remaining, ...)
                        if type(s.loaded_characters) == "table" then
                            local is_char = {}
                            for i = 1, #s.loaded_characters do is_char[s.loaded_characters[i]] = true end
                            for i = 1, #remaining do
                                if is_char[remaining[i]] then
                                    s._hakimi_kept_chars[remaining[i]] = true
                                end
                            end
                        end
                        return ret
                    end
                end
            end
            return old_LoadPrefabs(sim, prefabs, ...)
        end

        idx.UnloadPrefabs = function(sim, prefabs, ...)
            local s = GLOBAL.Settings
            if s and type(prefabs) == "table" then
                -- A) Returning to FRONTEND / FORCERESET: cleanly unload all kept worlds & chars (zero double-unload!)
                if s.current_asset_set == "FRONTEND" or s.current_asset_set == "FORCERESET" then
                    local excl = (#prefabs == 1 and VANILLA_WORLDS[prefabs[1]]) and prefabs[1] or nil
                    local ret = old_UnloadPrefabs(sim, prefabs, ...)
                    if excl then
                        UnloadAllKeptWorldsAndChars(sim, s, excl)
                    elseif s.loaded_characters ~= nil and prefabs == s.loaded_characters then
                        UnloadKeptChars(sim, s, prefabs)
                    end
                    return ret
                end
                -- B) BACKEND -> BACKEND shard migration (Forest <-> Caves):
                if IsPureMultiplayerClient(s) and s.current_asset_set == "BACKEND" and s.last_asset_set == "BACKEND" then
                    if #prefabs == 1 and VANILLA_WORLDS[prefabs[1]] then
                        s._hakimi_kept_worlds = (type(s._hakimi_kept_worlds) == "table") and s._hakimi_kept_worlds or {}
                        s._hakimi_kept_worlds[prefabs[1]] = true
                        return
                    end
                    if s.loaded_characters ~= nil and prefabs == s.loaded_characters then
                        s._hakimi_kept_chars = (type(s._hakimi_kept_chars) == "table") and s._hakimi_kept_chars or {}
                        for i = 1, #prefabs do
                            s._hakimi_kept_chars[prefabs[i]] = true
                        end
                        return
                    end
                end
            end
            return old_UnloadPrefabs(sim, prefabs, ...)
        end
    end)
end

--------------------------------------------------------------------------------
-- 4. Skip Blocking MOTD Network Wait: mark MOTD ready from local cache immediately.
--    Does NOT affect Steam/Klei online login, skins, or multiplayer.
--------------------------------------------------------------------------------
if SKIP_MOTD_DELAY then
    AddClassPostConstruct("motdmanager", function(self)
        local old_DownloadMotdInfo = self.DownloadMotdInfo
        if old_DownloadMotdInfo then
            self.DownloadMotdInfo = function(mgr, remaining_retries)
                if mgr.LoadCachedImages then
                    pcall(mgr.LoadCachedImages, mgr)
                end
                if mgr.SetLoadingDone and not mgr.is_loading_done then
                    pcall(mgr.SetLoadingDone, mgr)
                end
                return
            end
        end
    end)
end

--------------------------------------------------------------------------------
-- 5. Frame-Rate-Independent Exponential Damping + 240Hz Phase-Locked FollowCamera
--    a) Exponential Damping: calibrated to 60Hz reference so 120/144/240Hz have
--       identical pan/zoom/rotate feel.
--    b) [v2.6.0 Pillar 1] 240Hz Phase-Locked Lag-Comp-OFF Camera Target Smoothing:
--       When Lag Compensation is OFF (target == ThePlayer & locomotor == nil) and
--       240Hz interpolation is active, Klei's FollowCamera:Update still reads raw
--       30Hz Transform:GetWorldPosition(), causing subtle 30Hz background judder.
--       Phase-locking camera tracking to the exact C0 segment curve (auto-bypassing
--       >111Hz continuous physics, snapping on teleports / Snap() / target change,
--       and preserving 100% of focalpoint, screenoffset & shake) syncs Sprite +
--       Dynamic Light + Camera onto one single 240Hz curve!
--------------------------------------------------------------------------------
do
    local REF_FPS = 60.0
    local REF_DT = 1.0 / REF_FPS

    local function EffectiveGain(gain, dt)
        if gain == nil or gain <= 0 or dt <= 0 then
            return gain
        end
        local alpha60 = gain * REF_DT
        if alpha60 >= 0.995 then
            return gain
        end
        local alpha_dt = 1.0 - math.pow(1.0 - alpha60, dt * REF_FPS)
        return alpha_dt / dt
    end

    AddClassPostConstruct("cameras/followcamera", function(self)
        local old_Update = self.Update
        if not old_Update then
            return
        end

        local ct_target = nil
        local ct_clock = 0.0
        local ct_last_t = 0.0
        local ct_snap_t = 0.0
        local ct_period = 1.0 / 30.0
        local ct_fast_streak = 0
        local ct_prev_x, ct_prev_y, ct_prev_z = 0.0, 0.0, 0.0
        local ct_raw_x, ct_raw_y, ct_raw_z = 0.0, 0.0, 0.0

        local old_Snap = self.Snap
        if type(old_Snap) == "function" then
            self.Snap = function(cam, ...)
                ct_target = nil
                return old_Snap(cam, ...)
            end
        end

        self.Update = function(cam, dt, dontupdatepos)
            if dt == nil or dt <= 0 or dt > 0.25 or cam.paused then
                ct_target = nil
                return old_Update(cam, dt, dontupdatepos)
            end

            ct_clock = ct_clock + dt
            local dx, dy, dz = 0.0, 0.0, 0.0
            local cam240_active = current_skeletal_tween
                and not dontupdatepos
                and not cam.cutscene
                and cam.target ~= nil
                and cam.target == GLOBAL.ThePlayer
                and cam.target.Transform ~= nil
                and cam.target.components ~= nil
                and cam.target.components.locomotor == nil

            if cam240_active then
                local rx, ry, rz = cam.target.Transform:GetWorldPosition()
                if rx == nil then
                    ct_target = nil
                    cam240_active = false
                elseif ct_target ~= cam.target or (ct_clock - ct_last_t) > 0.16 then
                    ct_target = cam.target
                    ct_prev_x, ct_prev_y, ct_prev_z = rx, ry, rz
                    ct_raw_x, ct_raw_y, ct_raw_z = rx, ry, rz
                    ct_snap_t = ct_clock
                    ct_last_t = ct_clock
                    ct_period = 1.0 / 30.0
                    ct_fast_streak = 0
                else
                    local mx = rx - ct_raw_x
                    local my = ry - ct_raw_y
                    local mz = rz - ct_raw_z
                    local move_sq = mx * mx + my * my + mz * mz
                    if move_sq > 25.0 then
                        -- Teleport / wormhole / cave jump (>5 units): hard snap
                        ct_prev_x, ct_prev_y, ct_prev_z = rx, ry, rz
                        ct_raw_x, ct_raw_y, ct_raw_z = rx, ry, rz
                        ct_snap_t = ct_clock
                        ct_fast_streak = 0
                    elseif move_sq > 1e-6 then
                        local gap = ct_clock - ct_snap_t
                        local prev_a = (ct_period > 1e-4) and math.min(1.0, math.max(0.0, gap / ct_period)) or 1.0
                        local disp_x = ct_prev_x + (ct_raw_x - ct_prev_x) * prev_a
                        local disp_y = ct_prev_y + (ct_raw_y - ct_prev_y) * prev_a
                        local disp_z = ct_prev_z + (ct_raw_z - ct_prev_z) * prev_a
                        if gap < 0.009 then
                            ct_fast_streak = math.min(8, ct_fast_streak + 1)
                        else
                            ct_fast_streak = 0
                            if gap >= 0.014 and gap <= 0.12 then
                                local clamped_gap = math.min(0.095, math.max(0.015, gap))
                                ct_period = ct_period * 0.70 + clamped_gap * 0.30
                            end
                        end
                        ct_prev_x, ct_prev_y, ct_prev_z = disp_x, disp_y, disp_z
                        ct_raw_x, ct_raw_y, ct_raw_z = rx, ry, rz
                        ct_snap_t = ct_clock
                    end

                    if ct_fast_streak < 2 then
                        local elapsed = ct_clock - ct_snap_t
                        local alpha = (ct_period > 1e-4) and math.min(1.0, math.max(0.0, elapsed / ct_period)) or 1.0
                        local sx = ct_prev_x + (ct_raw_x - ct_prev_x) * alpha
                        local sy = ct_prev_y + (ct_raw_y - ct_prev_y) * alpha
                        local sz = ct_prev_z + (ct_raw_z - ct_prev_z) * alpha
                        dx = sx - rx
                        dy = sy - ry
                        dz = sz - rz
                    end
                    ct_last_t = ct_clock
                end
            else
                ct_target = nil
            end

            local orig_pan = cam.pangain
            local orig_heading = cam.headinggain
            local orig_dist = cam.distancegain
            local orig_cdist = cam.continuousdistancegain
            local eff_pan = orig_pan

            if SMOOTH_CAMERA then
                eff_pan = EffectiveGain(orig_pan, dt)
                cam.pangain = eff_pan
                cam.headinggain = EffectiveGain(orig_heading, dt)
                cam.distancegain = EffectiveGain(orig_dist, dt)
                cam.continuousdistancegain = EffectiveGain(orig_cdist, dt)
            end

            old_Update(cam, dt, dontupdatepos)

            if SMOOTH_CAMERA then
                cam.pangain = orig_pan
                cam.headinggain = orig_heading
                cam.distancegain = orig_dist
                cam.continuousdistancegain = orig_cdist
            end

            -- Algebraically identical to lerp(orig_c, smooth + offset, alpha_p) + shake!
            if cam240_active and (dx ~= 0.0 or dy ~= 0.0 or dz ~= 0.0) and cam.currentpos ~= nil then
                local alpha_p = (type(eff_pan) == "number" and eff_pan > 0) and math.min(1.0, dt * eff_pan) or 0.0
                if alpha_p > 0.0 then
                    cam.currentpos.x = cam.currentpos.x + dx * alpha_p
                    cam.currentpos.y = cam.currentpos.y + dy * alpha_p
                    cam.currentpos.z = cam.currentpos.z + dz * alpha_p
                    if cam.targetpos ~= nil then
                        cam.targetpos.x = cam.targetpos.x + dx
                        cam.targetpos.y = cam.targetpos.y + dy
                        cam.targetpos.z = cam.targetpos.z + dz
                    end
                    latency_stats.cam240_frames = latency_stats.cam240_frames + 1
                    if type(cam.Apply) == "function" then
                        cam:Apply()
                    end
                end
            end
        end
    end)
end

--------------------------------------------------------------------------------
-- 6. High-Refresh-Rate (120~360Hz) Vanilla DST Bug Fixes:
--    a) TrueScrollList & ChatScrollList: replace hardcoded 0.25 Lerp (4x too fast @ 240Hz)
--       with exact 60Hz-calibrated exponential damping (silky smooth crafting/mods/chat scroll).
--    b) MapScreen: fix hardcoded 1/60 zoom step (4x too fast @ 240Hz).
--    c) DeployHelper: fix frame-counted delay=2 decay (eliminates placement ring flicker @ 240Hz).
--------------------------------------------------------------------------------
if SMOOTH_UI_240HZ then
    local function WrapSmoothScrollList(self)
        local old_OnUpdate = self.OnUpdate
        if not old_OnUpdate then
            return
        end
        self.OnUpdate = function(list, dt)
            if dt and dt > 0 and dt < 0.05 and list.current_scroll_pos and list.target_scroll_pos then
                local diff = list.target_scroll_pos - list.current_scroll_pos
                if math.abs(diff) > 0.01 then
                    local alpha_dt = 1.0 - math.pow(0.75, dt * 60.0)
                    local scale = alpha_dt * 4.0 -- alpha_dt / 0.25
                    if scale < 0.98 then
                        local orig_target = list.target_scroll_pos
                        local fake_target = list.current_scroll_pos + diff * scale
                        if math.abs(fake_target - list.current_scroll_pos) > 0.011 then
                            list.target_scroll_pos = fake_target
                            old_OnUpdate(list, dt)
                            local delta = list.target_scroll_pos - fake_target
                            list.target_scroll_pos = orig_target + delta
                            return
                        end
                    end
                end
            end
            return old_OnUpdate(list, dt)
        end
    end

    AddClassPostConstruct("widgets/truescrolllist", WrapSmoothScrollList)
    AddClassPostConstruct("widgets/redux/chatscrolllist", WrapSmoothScrollList)

    -- TrueScrollArea (Cookbook / Scrapbook / Mod Settings / Skins / Lore panels):
    -- Vanilla hardcodes blend_weight = 0.7 (alpha = 0.30/frame -> 4x too abrupt @ 240Hz!).
    AddClassPostConstruct("widgets/truescrollarea", function(self)
        local old_OnUpdate = self.OnUpdate
        if not old_OnUpdate then
            return
        end
        self.OnUpdate = function(area, dt)
            if dt and dt > 0 and dt < 0.05 and area.current_scroll_pos and area.target_scroll_pos then
                local diff = area.target_scroll_pos - area.current_scroll_pos
                if math.abs(diff) > 0.01 then
                    local alpha_dt = 1.0 - math.pow(0.70, dt * 60.0)
                    local scale = alpha_dt / 0.30
                    if scale < 0.98 then
                        local orig_target = area.target_scroll_pos
                        local fake_target = area.current_scroll_pos + diff * scale
                        if math.abs(fake_target - area.current_scroll_pos) > 0.035 then
                            area.target_scroll_pos = fake_target
                            old_OnUpdate(area, dt)
                            local delta = area.target_scroll_pos - fake_target
                            area.target_scroll_pos = orig_target + delta
                            return
                        end
                    end
                end
            end
            return old_OnUpdate(area, dt)
        end
    end)

    -- FollowText (Speech Bubbles & Floating Character/Mob Labels):
    -- Vanilla queries raw 30Hz GetSymbolPosition / GetWorldPosition every frame,
    -- making speech bubbles judder at 30Hz above smoothly gliding 240Hz characters!
    AddClassPostConstruct("widgets/followtext", function(self)
        local old_OnUpdate = self.OnUpdate
        if not old_OnUpdate then
            return
        end
        local ft_target = nil
        local ft_clock = 0.0
        local ft_last_t = 0.0
        local ft_snap_t = 0.0
        local ft_period = 1.0 / 30.0
        local ft_fast_streak = 0
        local ft_prev_x, ft_prev_y, ft_prev_z = 0.0, 0.0, 0.0
        local ft_raw_x, ft_raw_y, ft_raw_z = 0.0, 0.0, 0.0

        local old_SetTarget = self.SetTarget
        if type(old_SetTarget) == "function" then
            self.SetTarget = function(ft, target, ...)
                ft_target = nil
                return old_SetTarget(ft, target, ...)
            end
        end

        self.OnUpdate = function(ft, dt)
            if not current_skeletal_tween or dt == nil or dt <= 0 or dt > 0.25
                or ft.target == nil or not ft.target:IsValid() then
                ft_target = nil
                return old_OnUpdate(ft, dt)
            end

            if not ft.hashud and GLOBAL.TheFrontEnd ~= nil then
                ft.text:SetScale(GLOBAL.TheFrontEnd:GetHUDScale())
            end

            local rx, ry, rz
            if ft.target.AnimState ~= nil then
                rx, ry, rz = ft.target.AnimState:GetSymbolPosition(
                    ft.symbol or "", ft.offset.x, ft.offset.y, ft.offset.z)
            elseif ft.target.Transform ~= nil then
                rx, ry, rz = ft.target.Transform:GetWorldPosition()
            end
            if rx == nil then
                ft_target = nil
                return old_OnUpdate(ft, dt)
            end

            ft_clock = ft_clock + dt
            local sx, sy, sz = rx, ry, rz
            if ft_target ~= ft.target or (ft_clock - ft_last_t) > 0.16 then
                ft_target = ft.target
                ft_prev_x, ft_prev_y, ft_prev_z = rx, ry, rz
                ft_raw_x, ft_raw_y, ft_raw_z = rx, ry, rz
                ft_snap_t = ft_clock
                ft_last_t = ft_clock
                ft_period = 1.0 / 30.0
                ft_fast_streak = 0
            else
                local mx = rx - ft_raw_x
                local my = ry - ft_raw_y
                local mz = rz - ft_raw_z
                local move_sq = mx * mx + my * my + mz * mz
                if move_sq > 25.0 then
                    ft_prev_x, ft_prev_y, ft_prev_z = rx, ry, rz
                    ft_raw_x, ft_raw_y, ft_raw_z = rx, ry, rz
                    ft_snap_t = ft_clock
                    ft_fast_streak = 0
                elseif move_sq > 1e-6 then
                    local gap = ft_clock - ft_snap_t
                    local prev_a = (ft_period > 1e-4) and math.min(1.0, math.max(0.0, gap / ft_period)) or 1.0
                    local disp_x = ft_prev_x + (ft_raw_x - ft_prev_x) * prev_a
                    local disp_y = ft_prev_y + (ft_raw_y - ft_prev_y) * prev_a
                    local disp_z = ft_prev_z + (ft_raw_z - ft_prev_z) * prev_a
                    if gap < 0.009 then
                        ft_fast_streak = math.min(8, ft_fast_streak + 1)
                    else
                        ft_fast_streak = 0
                        if gap >= 0.014 and gap <= 0.12 then
                            local clamped_gap = math.min(0.095, math.max(0.015, gap))
                            ft_period = ft_period * 0.70 + clamped_gap * 0.30
                        end
                    end
                    ft_prev_x, ft_prev_y, ft_prev_z = disp_x, disp_y, disp_z
                    ft_raw_x, ft_raw_y, ft_raw_z = rx, ry, rz
                    ft_snap_t = ft_clock
                end

                if ft_fast_streak < 2 then
                    local elapsed = ft_clock - ft_snap_t
                    local alpha = (ft_period > 1e-4) and math.min(1.0, math.max(0.0, elapsed / ft_period)) or 1.0
                    if alpha < 1.0 then
                        sx = ft_prev_x + (ft_raw_x - ft_prev_x) * alpha
                        sy = ft_prev_y + (ft_raw_y - ft_prev_y) * alpha
                        sz = ft_prev_z + (ft_raw_z - ft_prev_z) * alpha
                        latency_stats.followtext_240 = latency_stats.followtext_240 + 1
                    end
                end
                ft_last_t = ft_clock
            end

            local x, y = TheSim:GetScreenPos(sx, sy, sz)
            ft:SetPosition(x + ft.screen_offset.x, y + ft.screen_offset.y, 0)
        end
    end)

    AddClassPostConstruct("screens/mapscreen", function(self)
        local old_OnUpdate = self.OnUpdate
        if not old_OnUpdate then
            return
        end

        local in_onupdate = false
        local frame_skip = false
        local hs_ctrl_gen = 0
        local hs_moved_gen = 0
        local hs_clock = 0.0
        local last_eval_t = -1.0
        local last_mx, last_my = nil, nil
        local last_zoom = nil
        local last_moved_gen = -1
        local last_ctrl_gen = -1
        local last_wx, last_wy, last_wz = nil, nil, nil
        local last_aax, last_aay, last_aaz = nil, nil, nil
        local last_lmb, last_rmb = nil, nil
        local has_cached_actions = false

        local old_OnControl = self.OnControl
        if type(old_OnControl) == "function" then
            self.OnControl = function(scr, control, down, ...)
                hs_ctrl_gen = hs_ctrl_gen + 1
                frame_skip = false
                return old_OnControl(scr, control, down, ...)
            end
        end

        local old_OnMinimapMoved = self.OnMinimapMoved
        if type(old_OnMinimapMoved) == "function" then
            self.OnMinimapMoved = function(scr, ...)
                hs_moved_gen = hs_moved_gen + 1
                return old_OnMinimapMoved(scr, ...)
            end
        end

        local old_OnBecomeActive = self.OnBecomeActive
        if type(old_OnBecomeActive) == "function" then
            self.OnBecomeActive = function(scr, ...)
                last_eval_t = -1.0
                has_cached_actions = false
                hs_ctrl_gen = hs_ctrl_gen + 1
                return old_OnBecomeActive(scr, ...)
            end
        end

        local old_AutoAim = self.AutoAimToStaticDecorations
        if type(old_AutoAim) == "function" then
            self.AutoAimToStaticDecorations = function(scr, x, y, z, ...)
                local dd = scr.decorationdata
                local zoom = (scr.minimap and scr.minimap.GetZoom) and scr.minimap:GetZoom() or nil
                if in_onupdate and frame_skip
                    and (dd == nil or (not dd.dirty and not dd.alwaysdirty))
                    and zoom == last_zoom
                    and x == last_wx and y == last_wy and z == last_wz
                    and last_aax ~= nil then
                    return last_aax, last_aay, last_aaz
                end
                local aax, aay, aaz = old_AutoAim(scr, x, y, z, ...)
                last_wx, last_wy, last_wz = x, y, z
                last_aax, last_aay, last_aaz = aax, aay, aaz
                return aax, aay, aaz
            end
        end

        local old_UpdateMapActions = self.UpdateMapActions
        if type(old_UpdateMapActions) == "function" then
            self.UpdateMapActions = function(scr, aax, aay, aaz, ...)
                local dd = scr.decorationdata
                local zoom = (scr.minimap and scr.minimap.GetZoom) and scr.minimap:GetZoom() or nil
                if in_onupdate and frame_skip and has_cached_actions
                    and (dd == nil or (not dd.dirty and not dd.alwaysdirty))
                    and zoom == last_zoom
                    and aax == last_aax and aaz == last_aaz then
                    return last_lmb, last_rmb
                end
                local lmb, rmb = old_UpdateMapActions(scr, aax, aay, aaz, ...)
                last_lmb, last_rmb = lmb, rmb
                has_cached_actions = true
                last_eval_t = hs_clock
                last_zoom = zoom
                last_moved_gen = hs_moved_gen
                last_ctrl_gen = hs_ctrl_gen
                return lmb, rmb
            end
        end

        self.OnUpdate = function(scr, dt)
            local step_dt = (type(dt) == "number" and dt >= 0 and dt < 1.0) and dt or 0.0
            hs_clock = hs_clock + step_dt
            local GetTimeRealSeconds = rawget(GLOBAL, "GetTimeRealSeconds")
            if type(GetTimeRealSeconds) == "function" then
                local real_t = GetTimeRealSeconds()
                if type(real_t) == "number" and real_t > 0 then
                    hs_clock = real_t
                end
            end

            local sim = rawget(GLOBAL, "TheSim")
            local mx, my = nil, nil
            if sim and type(sim.GetPosition) == "function" then
                mx, my = sim:GetPosition()
            end
            local zoom = (scr.minimap and scr.minimap.GetZoom) and scr.minimap:GetZoom() or nil
            local dd = scr.decorationdata
            local stationary = (not scr.zoom_target_time or scr.zoom_target_time <= 0)
                and (dd == nil or (not dd.dirty and not dd.alwaysdirty))
                and mx == last_mx and my == last_my
                and zoom == last_zoom
                and hs_moved_gen == last_moved_gen
                and hs_ctrl_gen == last_ctrl_gen
                and has_cached_actions
                and last_eval_t >= 0.0
                and (hs_clock - last_eval_t) >= 0.0
                and (hs_clock - last_eval_t) < 0.0333
                and last_wx ~= nil

            in_onupdate = true
            frame_skip = stationary
            if stationary then
                latency_stats.map_skip = latency_stats.map_skip + 1
                rawset(GLOBAL, "HS_MAP_SKIP_COUNT", latency_stats.map_skip)
            else
                last_mx, last_my = mx, my
            end

            local orig_sens = scr.zoomsensitivity
            local ok, err
            if type(orig_sens) == "number" and type(dt) == "number" and dt > 0 and dt < 0.25 then
                local factor = dt * 60.0
                if factor > 2.0 then factor = 2.0 end
                scr.zoomsensitivity = orig_sens * factor
                ok, err = pcall(old_OnUpdate, scr, dt)
                scr.zoomsensitivity = orig_sens
            else
                ok, err = pcall(old_OnUpdate, scr, dt)
            end

            in_onupdate = false
            frame_skip = false
            if not ok then
                GLOBAL.error(err)
            end
        end
    end)

    AddComponentPostInit("placer", function(self)
        local old_TestCanBuild = self.TestCanBuild
        if type(old_TestCanBuild) ~= "function" then
            return
        end
        self.TestCanBuild = function(cmp, ...)
            local sim = rawget(GLOBAL, "TheSim")
            local tick = (sim and type(sim.GetTick) == "function") and sim:GetTick() or nil
            local inst = cmp.inst
            local tf = inst and inst.Transform or nil
            if tick ~= nil and tf and type(tf.GetWorldPosition) == "function" then
                local x, y, z = tf:GetWorldPosition()
                if type(x) == "number" and type(z) == "number" then
                    local rot = (type(tf.GetRotation) == "function") and tf:GetRotation() or 0
                    local qx = math.floor(x * 1000 + 0.5)
                    local qy = math.floor((type(y) == "number" and y or 0) * 1000 + 0.5)
                    local qz = math.floor(z * 1000 + 0.5)
                    local qrot = math.floor((type(rot) == "number" and rot or 0) * 100 + 0.5)
                    local inp = rawget(GLOBAL, "TheInput")
                    local hud_ent = (inp and type(inp.GetHUDEntityUnderMouse) == "function") and inp:GetHUDEntityUnderMouse() or nil
                    local world_ent = (inp and type(inp.GetWorldEntityUnderMouse) == "function") and inp:GetWorldEntityUnderMouse() or nil
                    local builder = cmp.builder
                    local inv_rep = builder and builder.inst and builder.inst.replica and builder.inst.replica.inventory or nil
                    local active_item = (inv_rep and type(inv_rep.GetActiveItem) == "function") and inv_rep:GetActiveItem() or nil
                    local mode_toggle = cmp.axisalignedplacementtoggle and true or false
                    if cmp._hs_tcb_tick == tick
                        and cmp._hs_tcb_qx == qx and cmp._hs_tcb_qy == qy and cmp._hs_tcb_qz == qz
                        and cmp._hs_tcb_qrot == qrot
                        and cmp._hs_tcb_hud == hud_ent and cmp._hs_tcb_went == world_ent
                        and cmp._hs_tcb_act == active_item and cmp._hs_tcb_tog == mode_toggle then
                        latency_stats.placer_skip = latency_stats.placer_skip + 1
                        rawset(GLOBAL, "HS_PLACER_SKIP_COUNT", latency_stats.placer_skip)
                        return cmp._hs_tcb_can, cmp._hs_tcb_blocked
                    end
                    local can_build, mouse_blocked = old_TestCanBuild(cmp, ...)
                    cmp._hs_tcb_tick = tick
                    cmp._hs_tcb_qx = qx
                    cmp._hs_tcb_qy = qy
                    cmp._hs_tcb_qz = qz
                    cmp._hs_tcb_qrot = qrot
                    cmp._hs_tcb_hud = hud_ent
                    cmp._hs_tcb_went = world_ent
                    cmp._hs_tcb_act = active_item
                    cmp._hs_tcb_tog = mode_toggle
                    cmp._hs_tcb_can = can_build
                    cmp._hs_tcb_blocked = mouse_blocked
                    return can_build, mouse_blocked
                end
            end
            return old_TestCanBuild(cmp, ...)
        end
    end)

    local function InstallDeployHelperDedup()
        local orig_fn = rawget(GLOBAL, "TriggerDeployHelpers")
        if type(orig_fn) ~= "function" or rawget(GLOBAL, "_HS_DEPLOY_WRAPPED") then
            return
        end
        rawset(GLOBAL, "_HS_DEPLOY_WRAPPED", true)
        local last_tick = -1
        local last_x, last_z, last_range = 0, 0, -1
        local last_recipe, last_placer = nil, nil
        local last_t = -1.0
        rawset(GLOBAL, "TriggerDeployHelpers", function(x, y, z, range, recipe, placerinst, ...)
            local sim = rawget(GLOBAL, "TheSim")
            local tick = (sim and type(sim.GetTick) == "function") and sim:GetTick() or nil
            if tick ~= nil and type(x) == "number" and type(z) == "number" then
                local GetTimeRealSeconds = rawget(GLOBAL, "GetTimeRealSeconds")
                local now = (type(GetTimeRealSeconds) == "function") and (GetTimeRealSeconds() or 0.0) or 0.0
                if tick == last_tick
                    and range == last_range and recipe == last_recipe and placerinst == last_placer
                    and ((x - last_x) * (x - last_x) + (z - last_z) * (z - last_z)) < 0.0625
                    and (now - last_t) >= 0.0 and (now - last_t) < 0.020 then
                    latency_stats.deploy_skip = latency_stats.deploy_skip + 1
                    rawset(GLOBAL, "HS_DEPLOY_SKIP_COUNT", latency_stats.deploy_skip)
                    return
                end
                last_tick = tick
                last_x, last_z, last_range = x, z, range
                last_recipe, last_placer = recipe, placerinst
                last_t = now
            end
            return orig_fn(x, y, z, range, recipe, placerinst, ...)
        end)
    end
    InstallDeployHelperDedup()
    AddSimPostInit(InstallDeployHelperDedup)

    local _dbg_tbl = rawget(GLOBAL, "debug")
    local _dbg_getinfo = (type(_dbg_tbl) == "table" and type(_dbg_tbl.getinfo) == "function") and _dbg_tbl.getinfo or nil
    local function IsVanillaComponentFn(fn, file_pat)
        if fn == nil or _dbg_getinfo == nil then
            return true
        end
        local info = _dbg_getinfo(fn, "S")
        local src = info ~= nil and info.source or nil
        if type(src) ~= "string" then
            return true
        end
        return src:find(file_pat) ~= nil
    end

    AddComponentPostInit("deployhelper", function(self)
        if not IsVanillaComponentFn(self.OnWallUpdate, "components[/\\]deployhelper%.lua") then
            -- Preserve third-party mod custom deployhelper:OnWallUpdate untouched
            return
        end
        self.OnWallUpdate = function(cmp, dt)
            local step = (dt and dt > 0 and dt < 0.25) and (dt * 60.0) or 1.0
            if cmp.delay ~= nil and cmp.delay > step then
                cmp.delay = cmp.delay - step
            else
                cmp:StopHelper()
            end
        end
    end)
end

--------------------------------------------------------------------------------
-- 7. 240Hz Zero-Churn CPU & GC Optimizations:
--    a) DistanceFade: inline scalar dot product (eliminates 240 Vector3 allocs/sec per tree/canopy).
--    b) Input:OnUpdate: lossless stationary hit-test cache (0ms latency on mouse/camera move
--       or UI screens; skips ~75% redundant 240Hz C++ GetEntitiesAtScreenPoint scans).
--    (v2.2.1+: removed HoverText SetString wrapper for 100% ShowMe / Insight RPC compatibility.)
--------------------------------------------------------------------------------
if REDUCE_240HZ_CHURN then
    local _dbg_getinfo_df = (type(GLOBAL.debug) == "table" and type(GLOBAL.debug.getinfo) == "function") and GLOBAL.debug.getinfo or nil
    AddComponentPostInit("distancefade", function(self)
        if self.OnWallUpdate ~= nil and _dbg_getinfo_df ~= nil then
            local info = _dbg_getinfo_df(self.OnWallUpdate, "S")
            local src = info ~= nil and info.source or nil
            if type(src) == "string" and src:find("components[/\\]distancefade%.lua") == nil then
                -- Preserve third-party mod custom distancefade:OnWallUpdate untouched
                return
            end
        end
        self.OnWallUpdate = function(cmp, dt)
            local player = GLOBAL.ThePlayer
            if not player then
                cmp.inst.AnimState:OverrideMultColour(1, 1, 1, 1)
                return
            end
            local cam = GLOBAL.TheCamera
            local camx, camy, camz = cam.currentpos:Get()
            local x, y, z = cmp.inst.Transform:GetWorldPosition()
            local down = cam:GetDownVec()
            local mody = (x - camx) * down.x + (y - camy) * down.y + (z - camz) * down.z
            local extrapercent = cmp.extrafn and cmp.extrafn(cmp.inst, dt) or 1
            if mody > cmp.range then
                mody = mody - cmp.range
                mody = math.min(mody, cmp.fadedist) * 1.7
                local percent = (1 - (mody / cmp.fadedist)) * extrapercent
                cmp.inst.AnimState:OverrideMultColour(1, 1, 1, percent)
            else
                cmp.inst.AnimState:OverrideMultColour(1, 1, 1, extrapercent)
            end
        end
    end)

    if TheInput and TheInput.OnUpdate then
        local old_InputOnUpdate = TheInput.OnUpdate
        local last_mx, last_my = nil, nil
        local last_tick = -1
        local last_cx, last_cy, last_cz = nil, nil, nil
        local last_heading, last_dist = nil, nil
        local last_scan_t = 0
        local GetTick = GLOBAL.GetTick
        local GetTimeRealSeconds = GLOBAL.GetTimeRealSeconds

        TheInput.OnUpdate = function(inp)
            if not inp.mouse_enabled then
                return old_InputOnUpdate(inp)
            end
            local fe = GLOBAL.TheFrontEnd
            local screen = fe and fe:GetActiveScreen()
            local world = GLOBAL.TheWorld
            local cam = GLOBAL.TheCamera
            if world and cam and cam.currentpos and screen and screen.name == "HUD" and GetTick and GetTimeRealSeconds then
                local mx, my = TheSim:GetPosition()
                local tick = GetTick()
                local cx, cy, cz = cam.currentpos:Get()
                local hd = cam.heading
                local ds = cam.distance
                local now = GetTimeRealSeconds()
                local hov = inp.hoverinst
                local hov_ok = (hov == nil) or (hov.IsValid and hov:IsValid())
                if hov_ok
                    and inp.entitiesundermouse ~= nil
                    and mx == last_mx and my == last_my
                    and tick == last_tick
                    and cx == last_cx and cy == last_cy and cz == last_cz
                    and hd == last_heading and ds == last_dist
                    and (now - last_scan_t) < 0.016 then
                    return
                end
                last_mx, last_my = mx, my
                last_tick = tick
                last_cx, last_cy, last_cz = cx, cy, cz
                last_heading, last_dist = hd, ds
                last_scan_t = now
            else
                last_mx = nil
            end
            return old_InputOnUpdate(inp)
        end
    end

    ----------------------------------------------------------------------------
    -- 7b. CraftingMenu Zero-Stall Overhaul (Eliminates Pickup/Chest/Combat Micro-Stutters)
    --     Vanilla CraftingMenuHUD:RebuildRecipes() invokes inventory:Has(prefab, amt, true)
    --     ~1,500-3,000 times on EVERY itemget / itemlose / stacksizechange / healthdelta /
    --     sanitydelta / open-chest event (even with the crafting menu CLOSED!), scanning
    --     ~38+ C++ net_entity slots per call (~57,000 C++ boundary crossings in 1 frame!),
    --     and then rebuilds hidden CraftingMenuWidget grids & details panels!
    --     Fix:
    --       1) Single-pass O(slots) crafting count snapshot when vanilla Has() is intact,
    --          auto-falling back to mathematically exact per-rebuild memoization if ANY
    --          third-party mod (e.g. Craft From Chests) hooks Inventory/Container:Has().
    --       2) Defer hidden CraftingMenuWidget grid/filter/detail rebuilds while closed,
    --          keeping valid_recipes + PinBar + lightbulb/ding sounds 100% vanilla-live,
    --          and flushing cleanly on CraftingMenuHUD:Open().
    ----------------------------------------------------------------------------
    local function IsVanillaSrc(fn, pat)
        if type(fn) ~= "function" or _dbg_getinfo_df == nil then
            return false
        end
        local info = _dbg_getinfo_df(fn, "S")
        local src = info ~= nil and info.source or nil
        return type(src) == "string" and src:find(pat) ~= nil
    end

    local function TallyHostItem(item, counts)
        if item ~= nil and item.prefab ~= nil and (item.HasTag == nil or not item:HasTag("nocrafting")) then
            local st = item.components ~= nil and item.components.stackable or nil
            local n = st ~= nil and st:StackSize() or 1
            counts[item.prefab] = (counts[item.prefab] or 0) + n
        end
    end

    local function TallyClientItem(item, counts)
        if item ~= nil and item.prefab ~= nil and (item.HasTag == nil or not item:HasTag("nocrafting")) then
            local st = item.replica ~= nil and item.replica.stackable or nil
            local n = st ~= nil and st:StackSize() or 1
            counts[item.prefab] = (counts[item.prefab] or 0) + n
        end
    end

    local function TallyClientContainer(container, counts)
        if not IsVanillaSrc(container.Has, "components[/\\]container_replica%.lua") then
            return false
        end
        local ccl = container.classified
        if ccl == nil or not IsVanillaSrc(ccl.Has, "prefabs[/\\]container_classified%.lua") then
            return false
        end
        local citems = ccl._items
        if type(citems) ~= "table" then
            return false
        end
        local cprev = ccl._itemspreview
        if cprev ~= nil then
            for i = 1, #citems do
                TallyClientItem(cprev[i], counts)
            end
        else
            for i = 1, #citems do
                local nv = citems[i]
                if nv ~= nil and nv.value ~= nil then
                    TallyClientItem(nv:value(), counts)
                end
            end
        end
        return true
    end

    local function TryBuildCraftingSnapshot(owner, inv_rep)
        if owner == nil or inv_rep == nil then
            return nil
        end
        if not IsVanillaSrc(inv_rep.Has, "components[/\\]inventory_replica%.lua") then
            return nil
        end
        local counts = {}
        local inv_cmp = owner.components ~= nil and owner.components.inventory or nil
        if inv_cmp ~= nil then
            -- Local Host / Solo path
            if not IsVanillaSrc(inv_cmp.Has, "components[/\\]inventory%.lua") then
                return nil
            end
            if type(inv_cmp.itemslots) ~= "table" then
                return nil
            end
            for _, v in pairs(inv_cmp.itemslots) do
                TallyHostItem(v, counts)
            end
            TallyHostItem(inv_cmp.activeitem, counts)

            local overflow = inv_cmp:GetOverflowContainer()
            if overflow ~= nil then
                if not IsVanillaSrc(overflow.Has, "components[/\\]container%.lua")
                    and not IsVanillaSrc(overflow.Has, "components[/\\]inventory%.lua") then
                    return nil
                end
                local oslots = overflow.slots or overflow.itemslots
                if type(oslots) ~= "table" then
                    return nil
                end
                for _, v in pairs(oslots) do
                    TallyHostItem(v, counts)
                end
            end

            if type(inv_cmp.opencontainers) == "table" then
                for container_inst in pairs(inv_cmp.opencontainers) do
                    local c = container_inst.components ~= nil
                        and (container_inst.components.container or container_inst.components.inventory) or nil
                    if c ~= nil and c ~= overflow and not c.excludefromcrafting and not c.readonlycontainer then
                        if not IsVanillaSrc(c.Has, "components[/\\]container%.lua")
                            and not IsVanillaSrc(c.Has, "components[/\\]inventory%.lua") then
                            return nil
                        end
                        local cslots = c.slots or c.itemslots
                        if type(cslots) ~= "table" then
                            return nil
                        end
                        for _, v in pairs(cslots) do
                            TallyHostItem(v, counts)
                        end
                    end
                end
            end
            return counts
        end

        -- Multiplayer Remote Client path (inventory_classified)
        local cl = inv_rep.classified
        if cl == nil or not IsVanillaSrc(cl.Has, "prefabs[/\\]inventory_classified%.lua") then
            return nil
        end
        local items = cl._items
        if type(items) ~= "table" then
            return nil
        end
        local active = cl._activeitem
        TallyClientItem(active, counts)
        local prev = cl._itemspreview
        if prev ~= nil then
            for i = 1, #items do
                TallyClientItem(prev[i], counts)
            end
        else
            for i = 1, #items do
                local nv = items[i]
                if nv ~= nil and nv.value ~= nil then
                    local it = nv:value()
                    if it ~= active then
                        TallyClientItem(it, counts)
                    end
                end
            end
        end

        local overflow = inv_rep.GetOverflowContainer ~= nil and inv_rep:GetOverflowContainer() or nil
        if overflow ~= nil then
            if not TallyClientContainer(overflow, counts) then
                return nil
            end
        end

        local open_c = inv_rep.GetOpenContainers ~= nil and inv_rep:GetOpenContainers() or nil
        if type(open_c) == "table" then
            for container_inst in pairs(open_c) do
                local c = container_inst.replica ~= nil
                    and (container_inst.replica.container or container_inst.replica.inventory) or nil
                if c ~= nil and c ~= overflow and not c.excludefromcrafting
                    and (c.IsReadOnlyContainer == nil or not c:IsReadOnlyContainer()) then
                    if not TallyClientContainer(c, counts) then
                        return nil
                    end
                end
            end
        end
        return counts
    end

    AddClassPostConstruct("widgets/redux/craftingmenu_hud", function(hud)
        if type(hud) ~= "table" or type(hud.RebuildRecipes) ~= "function" then
            return
        end
        local orig_RebuildRecipes = hud.RebuildRecipes
        hud.RebuildRecipes = function(self, ...)
            local owner = self.owner
            local inv_rep = owner ~= nil and owner.replica ~= nil and owner.replica.inventory or nil
            if inv_rep == nil or type(inv_rep.Has) ~= "function" then
                return orig_RebuildRecipes(self, ...)
            end

            latency_stats.craft_rebuilds = latency_stats.craft_rebuilds + 1
            local inv_cmp = owner.components ~= nil and owner.components.inventory or nil
            local orig_rep_has = inv_rep.Has
            local orig_cmp_has = inv_cmp ~= nil and inv_cmp.Has or nil

            local counts = TryBuildCraftingSnapshot(owner, inv_rep)
            if counts ~= nil then
                local fast_has = function(self_inv, prefab, amount, checkallcontainers)
                    if checkallcontainers then
                        latency_stats.craft_has_saved = latency_stats.craft_has_saved + 1
                        local c = counts[prefab] or 0
                        return c >= amount, c
                    end
                    if self_inv == inv_cmp and orig_cmp_has ~= nil then
                        return orig_cmp_has(self_inv, prefab, amount, checkallcontainers)
                    end
                    return orig_rep_has(self_inv, prefab, amount, checkallcontainers)
                end
                inv_rep.Has = fast_has
                if inv_cmp ~= nil and orig_cmp_has ~= nil then
                    inv_cmp.Has = fast_has
                end
            else
                -- Third-party mod wrapped Has() (e.g. Craft From Nearby Chests):
                -- use mathematically exact per-rebuild memoization calling the mod's own Has()!
                local memo = {}
                local function make_memo(orig_fn)
                    return function(self_inv, prefab, amount, checkallcontainers)
                        if checkallcontainers and prefab ~= nil and type(amount) == "number" then
                            local e = memo[prefab]
                            if e ~= nil then
                                if amount <= e.min then
                                    latency_stats.craft_has_saved = latency_stats.craft_has_saved + 1
                                    return true, e.min
                                elseif e.exact then
                                    latency_stats.craft_has_saved = latency_stats.craft_has_saved + 1
                                    return false, e.min
                                end
                            end
                            local enough, found = orig_fn(self_inv, prefab, amount, checkallcontainers)
                            local cnt = type(found) == "number" and found or (enough and amount or 0)
                            if e == nil then
                                memo[prefab] = { min = cnt, exact = not enough }
                            else
                                if cnt > e.min then e.min = cnt end
                                if not enough then e.exact = true end
                            end
                            return enough, found
                        end
                        return orig_fn(self_inv, prefab, amount, checkallcontainers)
                    end
                end
                inv_rep.Has = make_memo(orig_rep_has)
                if inv_cmp ~= nil and orig_cmp_has ~= nil then
                    inv_cmp.Has = make_memo(orig_cmp_has)
                end
            end

            local ok, r1, r2, r3 = pcall(orig_RebuildRecipes, self, ...)
            inv_rep.Has = orig_rep_has
            if inv_cmp ~= nil and orig_cmp_has ~= nil then
                inv_cmp.Has = orig_cmp_has
            end
            if not ok then
                GLOBAL.error(r1, 0)
            end
            return r1, r2, r3
        end

        if type(hud.Open) == "function" then
            local orig_Open = hud.Open
            hud.Open = function(self, search, ...)
                local cm = self.craftingmenu
                if cm ~= nil and cm._hs_deferred_refresh then
                    local need_rebuild = cm._hs_deferred_rebuild
                    cm._hs_deferred_refresh = false
                    cm._hs_deferred_rebuild = false
                    if cm.sort_class == nil or cm.sort_class.Refresh == nil or not cm.sort_class:Refresh() then
                        if need_rebuild and type(cm.ApplyFilters) == "function" then
                            cm:ApplyFilters()
                        elseif cm.recipe_grid ~= nil and type(cm.recipe_grid.RefreshView) == "function" then
                            cm.recipe_grid:RefreshView()
                        end
                    end
                    if cm.details_root ~= nil and type(cm.details_root.Refresh) == "function" then
                        cm.details_root:Refresh()
                    end
                end
                return orig_Open(self, search, ...)
            end
        end
    end)

    AddClassPostConstruct("widgets/redux/craftingmenu_widget", function(cm)
        if type(cm) ~= "table" or type(cm.Refresh) ~= "function" then
            return
        end
        local orig_Refresh = cm.Refresh
        cm.Refresh = function(self, tech_tree_changed, ...)
            if self.crafting_hud ~= nil
                and type(self.crafting_hud.IsCraftingOpen) == "function"
                and not self.crafting_hud:IsCraftingOpen()
                and type(self.UpdateFilterButtons) == "function" then
                -- Keep open_menu_button prototype lightbulb & ding sound 100% live,
                -- but defer hidden ApplyFilters / recipe_grid / details_root until Open()!
                local rebuild_details_list = self:UpdateFilterButtons()
                self._hs_deferred_refresh = true
                if rebuild_details_list then
                    self._hs_deferred_rebuild = true
                end
                latency_stats.craft_ui_deferred = latency_stats.craft_ui_deferred + 1
                return
            end
            self._hs_deferred_refresh = false
            self._hs_deferred_rebuild = false
            return orig_Refresh(self, tech_tree_changed, ...)
        end
    end)
end

--------------------------------------------------------------------------------
-- 8. Native ESC -> Settings -> Graphics Integration (v2.3.0)
--    Directly revives & powers Klei's official "Refresh Rate" (刷新率) spinner
--    in BOTH Windowed and Fullscreen modes (ZERO duplicate FPS UI!), safely maps
--    Fullscreen hardware display Hz, fixes Klei's IsGraphicsDirty() refreshrate bug,
--    and adds only the 2 truly new visual toggles (Skeletal 240fps + FPS HUD)
--    into the empty right-column rows 6 & 7 below Dynamic Tree Shadows.
--------------------------------------------------------------------------------
AddClassPostConstruct("screens/redux/optionsscreen", function(self)
    if not self or not self.refreshRateSpinner or not self.options or not self.working then
        return
    end

    local opt_str = GLOBAL.STRINGS and GLOBAL.STRINGS.UI and GLOBAL.STRINGS.UI.OPTIONS
    local enabled_str = (opt_str and opt_str.ENABLED) or "Enabled"
    local disabled_str = (opt_str and opt_str.DISABLED) or "Disabled"
    local is_zh = ResolveUiIsZh()
    local unlimited_label = is_zh and "无限制" or "Unlimited"
    local has_dll = HasNativePlugin()

    -- Helper: read real hardware monitor refresh rates for current display/resolution
    local function GetHardwareRefreshRates(scr)
        local rates = {}
        local fe = GLOBAL.TheFrontEnd
        local gOpts = fe and fe:GetGraphicsOptions()
        if not gOpts then return rates end
        local disp = scr.working.display or 0
        local mode_idx = scr.working.mode_idx or 0
        local ok, w, h = pcall(gOpts.GetDisplayMode, gOpts, disp, mode_idx)
        if not ok or not w or not h then return rates end
        local ok_n, num = pcall(gOpts.GetNumRefreshRates, gOpts, disp, w, h)
        if not ok_n or not num then return rates end
        local seen = {}
        for i = 0, num - 1 do
            local ok_r, hz = pcall(gOpts.GetRefreshRate, gOpts, disp, w, h, i)
            if ok_r and hz and hz > 0 and not seen[hz] then
                seen[hz] = true
                rates[#rates + 1] = hz
            end
        end
        table.sort(rates)
        return rates
    end

    -- Helper: map logical target FPS (including 0=Unlimited or >panel Hz) to a guaranteed valid
    -- Windows hardware display Hz so ChangeGraphicsMode NEVER falls back to 640x480 mode 0!
    local function PickSafeHardwareHz(scr, target_fps)
        local hw = GetHardwareRefreshRates(scr)
        if #hw == 0 then
            return scr._hc_hw_refreshrate or 60
        end
        if not target_fps or target_fps <= 0 then
            return hw[#hw] -- Unlimited -> highest native monitor refresh rate
        end
        for _, hz in ipairs(hw) do
            if hz == target_fps then return hz end
        end
        for _, hz in ipairs(hw) do
            if hz >= target_fps then return hz end
        end
        return hw[#hw]
    end

    -- Helper: build unified refresh/FPS option list (real monitor Hz + standard presets + Unlimited)
    local function BuildMergedRefreshOptions(scr, active_fps)
        local set = {}
        local nums = {}
        local function add_num(v)
            if type(v) == "number" and v > 0 and not set[v] then
                set[v] = true
                nums[#nums + 1] = v
            end
        end
        for _, hz in ipairs(GetHardwareRefreshRates(scr)) do add_num(hz) end
        for _, p in ipairs({ 60, 120, 144, 165, 240, 360 }) do add_num(p) end
        if type(active_fps) == "number" and active_fps > 0 then add_num(active_fps) end
        table.sort(nums)

        local opts = {}
        for _, v in ipairs(nums) do
            opts[#opts + 1] = { text = string.format("%d", v), data = v }
        end
        opts[#opts + 1] = { text = unlimited_label, data = 0 }
        return opts
    end

    self._hc_hw_refreshrate = self.options.refreshrate or 60
    self.options.refreshrate = current_target_fps
    self.working.refreshrate = current_target_fps
    self.options.hc_skeletal = current_skeletal_tween
    self.working.hc_skeletal = current_skeletal_tween
    local init_hud_state = current_show_fps_hud and ((current_hud_mode == "detail") and 2 or 1) or 0
    self.options.hc_show_hud = init_hud_state
    self.working.hc_show_hud = init_hud_state
    self.options.hc_visual = current_visual_preset
    self.working.hc_visual = current_visual_preset

    -- Update official Refresh Rate tooltip to reflect live HyperSmooth v2.8.0 RTSS control
    if self.refreshRateSpinner.parent then
        if is_zh then
            self.refreshRateSpinner.parent.tooltip_text = has_dll
                and "由 HyperSmooth v2.8.0 零争抢 RTSS + D3D9Ex 1帧低延迟引擎实时接管：支持全屏/窗口自由切帧（60~360/无限制）"
                or "需安装配套 bin64/dinput8.dll 原生插件方可突破官方 60 帧上限与启用 RTSS 稳帧（当前仅控制全屏显示器模式）"
        else
            self.refreshRateSpinner.parent.tooltip_text = has_dll
                and "Powered live by HyperSmooth v2.8.0 RTSS Pacer + D3D9Ex 1-Frame Reflex Queue: 60-360 FPS or Unlimited."
                or "Install bin64/dinput8.dll companion plugin to unlock frame rate above 60 FPS & RTSS pacing."
        end
    end

    -- 1. Keep official refreshRateSpinner ALWAYS enabled (even in Windowed / Borderless mode!)
    local orig_UpdateResolutionsSpinner = self.UpdateResolutionsSpinner
    self.UpdateResolutionsSpinner = function(scr, ...)
        local r = orig_UpdateResolutionsSpinner(scr, ...)
        if scr.refreshRateSpinner then
            scr.refreshRateSpinner:Enable()
        end
        return r
    end

    -- 2. Populate official refreshRateSpinner with merged monitor Hz + high-FPS presets + Unlimited
    self.UpdateRefreshRatesSpinner = function(scr)
        local target = scr.working.refreshrate
        if target == nil then target = current_target_fps end
        local opts = BuildMergedRefreshOptions(scr, target)
        scr.refreshRateSpinner:SetOptions(opts)
        local selected = 1
        for idx, item in ipairs(opts) do
            if item.data == target then
                selected = idx
                break
            end
        end
        scr.refreshRateSpinner:SetSelectedIndex(selected)
        scr.working.refreshrate = opts[selected].data
    end

    -- 3. Fix Klei's IsGraphicsDirty() refreshrate bug while avoiding monitor flicker in Windowed mode
    local orig_IsGraphicsDirty = self.IsGraphicsDirty
    self.IsGraphicsDirty = function(scr, ...)
        if orig_IsGraphicsDirty(scr, ...) then
            return true
        end
        if scr.working.fullscreen then
            return PickSafeHardwareHz(scr, scr.working.refreshrate) ~= PickSafeHardwareHz(scr, scr.options.refreshrate)
        end
        return false
    end

    -- 4. Guard ChangeGraphicsMode() so 0 (Unlimited) or >panel Hz safely selects highest valid monitor Hz
    local orig_ChangeGraphicsMode = self.ChangeGraphicsMode
    self.ChangeGraphicsMode = function(scr, ...)
        local saved_fps = scr.working.refreshrate
        scr.working.refreshrate = PickSafeHardwareHz(scr, saved_fps)
        local r = orig_ChangeGraphicsMode(scr, ...)
        scr.working.refreshrate = saved_fps
        return r
    end

    -- 5. Add only the 2 truly new visual toggles into right-column empty rows 6 & 7 (below Dynamic Tree Shadows)
    local function CreateNativeGraphicsSpinner(labeltext, spinnerdata, tooltip_text)
        local w = TEMPLATES.LabelSpinner(labeltext, spinnerdata, 200, 220, 36, 5, nil, nil, -50, nil, nil, tooltip_text)
        w.bg = w:AddChild(TEMPLATES.ListItemBackground(200 + 220 + 5 + 15, 36 + 5))
        w.bg:SetPosition(-40, 0)
        w.bg:MoveToBack()
        return w.spinner
    end

    local function FindGraphicsTooltipWidgets(scr)
        local root = scr.grid_graphics and scr.grid_graphics.parent
        if not root or not root.children then return nil, nil end
        local tip_txt, tip_div = nil, nil
        for child in pairs(root.children) do
            if child ~= scr.grid_graphics then
                if child.SetString and child.EnableWordWrap then
                    tip_txt = child
                elseif child.SetTexture then
                    tip_div = child
                end
            end
        end
        return tip_txt, tip_div
    end

    local function AttachTooltip(widget, tooltip, divider)
        if not widget or not tooltip then return end
        local function ongain()
            if widget.tooltip_text ~= nil then
                tooltip:SetString(widget.tooltip_text)
                if divider then divider:Show() end
            end
        end
        local function onlose()
            if widget.parent and not widget.parent.focus then
                tooltip:SetString("")
                if divider then divider:Hide() end
            end
        end
        if widget.bg then
            widget.bg.ongainfocus = ongain
            widget.bg.onlosefocus = onlose
        end
        if widget.spinner then
            widget.spinner.ongainfocusfn = ongain
            widget.spinner.onlosefocusfn = onlose
        end
    end

    local function BindPendingHighlight(spinner)
        if not spinner or not spinner.GetSelectedIndex then return end
        spinner.default_index = spinner:GetSelectedIndex()
        spinner:EnablePendingModificationBackground()
        local orig_cb = spinner.OnChanged
        spinner.OnChanged = function(sp, data)
            if orig_cb then orig_cb(sp, data) end
            spinner:SetHasModification(spinner:GetSelectedIndex() ~= spinner.default_index)
        end
    end

    -- Real Klei Grid uses `.rows` (not `.num_rows`, which only FillGrid sets!).
    -- Right column has 5 stock items + 3 HyperSmooth spinners = exact 8 rows (zero overflow!).
    local grid = self.grid_graphics
    local grid_rows = grid and (grid.rows or grid.num_rows) or 0
    if grid and self.right_spinners_graphics and grid_rows > 0
        and (#self.right_spinners_graphics + 2) <= grid_rows then
        local toggle_opts = {
            { text = disabled_str, data = false },
            { text = enabled_str, data = true },
        }
        local tip_txt, tip_div = FindGraphicsTooltipWidgets(self)

        local skel_label = is_zh and "240帧骨骼/粒子补帧" or "240fps Anim & VFX"
        local skel_tip = is_zh
            and (has_dll and "开启全实体骨骼、位移以及雨雪/火星/法术粒子的真 240 帧连续插值（dinput8.dll 已直连，实时生效）"
                         or "需安装 bin64/dinput8.dll 原生插件方可生效")
            or (has_dll and "True 240fps interpolation across Skeletal Anims, World Positions & VFX Particles (dinput8.dll active)."
                        or "Requires bin64/dinput8.dll native companion plugin.")
        self.hcSkeletalSpinner = CreateNativeGraphicsSpinner(skel_label, toggle_opts, skel_tip)
        self.hcSkeletalSpinner.OnChanged = function(_, data)
            self.working.hc_skeletal = data
            self:UpdateMenu()
        end

        local hud_opts = is_zh and {
            { text = disabled_str, data = 0 },
            { text = "简洁模式", data = 1 },
            { text = "详细模式", data = 2 },
        } or {
            { text = disabled_str, data = 0 },
            { text = "Compact", data = 1 },
            { text = "Detailed", data = 2 },
        }
        local function HudValToIdx(v)
            if v == false or v == 0 or v == nil then return 1 end
            if v == 2 then return 3 end
            return 2
        end
        local hud_label = is_zh and "右上角 HUD 模式" or "FPS Overlay HUD"
        local hud_tip = is_zh
            and "在画面右上角显示简洁（帧率+延迟/丢包）或详细（含 1% Low/帧时/GL）实时状态"
            or "Show compact (FPS + Ping/Loss) or detailed (1% Low / Frame ms / GL) top-right telemetry."
        self.hcHudSpinner = CreateNativeGraphicsSpinner(hud_label, hud_opts, hud_tip)
        self.hcHudSpinner.OnChanged = function(_, data)
            self.working.hc_show_hud = data
            self:UpdateMenu()
        end

        local r1 = #self.right_spinners_graphics + 1
        table.insert(self.right_spinners_graphics, self.hcSkeletalSpinner)
        grid:AddItem(self.hcSkeletalSpinner.parent, 2, r1)
        AttachTooltip(self.hcSkeletalSpinner.parent, tip_txt, tip_div)

        local r2 = #self.right_spinners_graphics + 1
        table.insert(self.right_spinners_graphics, self.hcHudSpinner)
        grid:AddItem(self.hcHudSpinner.parent, 2, r2)
        AttachTooltip(self.hcHudSpinner.parent, tip_txt, tip_div)

        self.hcSkeletalSpinner:SetSelectedIndex(self.working.hc_skeletal and 2 or 1)
        self.hcHudSpinner:SetSelectedIndex(HudValToIdx(self.working.hc_show_hud))
        BindPendingHighlight(self.hcSkeletalSpinner)
        BindPendingHighlight(self.hcHudSpinner)

        -- Right-column row 8: Player-First Visual Fidelity Preset (Default: Vanilla Pure)
        if (#self.right_spinners_graphics + 1) <= grid_rows then
            local vis_opts = is_zh and {
                { text = "原版纯净", data = 0 },
                { text = "护眼高清", data = 1 },
                { text = "柔和绘本", data = 2 },
            } or {
                { text = "Vanilla", data = 0 },
                { text = "Eye-Care HD", data = 1 },
                { text = "Storybook", data = 2 },
            }
            if EncodeCustomVisMask(custom_vis_flags) > 0 or self.working.hc_visual == 3 then
                vis_opts[#vis_opts + 1] = is_zh and { text = "自定义", data = 3 } or { text = "Custom", data = 3 }
            end
            local vis_label = is_zh and "画质增强" or "Visual Fidelity"
            local vis_tip = is_zh
                and (has_dll and "玩家优先原则：默认原版纯净，不强迫后处理；关闭后完整恢复原版。护眼高清（16×AF + 1.0×光照 + 轻度保真锐化 + 温和接地阴影，关泛光全效）；亦可在模组配置页或控制台 c_hsopt 逐项独立开关"
                             or "默认原版纯净，关闭即完整恢复原版；启用后提供轻度保真微锐化与暗夜防色带（搭配 bin64/dinput8.dll 解锁 16×AF、全分辨率光照与温和接地阴影）")
                or (has_dll and "Player-First: Vanilla Pure by default; disabling restores 100% stock visuals. Eye-Care HD works with Bloom OFF; individual toggles available in Mod Config or c_hsopt()."
                            or "Vanilla Pure by default. Eye-Care CAS + anti-banding (install bin64/dinput8.dll for 16x AF, 1.0x LightMap & Contact AO).")
            self.hcVisualSpinner = CreateNativeGraphicsSpinner(vis_label, vis_opts, vis_tip)
            self.hcVisualSpinner.OnChanged = function(_, data)
                self.working.hc_visual = data
                self:UpdateMenu()
            end
            local r3 = #self.right_spinners_graphics + 1
            table.insert(self.right_spinners_graphics, self.hcVisualSpinner)
            grid:AddItem(self.hcVisualSpinner.parent, 2, r3)
            AttachTooltip(self.hcVisualSpinner.parent, tip_txt, tip_div)
            local vidx = (self.working.hc_visual or 0) + 1
            if vidx < 1 or vidx > #vis_opts then vidx = 1 end
            self.hcVisualSpinner:SetSelectedIndex(vidx)
            BindPendingHighlight(self.hcVisualSpinner)
        end
    end

    -- 6. Sync on InitializeSpinners (handles RevertChanges cleanly)
    local orig_InitializeSpinners = self.InitializeSpinners
    self.InitializeSpinners = function(scr, first, ...)
        local r = orig_InitializeSpinners(scr, first, ...)
        if scr.refreshRateSpinner then
            scr.refreshRateSpinner:SetHasModification(false)
        end
        if scr.hcSkeletalSpinner then
            scr.hcSkeletalSpinner:SetSelectedIndex(scr.working.hc_skeletal and 2 or 1)
            scr.hcSkeletalSpinner:SetHasModification(false)
        end
        if scr.hcHudSpinner then
            scr.hcHudSpinner:SetSelectedIndex(HudValToIdx(scr.working.hc_show_hud))
            scr.hcHudSpinner:SetHasModification(false)
        end
        if scr.hcVisualSpinner then
            local opts = scr.hcVisualSpinner.options
            local max_i = (type(opts) == "table" and #opts) or 3
            local vidx = (scr.working.hc_visual or 0) + 1
            if vidx < 1 or vidx > max_i then vidx = 1 end
            scr.hcVisualSpinner:SetSelectedIndex(vidx)
            scr.hcVisualSpinner:SetHasModification(false)
        end
        return r
    end

    -- 7. Sync live engine & persist on Apply / Save
    local orig_Apply = self.Apply
    self.Apply = function(scr, ...)
        local r = orig_Apply(scr, ...)
        if scr.working.refreshrate ~= nil then
            current_target_fps = scr.working.refreshrate
        end
        if scr.working.hc_skeletal ~= nil then
            current_skeletal_tween = scr.working.hc_skeletal and true or false
        end
        if scr.working.hc_show_hud ~= nil then
            local hv = scr.working.hc_show_hud
            if type(hv) == "number" then
                current_show_fps_hud = (hv > 0)
                if hv == 2 then
                    current_hud_mode = "detail"
                elseif hv == 1 then
                    current_hud_mode = "simple"
                end
            else
                current_show_fps_hud = hv and true or false
            end
        end
        if scr.working.hc_visual ~= nil then
            if scr.working.hc_visual ~= current_visual_preset then
                visual_bench_timer = 15.0
            end
            current_visual_preset = scr.working.hc_visual
        end
        SyncToNativePlugin(true)
        return r
    end

    local orig_Save = self.Save
    self.Save = function(scr, cb, ...)
        return orig_Save(scr, function(...)
            if scr.refreshRateSpinner then
                scr.refreshRateSpinner.default_index = scr.refreshRateSpinner:GetSelectedIndex()
                scr.refreshRateSpinner:SetHasModification(false)
            end
            if scr.hcSkeletalSpinner then
                scr.hcSkeletalSpinner.default_index = scr.hcSkeletalSpinner:GetSelectedIndex()
                scr.hcSkeletalSpinner:SetHasModification(false)
            end
            if scr.hcHudSpinner then
                scr.hcHudSpinner.default_index = scr.hcHudSpinner:GetSelectedIndex()
                scr.hcHudSpinner:SetHasModification(false)
            end
            if scr.hcVisualSpinner then
                scr.hcVisualSpinner.default_index = scr.hcVisualSpinner:GetSelectedIndex()
                scr.hcVisualSpinner:SetHasModification(false)
            end
            if cb then cb(...) end
        end, ...)
    end

    -- Initialize official Refresh Rate spinner state right now (no false yellow highlight on open!)
    self:UpdateResolutionsSpinner()
    self:UpdateRefreshRatesSpinner()
    self.refreshRateSpinner.default_index = self.refreshRateSpinner:GetSelectedIndex()
    self.refreshRateSpinner:SetHasModification(false)
    self:UpdateMenu()
end)

--------------------------------------------------------------------------------
-- 9. Route B: Auto-Negotiated Opt-In 60Hz SimTick Handshake (v2.4.0)
--    100% passive on ALL vanilla/public servers (dinput8.dll unconditionally
--    resets Client SimTick to 30Hz on every VM/menu/server transition!).
--    Activates 60Hz ONLY when connected to a DST HyperCore Server v1.2.0+
--    with opt_sim_60hz enabled AND user has auto_60hz_sim = true.
--------------------------------------------------------------------------------
local toast_timer = 0
local toast_msg = nil
local active_sim_tick_hz = 30

local function TryActivate60HzSim(source_label)
    if not AUTO_60HZ_SIM or active_sim_tick_hz == 60 then
        return
    end
    local fnSetSim = rawget(GLOBAL, "HyperSmooth_SetSimTickRate")
    if type(fnSetSim) ~= "function" then
        return
    end
    local ok, applied = pcall(fnSetSim, 60)
    if ok and applied then
        active_sim_tick_hz = 60
        -- Override GetTime / GetStaticTime dynamically so Lua never reads stale 1/30 upvalues
        rawset(GLOBAL, "GetTime", function()
            return TheSim:GetTick() * TheSim:GetTickTime()
        end)
        rawset(GLOBAL, "GetStaticTime", function()
            return TheSim:GetStaticTick() * TheSim:GetTickTime()
        end)
        toast_msg = ResolveUiIsZh() and "HyperCore 60Hz 逻辑刻已开启" or "HyperCore 60Hz SimTick ACTIVE"
        toast_timer = 4.0
        hud_dirty_now = true
        if GLOBAL.print then
            GLOBAL.print(string.format("[HyperSmooth v2.8.0] Auto-negotiated 60Hz SimTick via %s (GetTickTime=%.6f)",
                tostring(source_label), TheSim:GetTickTime()))
        end
    end
end

local function TryRestore30HzSim(source_label)
    local fnSetSim = rawget(GLOBAL, "HyperSmooth_SetSimTickRate")
    if type(fnSetSim) ~= "function" then
        return false
    end
    local ok, applied = pcall(fnSetSim, 30)
    if ok and applied then
        active_sim_tick_hz = 30
        toast_msg = ResolveUiIsZh() and "已恢复 30Hz 原版逻辑刻" or "HyperCore 30Hz Vanilla SimTick RESTORED"
        toast_timer = 3.0
        hud_dirty_now = true
        if GLOBAL.print then
            GLOBAL.print(string.format("[HyperSmooth v2.8.0] Restored 30Hz SimTick via %s (GetTickTime=%.6f)",
                tostring(source_label), TheSim:GetTickTime()))
        end
        return true
    end
    if GLOBAL.print then
        GLOBAL.print(string.format("[HyperSmooth v2.4.0] WARNING: Failed restoring 30Hz SimTick via %s (ok=%s applied=%s)",
            tostring(source_label), tostring(ok), tostring(applied)))
    end
    return false
end

-- Channel 1: ClientModRPC("HyperCoreProto", "SimTickRate") without duplicating slot id if co-loaded
local function OnSimTickRateRpc(hz)
    if hz == 60 then
        TryActivate60HzSim("ClientModRPC")
    elseif hz == 30 then
        TryRestore30HzSim("ClientModRPC")
    end
end
if type(AddClientModRPCHandler) == "function" then
    -- NOTE: Klei's GetClientModRPC(ns, name) does NOT nil-check CLIENT_MOD_RPC[ns]!
    -- Always inspect GLOBAL.CLIENT_MOD_RPC directly before registering.
    local rpc_tbl = rawget(GLOBAL, "CLIENT_MOD_RPC")
    local rpc_ns = type(rpc_tbl) == "table" and rpc_tbl["HyperCoreProto"] or nil
    local existing_rpc = type(rpc_ns) == "table" and rpc_ns["SimTickRate"] or nil
    if existing_rpc == nil then
        AddClientModRPCHandler("HyperCoreProto", "SimTickRate", OnSimTickRateRpc)
    else
        local handlers_tbl = rawget(GLOBAL, "CLIENT_MOD_RPC_HANDLERS")
        local handlers_ns = type(handlers_tbl) == "table" and handlers_tbl["HyperCoreProto"] or nil
        if type(handlers_ns) == "table" and existing_rpc.id ~= nil then
            local prev_fn = handlers_ns[existing_rpc.id]
            handlers_ns[existing_rpc.id] = function(hz, ...)
                if type(prev_fn) == "function" then pcall(prev_fn, hz, ...) end
                OnSimTickRateRpc(hz)
            end
        end
    end
end

-- Channel 2: Replicated "hypercore_sim60" tag on world_network prefabs (zero RPC dependency!)
for _, net_prefab in ipairs({ "forest_network", "cave_network", "lavaarena_network", "quagmire_network" }) do
    AddPrefabPostInit(net_prefab, function(inst)
        if not AUTO_60HZ_SIM then return end
        if inst and inst.HasTag and inst:HasTag("hypercore_sim60") then
            TryActivate60HzSim("WorldNetTag")
        elseif inst and inst.DoTaskInTime then
            inst:DoTaskInTime(0.25, function(net_inst)
                if net_inst and net_inst:IsValid() and net_inst:HasTag("hypercore_sim60") then
                    TryActivate60HzSim("WorldNetTag")
                end
            end)
        end
    end)
end

AddSimPostInit(function()
    if not AUTO_60HZ_SIM then return end
    local w = GLOBAL.TheWorld
    if w ~= nil then
        if (w.HasTag and w:HasTag("hypercore_sim60")) or (w.net and w.net.HasTag and w.net:HasTag("hypercore_sim60")) then
            TryActivate60HzSim("SimPostInit")
        end
    end
end)

--------------------------------------------------------------------------------
-- 9b. 240Hz Immediate WASD RPC Dispatch (Lag Compensation OFF Zero-Wait Input)
--     Vanilla PlayerController:DoDirectWalking only polls WASD inside OnUpdate
--     (once per SimTick: 0-33.3ms stall on 30Hz servers, 0-16.7ms on 60Hz).
--     When Lag Comp is OFF (self.locomotor == nil on remote client), dispatching
--     RemoteDirectWalking / RemoteStopWalking directly on the 240Hz OnControl
--     edge cuts 8-33ms of input wait with ZERO duplicate RPCs (self.remote_vector
--     deduplicates) and 100% vanilla-server compatibility.
--------------------------------------------------------------------------------
if type(AddComponentPostInit) == "function" then
    AddComponentPostInit("playercontroller", function(pc)
        if type(pc) ~= "table" or type(pc.OnControl) ~= "function" then
            return
        end
        local MOVE_UP    = rawget(GLOBAL, "CONTROL_MOVE_UP")
        local MOVE_DOWN  = rawget(GLOBAL, "CONTROL_MOVE_DOWN")
        local MOVE_LEFT  = rawget(GLOBAL, "CONTROL_MOVE_LEFT")
        local MOVE_RIGHT = rawget(GLOBAL, "CONTROL_MOVE_RIGHT")
        local CTRL_ATK   = rawget(GLOBAL, "CONTROL_CONTROLLER_ATTACK")
        if not (MOVE_UP and MOVE_DOWN and MOVE_LEFT and MOVE_RIGHT) then
            return
        end

        local orig_OnControl = pc.OnControl
        pc.OnControl = function(self, control, down, analog, ...)
            local ret = orig_OnControl(self, control, down, analog, ...)
            if ret == true or not current_low_latency then
                return ret
            end
            if control ~= MOVE_UP and control ~= MOVE_DOWN and control ~= MOVE_LEFT and control ~= MOVE_RIGHT then
                return ret
            end
            -- Strictly remote client with Lag Compensation OFF (locomotor == nil)
            if self.ismastersim or self.locomotor ~= nil then
                return ret
            end
            if (GLOBAL.IsPaused and GLOBAL.IsPaused()) or self.handler == nil then
                return ret
            end
            local inst = self.inst
            if inst == nil or inst ~= GLOBAL.ThePlayer or (inst.IsValid and not inst:IsValid()) then
                return ret
            end
            -- Preserve full analog stick precision for gamepads (handled natively in OnUpdate)
            if TheInput.ControllerAttached and TheInput:ControllerAttached() then
                return ret
            end
            local isenabled, ishudblocking = false, false
            if self.IsEnabled then
                isenabled, ishudblocking = self:IsEnabled()
            end
            if not isenabled and not ishudblocking then
                return ret
            end
            if self.dragwalking or self.draggingonground then
                return ret
            end
            if self.IsLocalOrRemoteHopping and self:IsLocalOrRemoteHopping() then
                return ret
            end
            local bcu = inst.components and inst.components.boatcannonuser or nil
            if (bcu ~= nil and bcu.GetCannon and bcu:GetCannon() ~= nil)
                or (inst.HasTag and (inst:HasTag("steeringboat") or inst:HasTag("rotatingboat"))) then
                return ret
            end

            local trigVal = (type(analog) == "number" and analog) or (down and 1 or 0)
            local vR = (control == MOVE_RIGHT) and trigVal or (TheInput:GetAnalogControlValue(MOVE_RIGHT) or 0)
            local vL = (control == MOVE_LEFT)  and trigVal or (TheInput:GetAnalogControlValue(MOVE_LEFT) or 0)
            local vU = (control == MOVE_UP)    and trigVal or (TheInput:GetAnalogControlValue(MOVE_UP) or 0)
            local vD = (control == MOVE_DOWN)  and trigVal or (TheInput:GetAnalogControlValue(MOVE_DOWN) or 0)
            local xdir = vR - vL
            local ydir = vU - vD
            local deadzone = (GLOBAL.TUNING and GLOBAL.TUNING.CONTROLLER_DEADZONE_RADIUS) or 0.3

            if math.abs(xdir) >= deadzone or math.abs(ydir) >= deadzone then
                if self.IsBusy and self:IsBusy() then
                    return ret
                end
                if self.controller_attack_override ~= nil then
                    return ret
                end
                if CTRL_ATK and inst.sg ~= nil and inst.sg:HasStateTag("attack") and self:IsControlPressed(CTRL_ATK) then
                    return ret
                end
                local cam = GLOBAL.TheCamera
                if cam and cam.GetRightVec and cam.GetDownVec and type(self.RemoteDirectWalking) == "function" then
                    local dir = (cam:GetRightVec() * xdir - cam:GetDownVec() * ydir):GetNormalized()
                    if dir.x == dir.x and dir.z == dir.z then
                        -- Exact vanilla DoDirectWalking directional check (playercontroller.lua:L4325-L4343):
                        -- protect late key-release on SAME direction, but NEVER stall a NEW walk-cancel dodge direction!
                        local recent = self.recent_bufferedaction
                        if recent ~= nil and recent.act ~= nil then
                            local cur_ba = inst.GetBufferedAction and inst:GetBufferedAction() or nil
                            if cur_ba ~= nil and recent.act == cur_ba
                                and type(recent.x) == "number" and type(recent.z) == "number"
                                and math.abs(recent.x - dir.x) < 1e-4 and math.abs(recent.z - dir.z) < 1e-4 then
                                return ret
                            end
                            recent.act = nil
                            latency_stats.walk_cancels = latency_stats.walk_cancels + 1
                        end
                        if inst.ClearBufferedAction then inst:ClearBufferedAction() end
                        if self.CooldownRemoteController then self:CooldownRemoteController() end
                        if not self.directwalking then
                            self.time_direct_walking = 0
                            self.directwalking = true
                            self.dragwalking = false
                            self.predictwalking = false
                        end
                        self:RemoteDirectWalking(dir.x, dir.z)
                        latency_stats.wasd_instant_rpcs = latency_stats.wasd_instant_rpcs + 1
                    end
                end
            elseif self.directwalking and type(self.RemoteStopWalking) == "function" then
                if self.recent_bufferedaction ~= nil then
                    self.recent_bufferedaction.act = nil
                end
                self.directwalking = false
                self.dragwalking = false
                self.predictwalking = false
                if self.CooldownRemoteController then self:CooldownRemoteController() end
                self:RemoteStopWalking()
                latency_stats.wasd_instant_rpcs = latency_stats.wasd_instant_rpcs + 1
            end
            return ret
        end

        -- 9c. [v2.6.0 Pillar 2] 240Hz Fresh Flick-Click & Modifier-Click Action Resolution (0-33.3ms Stale Fix)
        --     Vanilla PlayerController:OnUpdate refreshes self.LMBaction / self.RMBaction ONLY at 30Hz.
        --     When a player flicks the mouse onto a target OR presses Ctrl/Shift/Alt (e.g. Ctrl+LMB Force Attack)
        --     and clicks within <33ms, OnLeftClick / OnRightClick reads stale LMBaction/RMBaction -> missed
        --     attack/pickup or accidental WALKTO into boss!
        --     Refreshing DoGetMouseActions() right on click-down ONLY when cursor, hover, or control modifiers
        --     changed since last OnUpdate eliminates up to 33.3ms of flick/modifier misses with zero duplicate
        --     RPCs and 100% compatibility with public servers, ActionQueue, Geometric Placement & ShowMe/Insight.
        if type(pc.OnLeftClick) == "function" and type(pc.OnRightClick) == "function" then
            local last_upd_mx, last_upd_my, last_upd_hover, last_upd_mods = nil, nil, nil, nil

            if type(pc.OnUpdate) == "function" then
                local orig_OnUpdate = pc.OnUpdate
                pc.OnUpdate = function(self, dt, ...)
                    local r = orig_OnUpdate(self, dt, ...)
                    if current_low_latency and self.inst == GLOBAL.ThePlayer and TheSim and TheSim.GetPosition then
                        last_upd_mx, last_upd_my = TheSim:GetPosition()
                        last_upd_hover = TheInput and TheInput.GetWorldEntityUnderMouse and TheInput:GetWorldEntityUnderMouse() or nil
                        last_upd_mods = type(self.EncodeControlMods) == "function" and self:EncodeControlMods() or nil
                    end
                    return r
                end
            end

            local function RefreshFreshClickActions(self, is_left)
                if not current_low_latency then return end
                if not (self.UsingMouse and self:UsingMouse()) then return end
                if (GLOBAL.IsPaused and GLOBAL.IsPaused()) or self.handler == nil then return end
                local inst = self.inst
                if inst == nil or inst ~= GLOBAL.ThePlayer or (inst.IsValid and not inst:IsValid()) then return end
                if not (self.IsEnabled and self:IsEnabled()) then return end
                if inst.IsActionsVisible and not inst:IsActionsVisible() then return end
                if self.placer_recipe ~= nil then return end
                if not is_left and self.deployplacer ~= nil then return end
                if self.IsAOETargeting and self:IsAOETargeting() then return end
                if TheInput.ControllerAttached and TheInput:ControllerAttached() then return end
                if TheInput.GetHUDEntityUnderMouse and TheInput:GetHUDEntityUnderMouse() ~= nil then return end
                local picker = inst.components and inst.components.playeractionpicker or nil
                if picker == nil or type(picker.DoGetMouseActions) ~= "function" then return end

                local mx, my = nil, nil
                if TheSim and TheSim.GetPosition then
                    mx, my = TheSim:GetPosition()
                end
                -- If mouse moved since last SimTick, ensure Input hoverinst is 0ms fresh at click pixel
                if mx ~= nil and (mx ~= last_upd_mx or my ~= last_upd_my) and TheInput.OnUpdate then
                    TheInput:OnUpdate()
                    if TheInput.GetHUDEntityUnderMouse and TheInput:GetHUDEntityUnderMouse() ~= nil then
                        return
                    end
                end
                local cur_hover = TheInput.GetWorldEntityUnderMouse and TheInput:GetWorldEntityUnderMouse() or nil
                local cur_mods = type(self.EncodeControlMods) == "function" and self:EncodeControlMods() or nil
                local mods_changed = (cur_mods ~= last_upd_mods)
                if mx ~= last_upd_mx or my ~= last_upd_my or cur_hover ~= last_upd_hover or mods_changed then
                    local prev_act = is_left and self.LMBaction or self.RMBaction
                    local new_lmb, new_rmb = picker:DoGetMouseActions()
                    self.LMBaction, self.RMBaction = new_lmb, new_rmb
                    last_upd_mx, last_upd_my, last_upd_hover, last_upd_mods = mx, my, cur_hover, cur_mods
                    latency_stats.flick_refreshes = latency_stats.flick_refreshes + 1
                    local next_act = is_left and new_lmb or new_rmb
                    local prev_code = prev_act and prev_act.action
                    local next_code = next_act and next_act.action
                    local prev_tgt  = prev_act and prev_act.target
                    local next_tgt  = next_act and next_act.target
                    if prev_code ~= next_code or prev_tgt ~= next_tgt then
                        latency_stats.flick_rescues = latency_stats.flick_rescues + 1
                        if mods_changed then
                            latency_stats.mod_click_rescues = latency_stats.mod_click_rescues + 1
                        end
                    end
                end
            end

            local orig_OnLeftClick = pc.OnLeftClick
            pc.OnLeftClick = function(self, down, ...)
                if down then
                    RefreshFreshClickActions(self, true)
                end
                return orig_OnLeftClick(self, down, ...)
            end

            local orig_OnRightClick = pc.OnRightClick
            pc.OnRightClick = function(self, down, ...)
                if down then
                    RefreshFreshClickActions(self, false)
                end
                return orig_OnRightClick(self, down, ...)
            end
        end
    end)
end

--------------------------------------------------------------------------------
-- 9d. Solo / Non-Caves Local Host Auto-Save Freeze Killer (C++ FastDataDumper Bridge)
--     Activates ONLY when hosting Master inside dontstarve_steam_x64.exe AND
--     HyperCore Server's HyperCore_FastDump is not present (zero double-hooking!).
--     100% vanilla save format with automatic fallback on any edge case.
--------------------------------------------------------------------------------
AddSimPostInit(function()
    local w = GLOBAL.TheWorld
    if not (w ~= nil and w.ismastersim) then
        return
    end
    if type(rawget(GLOBAL, "HyperCore_FastDump")) == "function" then
        return
    end
    local native_dump = rawget(GLOBAL, "HyperSmooth_FastDump")
    local orig_dumper = rawget(GLOBAL, "DataDumper")
    if type(native_dump) ~= "function" or type(orig_dumper) ~= "function" then
        return
    end
    if rawget(GLOBAL, "_HS_FastDumperBridged") then
        return
    end
    rawset(GLOBAL, "_HS_FastDumperBridged", true)
    rawset(GLOBAL, "DataDumper", function(value, varname, fastmode, ident)
        if fastmode and ident == nil then
            local dumped, err = native_dump(value, varname)
            if dumped ~= nil then
                latency_stats.host_fastdumps = latency_stats.host_fastdumps + 1
                return dumped
            end
            if err == "buffer_limit_exceeded" or err == "corrupt_number" then
                if GLOBAL.print then
                    GLOBAL.print("[HyperSmooth FastDataDumper] Refusing unsafe vanilla fallback: " .. tostring(err))
                end
                GLOBAL.error("[HyperSmooth FastDataDumper] Aborted unsafe serialization (" .. tostring(err) .. ")", 2)
            elseif err == "cycle_detected" or err == "node_limit_exceeded" or err == "depth_limit" or err == "stack_limit" then
                if GLOBAL.print then
                    GLOBAL.print("[HyperSmooth FastDataDumper] Safety limit (" .. tostring(err) .. "); downgrading to cycle-safe slow DataDumper")
                end
                return orig_dumper(value, varname, false, ident)
            end
        end
        return orig_dumper(value, varname, fastmode, ident)
    end)
end)

--------------------------------------------------------------------------------
-- 9e. C++ FastJson Bridge (json.encode / json.encode_compliant / json.decode)
--------------------------------------------------------------------------------
do
    local json_mod = rawget(GLOBAL, "json")
    if type(json_mod) ~= "table" and type(GLOBAL.require) == "function" then
        local ok_j, m_j = pcall(GLOBAL.require, "json")
        if ok_j and type(m_j) == "table" then
            json_mod = m_j
        end
    end
    local fnEnc = rawget(GLOBAL, "HyperCore_FastJsonEncode") or rawget(GLOBAL, "HyperSmooth_FastJsonEncode")
    local fnDec = rawget(GLOBAL, "HyperCore_FastJsonDecode") or rawget(GLOBAL, "HyperSmooth_FastJsonDecode")
    if type(json_mod) == "table" and not json_mod._hs_fastjson_bridged and
       type(fnEnc) == "function" and type(fnDec) == "function" then
        json_mod._hs_fastjson_bridged = true
        local orig_encode = json_mod.encode
        local orig_encode_comp = json_mod.encode_compliant
        local orig_decode = json_mod.decode
        if type(orig_encode) == "function" then
            json_mod.encode = function(v)
                local res = fnEnc(v, false)
                if res ~= nil then
                    return res
                end
                return orig_encode(v)
            end
        end
        if type(orig_encode_comp) == "function" then
            json_mod.encode_compliant = function(v)
                local res = fnEnc(v, true)
                if res ~= nil then
                    return res
                end
                return orig_encode_comp(v)
            end
        end
        if type(orig_decode) == "function" then
            json_mod.decode = function(s, startPos)
                if (startPos == nil or startPos == 1) and type(s) == "string" then
                    local ok, val, nextPos = fnDec(s)
                    if ok then
                        return val, nextPos
                    end
                end
                return orig_decode(s, startPos)
            end
        end
    end
end

--------------------------------------------------------------------------------
-- 9f. Zero-Alloc EntityScript:PushEvent_Internal & Unrolled HasTags / HasOneOfTags
--------------------------------------------------------------------------------
do
    local EntityScript = rawget(GLOBAL, "EntityScript")
    if type(EntityScript) ~= "table" and type(GLOBAL.require) == "function" then
        local ok_es, es_mod = pcall(GLOBAL.require, "entityscript")
        if ok_es and type(es_mod) == "table" then
            EntityScript = es_mod
        end
    end
    if type(EntityScript) == "table" then
        if not EntityScript._hs_fast_pushevent and type(EntityScript.PushEvent_Internal) == "function" then
            EntityScript._hs_fast_pushevent = true
            local pe_pool = {}
            local pe_pool_n = 0
            local function AcquireScratch()
                if pe_pool_n > 0 then
                    local t = pe_pool[pe_pool_n]
                    pe_pool[pe_pool_n] = nil
                    pe_pool_n = pe_pool_n - 1
                    return t
                end
                return {}
            end
            local function ReleaseScratch(t, n)
                for i = n, 1, -1 do
                    t[i] = nil
                end
                if pe_pool_n < 16 then
                    pe_pool_n = pe_pool_n + 1
                    pe_pool[pe_pool_n] = t
                end
            end

            function EntityScript:PushEvent_Internal(event, data, immediate)
                if self.event_listeners then
                    local listeners = self.event_listeners[event]
                    if listeners then
                        local first_ent, first_fns = next(listeners)
                        if first_ent ~= nil then
                            if next(listeners, first_ent) == nil then
                                local n1 = #first_fns
                                if n1 == 1 then
                                    first_fns[1](self, data)
                                elseif n1 > 1 then
                                    local tocall = AcquireScratch()
                                    for i = 1, n1 do
                                        tocall[i] = first_fns[i]
                                    end
                                    for i = 1, n1 do
                                        tocall[i](self, data)
                                    end
                                    ReleaseScratch(tocall, n1)
                                end
                            else
                                local tocall = AcquireScratch()
                                local n = 0
                                for _, fns in pairs(listeners) do
                                    for i = 1, #fns do
                                        n = n + 1
                                        tocall[n] = fns[i]
                                    end
                                end
                                for i = 1, n do
                                    tocall[i](self, data)
                                end
                                ReleaseScratch(tocall, n)
                            end
                        end
                    end
                end

                if self.sg then
                    if immediate then
                        self.sg:HandleEvent(event, data)
                    else
                        local sgm = rawget(GLOBAL, "SGManager")
                        if self.sg:IsListeningForEvent(event) and sgm and sgm:OnPushEvent(self.sg) then
                            self.sg:PushEvent(event, data)
                        end
                    end
                end

                if self.brain then
                    self.brain:PushEvent(event, data)
                end
            end
        end

        if rawget(GLOBAL, "BRANCH") ~= "dev" and not EntityScript._hs_fast_hastags then
            EntityScript._hs_fast_hastags = true
            local _unpack = rawget(GLOBAL, "unpack") or unpack

            function EntityScript:HasTags(t1, ...)
                if t1 == nil then
                    return self.entity:HasAllTags(...)
                elseif type(t1) == "table" then
                    local a, b, c = t1[1], t1[2], t1[3]
                    if a == nil then
                        return self.entity:HasAllTags()
                    elseif b == nil then
                        return self.entity:HasTag(a)
                    elseif c == nil then
                        return self.entity:HasAllTags(a, b)
                    elseif t1[4] == nil then
                        return self.entity:HasAllTags(a, b, c)
                    else
                        return self.entity:HasAllTags(_unpack(t1))
                    end
                end
                return self.entity:HasAllTags(t1, ...)
            end

            function EntityScript:HasOneOfTags(t1, ...)
                if t1 == nil then
                    return self.entity:HasAnyTag(...)
                elseif type(t1) == "table" then
                    local a, b, c = t1[1], t1[2], t1[3]
                    if a == nil then
                        return self.entity:HasAnyTag()
                    elseif b == nil then
                        return self.entity:HasTag(a)
                    elseif c == nil then
                        return self.entity:HasAnyTag(a, b)
                    elseif t1[4] == nil then
                        return self.entity:HasAnyTag(a, b, c)
                    else
                        return self.entity:HasAnyTag(_unpack(t1))
                    end
                end
                return self.entity:HasAnyTag(t1, ...)
            end

            EntityScript.HasAllTags = EntityScript.HasTags
            EntityScript.HasAnyTag = EntityScript.HasOneOfTags
        end
    end
end

--------------------------------------------------------------------------------
-- 9g. On-Demand Lua Instruction Hotspot Profiler: c_hsprof(sec)
--------------------------------------------------------------------------------
do
    local prof_active = false
    rawset(GLOBAL, "c_hsprof", function(seconds)
        local dbg = rawget(GLOBAL, "debug")
        local ui_zh = ResolveUiIsZh()
        if type(dbg) ~= "table" or type(dbg.sethook) ~= "function" or type(dbg.getinfo) ~= "function" then
            if GLOBAL.print then
                GLOBAL.print(ui_zh
                    and "[HyperSmooth 采样] 当前环境不支持 debug.sethook。"
                    or "[HyperSmooth Prof] debug.sethook unavailable in this environment.")
            end
            return false
        end
        if prof_active then
            if GLOBAL.print then
                GLOBAL.print(ui_zh
                    and "[HyperSmooth 采样] 正在采样中，请稍候。"
                    or "[HyperSmooth Prof] Profiling already in progress.")
            end
            return false
        end
        local dur = tonumber(seconds) or 5
        if dur < 1 then dur = 1 end
        if dur > 30 then dur = 30 end

        local os_mod = rawget(GLOBAL, "os")
        local get_time = (type(os_mod) == "table" and type(os_mod.clock) == "function")
            and os_mod.clock or (rawget(GLOBAL, "GetTime") or function() return 0 end)
        local start_t = get_time()
        local stop_t = start_t + dur
        local counts = {}
        local total = 0
        prof_active = true

        local function FinishProf()
            if not prof_active then return end
            prof_active = false
            dbg.sethook()
            local rows = {}
            for k, v in pairs(counts) do
                rows[#rows + 1] = { k = k, v = v }
            end
            table.sort(rows, function(a, b) return a.v > b.v end)
            if GLOBAL.print then
                local fin_zh = ResolveUiIsZh()
                GLOBAL.print(fin_zh
                    and string.format("=== HyperSmooth Lua 热点采样 (耗时%.1fs, 样本=%d) ===",
                        math.max(0.01, get_time() - start_t), total)
                    or string.format("=== HyperSmooth Lua Hotspot Profile (%.1fs, samples=%d) ===",
                        math.max(0.01, get_time() - start_t), total))
                local limit = math.min(15, #rows)
                for i = 1, limit do
                    local pct = total > 0 and (100.0 * rows[i].v / total) or 0.0
                    GLOBAL.print(string.format("  #%02d %5.1f%% (%5d) %s", i, pct, rows[i].v, rows[i].k))
                end
            end
        end

        dbg.sethook(function()
            if get_time() >= stop_t then
                FinishProf()
                return
            end
            local info = dbg.getinfo(2, "nSl")
            if info then
                local src = info.short_src or info.source or "?"
                local line = info.linedefined or info.currentline or 0
                local name = info.name or "?"
                local key = string.format("%s:%d (%s)", src, line, name)
                counts[key] = (counts[key] or 0) + 1
                total = total + 1
            end
        end, "", 2000)

        local world = rawget(GLOBAL, "TheWorld")
        if world and type(world.DoTaskInTime) == "function" then
            world:DoTaskInTime(dur, FinishProf)
        elseif type(rawget(GLOBAL, "scheduler")) == "table" and type(GLOBAL.scheduler.ExecuteInTime) == "function" then
            GLOBAL.scheduler:ExecuteInTime(dur, FinishProf)
        end
        if GLOBAL.print then
            GLOBAL.print(ui_zh
                and string.format("[HyperSmooth 采样] 正在采样 Lua 调用热点（%.0f秒，每 2000 条指令一次）...", dur)
                or string.format("[HyperSmooth Prof] Sampling Lua callsites for %.0fs (every 2000 instructions)...", dur))
        end
        return true
    end)
end

--------------------------------------------------------------------------------
-- 9h. Low-Overhead Spike Black Box Console X-Ray: c_hsblackbox(arg)
--------------------------------------------------------------------------------
do
    local CLIENT_CULPRIT_ZH = {
        CLIENT_LUA           = "客户端Lua逻辑",
        CLIENT_GC            = "客户端GC回收",
        CLIENT_RENDER_SUBMIT = "渲染提交CPU",
        GPU_WAIT_OR_SWAP     = "GPU/Swap阻塞",
        CLIENT_IO_HOST       = "纹理/加载/宿主IO",
        CLIENT_NET_CPU       = "客户端网络处理",
        CLIENT_CPU_OTHER     = "客户端其他CPU",
        NET_SPIKE            = "网络延迟/丢包突刺",
        MIXED                = "混合因素",
    }

    local function FormatEpochMsShort(epochMs)
        local em = tonumber(epochMs) or 0
        if em <= 0 then return "--:--:--.000" end
        local sec = math.floor(em / 1000)
        local ms = math.floor(em - sec * 1000)
        local os_mod = rawget(GLOBAL, "os")
        if type(os_mod) == "table" and type(os_mod.date) == "function" then
            local okD, s = pcall(os_mod.date, "%H:%M:%S", sec)
            if okD and type(s) == "string" then
                return string.format("%s.%03d", s, ms)
            end
        end
        return string.format("%ds.%03d", sec % 86400, ms)
    end

    rawset(GLOBAL, "c_hsblackbox", function(arg)
        local ui_zh = ResolveUiIsZh()
        local p = GLOBAL.print
        if type(p) ~= "function" then return false end

        if type(arg) == "string" then
            local cmd = arg:lower()
            if cmd == "clear" then
                local fnClr = rawget(GLOBAL, "HyperSmooth_ClearBlackbox")
                if type(fnClr) ~= "function" then
                    p(ui_zh and "[HyperSmooth 黑匣子] 未检测到 dinput8.dll 原生黑匣子接口。"
                             or "[HyperSmooth Blackbox] dinput8.dll blackbox API unavailable.")
                    return false
                end
                local ok = pcall(fnClr)
                p(ui_zh and ("[HyperSmooth 黑匣子] 已清空客户端突刺缓存 -> " .. tostring(ok))
                         or ("[HyperSmooth Blackbox] Cleared client spike ring -> " .. tostring(ok)))
                return ok == true
            elseif cmd == "export" then
                local fnExp = rawget(GLOBAL, "HyperSmooth_ExportBlackbox")
                if type(fnExp) ~= "function" then
                    p(ui_zh and "[HyperSmooth 黑匣子] 未检测到 dinput8.dll 原生黑匣子接口。"
                             or "[HyperSmooth Blackbox] dinput8.dll blackbox API unavailable.")
                    return false
                end
                local okCall, okExp, outPath = pcall(fnExp)
                if okCall and okExp then
                    p(ui_zh and string.format("[HyperSmooth 黑匣子] 已导出 JSON 快照 -> %s", tostring(outPath))
                             or string.format("[HyperSmooth Blackbox] Exported JSON snapshot -> %s", tostring(outPath)))
                    return true
                else
                    p(ui_zh and string.format("[HyperSmooth 黑匣子] 导出失败 -> %s", tostring(outPath or ""))
                             or string.format("[HyperSmooth Blackbox] Export failed -> %s", tostring(outPath or "")))
                    return false
                end
            end
        end

        local fnGet = rawget(GLOBAL, "HyperSmooth_GetBlackbox")
        if type(fnGet) ~= "function" then
            p(ui_zh and "[HyperSmooth 黑匣子] 未检测到 dinput8.dll 原生黑匣子接口。"
                     or "[HyperSmooth Blackbox] dinput8.dll blackbox API unavailable.")
            return false
        end

        local limit = tonumber(arg) or 10
        limit = math.max(1, math.min(64, math.floor(limit)))
        local okGet, total, _, jsonStr = pcall(fnGet, limit)
        if not okGet or type(jsonStr) ~= "string" then
            p(ui_zh and "[HyperSmooth 黑匣子] 读取原生缓存失败。"
                     or "[HyperSmooth Blackbox] Failed reading native ring.")
            return false
        end

        local json_mod = rawget(GLOBAL, "json")
        if type(json_mod) ~= "table" and type(GLOBAL.require) == "function" then
            local okJ, mJ = pcall(GLOBAL.require, "json")
            if okJ and type(mJ) == "table" then json_mod = mJ end
        end
        if type(json_mod) ~= "table" or type(json_mod.decode) ~= "function" then
            p(jsonStr)
            return true
        end

        local okDec, tbl = pcall(json_mod.decode, jsonStr)
        if not okDec or type(tbl) ~= "table" then
            p(jsonStr)
            return true
        end

        local evs = type(tbl.events) == "table" and tbl.events or {}
        local threshMs = tonumber(tbl.threshold_ms) or 25
        local totCount = tonumber(tbl.total) or tonumber(total) or #evs
        p(ui_zh
            and string.format("=== HyperSmooth 掉帧黑匣子 (累计=%d, 阈值=%dms, 显示=%d条) ===",
                totCount, threshMs, #evs)
            or string.format("=== HyperSmooth Spike Blackbox (total=%d, thresh=%dms, showing=%d) ===",
                totCount, threshMs, #evs))
        if #evs == 0 then
            p(ui_zh and "  (暂无掉帧或网络突刺记录)" or "  (no frame/GPU/net spike events recorded yet)")
            return true
        end

        for i = 1, #evs do
            local e = evs[i]
            local ts = FormatEpochMsShort(e.epoch_ms)
            local tag = tostring(e.culprit or "MIXED")
            local culpritLbl = ui_zh and ((CLIENT_CULPRIT_ZH[tag] or tag) .. "(" .. tag .. ")") or tag
            local gcTag = ((e.gc_full or 0) ~= 0) and (ui_zh and " [全量GC]" or " [FULL_GC]") or ""
            p(ui_zh
                and string.format(
                    "  #%02d [%s] 帧#%d [%s]%s | 帧时=%.2fms (CPU=%.2f Lua=%.2f GC=%.2f 世界=%.2f 后处=%.2f Swap=%.2f 排空=%.2f 动画=%.2f/%d体 纹理=%.2f 解压=%.2f 着色=%.2f 宿主=%.2f 其他=%.2f) | Ping=%d/%dms 丢包=%.1f%% 重传=%d",
                    i, ts, tonumber(e.frame_seq) or 0, culpritLbl, gcTag,
                    (tonumber(e.frame_us) or 0) * 0.001,
                    (tonumber(e.cpu_busy_us) or 0) * 0.001,
                    (tonumber(e.lua_us) or 0) * 0.001,
                    (tonumber(e.gc_us) or 0) * 0.001,
                    (tonumber(e.world_us) or 0) * 0.001,
                    (tonumber(e.post_us) or 0) * 0.001,
                    (tonumber(e.swap_us) or 0) * 0.001,
                    (tonumber(e.gpu_drain_us) or 0) * 0.001,
                    (tonumber(e.anim_us) or 0) * 0.001, tonumber(e.anim_ents) or 0,
                    (tonumber(e.tex_us) or 0) * 0.001,
                    (tonumber(e.unz_us) or 0) * 0.001,
                    (tonumber(e.shader_us) or 0) * 0.001,
                    (tonumber(e.host_us) or 0) * 0.001,
                    (tonumber(e.other_us) or 0) * 0.001,
                    tonumber(e.ping_avg_ms) or -1, tonumber(e.ping_last_ms) or -1,
                    tonumber(e.loss_pct) or 0.0, tonumber(e.resend) or 0)
                or string.format(
                    "  #%02d [%s] F#%d [%s]%s | Frame=%.2fms (CPU=%.2f Lua=%.2f GC=%.2f World=%.2f Post=%.2f Swap=%.2f Drain=%.2f Anim=%.2f/%de Tex=%.2f Zip=%.2f Shdr=%.2f Host=%.2f Other=%.2f) | Ping=%d/%dms Loss=%.1f%% Resend=%d",
                    i, ts, tonumber(e.frame_seq) or 0, culpritLbl, gcTag,
                    (tonumber(e.frame_us) or 0) * 0.001,
                    (tonumber(e.cpu_busy_us) or 0) * 0.001,
                    (tonumber(e.lua_us) or 0) * 0.001,
                    (tonumber(e.gc_us) or 0) * 0.001,
                    (tonumber(e.world_us) or 0) * 0.001,
                    (tonumber(e.post_us) or 0) * 0.001,
                    (tonumber(e.swap_us) or 0) * 0.001,
                    (tonumber(e.gpu_drain_us) or 0) * 0.001,
                    (tonumber(e.anim_us) or 0) * 0.001, tonumber(e.anim_ents) or 0,
                    (tonumber(e.tex_us) or 0) * 0.001,
                    (tonumber(e.unz_us) or 0) * 0.001,
                    (tonumber(e.shader_us) or 0) * 0.001,
                    (tonumber(e.host_us) or 0) * 0.001,
                    (tonumber(e.other_us) or 0) * 0.001,
                    tonumber(e.ping_avg_ms) or -1, tonumber(e.ping_last_ms) or -1,
                    tonumber(e.loss_pct) or 0.0, tonumber(e.resend) or 0))
        end
        return true
    end)
end

--------------------------------------------------------------------------------
-- 10. Lightweight Zero-Churn FPS + 1% Low + P99 HUD & Optional Custom Hotkeys
--------------------------------------------------------------------------------
local function FormatTargetLabel(fps, ui_zh)
    if fps == 0 then
        return ui_zh and "无限制" or "Unlimited"
    end
    return tostring(fps) .. " FPS"
end

local function CanTriggerInGameHotkey()
    local player = GLOBAL.ThePlayer
    local fe = GLOBAL.TheFrontEnd
    if not player or not fe then
        return false
    end
    local screen = fe:GetActiveScreen()
    if not screen or (screen.name ~= "HUD" and screen.name ~= "MapScreen") then
        return false
    end
    local hud = player.HUD
    if hud then
        if hud.IsChatInputScreenOpen and hud:IsChatInputScreenOpen() then return false end
        if hud.IsConsoleScreenOpen and hud:IsConsoleScreenOpen() then return false end
    end
    return true
end

AddClassPostConstruct("screens/playerhud", function(self)
    -- Re-sync on entering world so any session reload matches active config immediately
    SyncToNativePlugin(false)

    -- One-time per-session update prompt when Workshop mod updated ahead of manual bin64/dinput8.dll
    if not dll_outdated_toast_shown then
        local d_st, d_ver, d_exp = GetDllVersionStatus()
        if d_st == "outdated" then
            dll_outdated_toast_shown = true
            local is_zh = ResolveUiIsZh()
            local ver_tag = tostring(d_ver or "old")
            toast_msg = is_zh
                and string.format("[请更新] 检测到旧版 dinput8.dll (%s)，请手动替换为 v%s", ver_tag, tostring(d_exp))
                or string.format("[Update] Outdated bin64/dinput8.dll (%s) -> please update to v%s", ver_tag, tostring(d_exp))
            toast_timer = 10.0
            toast_is_warning = true
            hud_dirty_now = true
            GLOBAL.print(string.format("[HyperSmooth] WARNING: Outdated bin64/dinput8.dll (%s) vs Workshop Mod v%s — please manually update bin64/dinput8.dll!", ver_tag, tostring(d_exp)))
        end
    end

    local txt = self:AddChild(Text(GLOBAL.NUMBERFONT, 22, ""))
    txt:SetHAnchor(GLOBAL.ANCHOR_RIGHT)
    txt:SetVAnchor(GLOBAL.ANCHOR_TOP)
    txt:SetRegionSize(1280, 30)
    txt:SetHAlign(GLOBAL.ANCHOR_RIGHT)
    txt:SetPosition(-795, -18, 0) -- right edge @ -155px: completely clear of the Day Clock!
    txt:SetColour(0.55, 1.0, 0.65, 0.92)
    txt:SetClickable(false)
    if not current_show_fps_hud then
        txt:Hide()
    end
    fps_hud_text = txt

    local accum_time = 0
    local accum_frames = 0
    local hd2d_accum = 0
    local last_real_fps = 0
    local last_ms = 0
    local old_OnUpdate = self.OnUpdate

    self.OnUpdate = function(hud, dt)
        if old_OnUpdate then
            old_OnUpdate(hud, dt)
        end
        if dt and dt > 0 and dt < 0.5 then
            TickVisualStudioDynamic(dt)
        end
        if not (fps_hud_text and fps_hud_text.inst and fps_hud_text.inst:IsValid()) then
            return
        end
        if dt and dt > 0 and dt < 1.0 then
            accum_time = accum_time + dt
            accum_frames = accum_frames + 1
        end
        if toast_timer > 0 then
            toast_timer = toast_timer - (dt or 0)
            if toast_timer <= 0 then
                toast_is_warning = false
                if not current_show_fps_hud then
                    fps_hud_text:Hide()
                end
            end
        end
        if visual_bench_timer > 0 then
            visual_bench_timer = visual_bench_timer - (dt or 0)
        end
        if hud_dirty_now or accum_time >= 0.25 then
            if accum_time > 0 and accum_frames > 0 then
                last_real_fps = accum_frames / accum_time
                last_ms = (accum_time / accum_frames) * 1000.0
                accum_time = 0
                accum_frames = 0
            end
            hud_dirty_now = false

            -- Prefer zero-allocation C++ 512-frame RTSS ring telemetry when available
            local fnStats = rawget(GLOBAL, "HyperSmooth_GetFrameStats")
            local disp_fps = last_real_fps
            local color_metric = last_real_fps
            local has_native_stats = false
            local avg_val, low1_val, p99_val, eff_simhz = last_real_fps, last_real_fps, last_ms, active_sim_tick_hz

            if type(fnStats) == "function" then
                local ok, avg, low1, p99, _, _, simhz = pcall(fnStats)
                if ok and type(avg) == "number" and avg > 1.0 and type(low1) == "number" and low1 > 1.0 then
                    has_native_stats = true
                    avg_val = avg
                    low1_val = low1
                    p99_val = p99 or last_ms
                    eff_simhz = simhz or active_sim_tick_hz
                    disp_fps = avg
                    color_metric = low1
                    last_hud_avg_fps = avg
                    last_hud_low1_fps = low1
                end
            end
            if not has_native_stats then
                last_hud_avg_fps = disp_fps
                last_hud_low1_fps = disp_fps
            end
            if type(HS_PanelTickTelemetry) == "function" then
                HS_PanelTickTelemetry()
            end

            local ui_zh = ResolveUiIsZh()
            local is_detail = (current_hud_mode == "detail")
            local sim_badge = (eff_simhz == 60) and " [60Hz]" or ""
            local isGl = false
            local fnBackend = rawget(GLOBAL, "HyperSmooth_GetRenderBackend")
            if type(fnBackend) == "function" then
                local okB, rawBe = pcall(fnBackend)
                if okB and IsRenderBackendOpenGL(rawBe) then
                    isGl = true
                end
            end
            local gl_badge = isGl and " [GL]" or ""

            local metric_str
            if is_detail then
                if SHOW_1PCT_LOW and has_native_stats then
                    metric_str = ui_zh
                        and string.format("帧率 %.0f · 最低帧 %.0f · 帧时 %.1fms%s%s", avg_val, low1_val, p99_val, sim_badge, gl_badge)
                        or string.format("FPS %.0f · 1%%L %.0f · Frame %.1fms%s%s", avg_val, low1_val, p99_val, sim_badge, gl_badge)
                else
                    metric_str = ui_zh
                        and string.format("帧率 %.0f · 帧时 %.1fms%s%s", disp_fps, last_ms, sim_badge, gl_badge)
                        or string.format("FPS %.0f · Frame %.1fms%s%s", disp_fps, last_ms, sim_badge, gl_badge)
                end
            else
                metric_str = ui_zh
                    and string.format("帧率 %.0f%s", disp_fps, sim_badge)
                    or string.format("FPS %.0f%s", disp_fps, sim_badge)
            end

            local fnNet = rawget(GLOBAL, "HyperSmooth_GetNetStats")
            if type(fnNet) == "function" then
                local okN, nConn, pAvg, _, lCur = pcall(fnNet)
                if okN and nConn == true and type(pAvg) == "number" and pAvg >= 0 then
                    if type(lCur) == "number" and lCur >= 0.5 then
                        metric_str = metric_str .. (ui_zh
                            and string.format(" | 延迟 %dms · 丢包 %.0f%%", pAvg, lCur)
                            or string.format(" | Ping %dms · Loss %.0f%%", pAvg, lCur))
                    else
                        metric_str = metric_str .. (ui_zh
                            and string.format(" | 延迟 %dms", pAvg)
                            or string.format(" | Ping %dms", pAvg))
                    end
                end
            end

            -- Append compact GPU/Busy/VRAM telemetry for 12s after visual switch or when pinned via c_hsbench()
            if visual_bench_timer > 0 or visual_bench_pinned then
                local fnPerf = rawget(GLOBAL, "HyperSmooth_GetVisualPerf")
                if type(fnPerf) == "function" then
                    local okP, _, _, _, wMs, _, bMs, _, vCur = pcall(fnPerf)
                    if okP then
                        local mode_tags = ui_zh
                            and { [0] = "原版", [1] = "护眼高清", [2] = "柔和绘本", [3] = "自定义" }
                            or { [0] = "Vanilla", [1] = "EyeHD", [2] = "Storybook", [3] = "Custom" }
                        local tag = mode_tags[current_visual_preset] or mode_tags[0]
                        local vram_part = (type(vCur) == "number" and vCur > 0.5)
                            and string.format(" %.0fMB", vCur) or ""
                        metric_str = metric_str .. (ui_zh
                            and string.format(" | %s 耗时%.1fms(世界%.1f)%s", tag, bMs or 0, wMs or 0, vram_part)
                            or string.format(" | %s %.1fms(W%.1f)%s", tag, bMs or 0, wMs or 0, vram_part))
                    end
                end
            end

            if toast_timer > 0 and toast_msg then
                if not current_show_fps_hud then
                    fps_hud_text:Show()
                    fps_hud_text:SetString(toast_msg)
                else
                    fps_hud_text:SetString(metric_str .. " | " .. toast_msg)
                end
            elseif current_show_fps_hud then
                fps_hud_text:Show()
                fps_hud_text:SetString(metric_str)
            end

            if toast_timer > 0 and toast_is_warning then
                fps_hud_text:SetColour(1.0, 0.85, 0.32, 0.98)
            elseif color_metric >= 95 or (disp_fps >= 110 and color_metric >= disp_fps * 0.72) then
                fps_hud_text:SetColour(0.45, 1.0, 0.65, 0.92)
            elseif color_metric >= 52 then
                fps_hud_text:SetColour(1.0, 0.92, 0.45, 0.92)
            else
                fps_hud_text:SetColour(1.0, 0.45, 0.45, 0.92)
            end
        end
    end
end)

-- Live Any-Key & Mouse-Button Rebind Engine (wired by keybind.lua in Mod Config & ESC->Settings->Controls)
local function CycleHudStateWithToast()
    local ui_zh = ResolveUiIsZh()
    if not current_show_fps_hud then
        current_show_fps_hud = true
        current_hud_mode = "simple"
        toast_msg = ui_zh and "HUD -> 简洁模式" or "HUD -> Compact"
    elseif current_hud_mode ~= "detail" then
        current_hud_mode = "detail"
        toast_msg = ui_zh and "HUD -> 详细模式" or "HUD -> Detailed"
    else
        current_show_fps_hud = false
        toast_msg = ui_zh and "HUD -> 已隐藏" or "HUD -> Hidden"
    end
    SyncToNativePlugin(true)
    toast_timer = 2.2
    hud_dirty_now = true
end

local HOTKEY_ACTIONS = {
    hotkey_cycle = function()
        local ui_zh = ResolveUiIsZh()
        local next_idx = 1
        for i, v in ipairs(FPS_PRESETS) do
            if v == current_target_fps then
                next_idx = (i % #FPS_PRESETS) + 1
                break
            end
        end
        current_target_fps = FPS_PRESETS[next_idx]
        SyncToNativePlugin(true)
        local lbl = FormatTargetLabel(current_target_fps, ui_zh)
        if HasNativePlugin() then
            toast_msg = (ui_zh and "目标帧率 -> " or "Target FPS -> ") .. lbl
        else
            toast_msg = (ui_zh and "目标帧率 -> " or "Target FPS -> ") .. lbl .. (ui_zh and " (需装 bin64/dinput8.dll)" or " (Need bin64/dinput8.dll)")
        end
        toast_timer = 3.0
        hud_dirty_now = true
    end,
    hotkey_skeletal = function()
        local ui_zh = ResolveUiIsZh()
        current_skeletal_tween = not current_skeletal_tween
        SyncToNativePlugin(true)
        local st_lbl = ui_zh and (current_skeletal_tween and "开" or "关") or (current_skeletal_tween and "ON" or "OFF")
        if HasNativePlugin() then
            toast_msg = (ui_zh and "240帧补帧 -> " or "240Hz Smooth -> ") .. st_lbl
        else
            toast_msg = (ui_zh and "240帧补帧 -> " or "240Hz Smooth -> ") .. st_lbl .. (ui_zh and " (需装 bin64/dinput8.dll)" or " (Need bin64/dinput8.dll)")
        end
        toast_timer = 3.0
        hud_dirty_now = true
    end,
    hotkey_hud = CycleHudStateWithToast,
    hotkey_visual = function()
        local ui_zh = ResolveUiIsZh()
        local has_custom = HasAnyCustomActive()
        local cycle_n = has_custom and 4 or 3
        current_visual_preset = (current_visual_preset + 1) % cycle_n
        visual_bench_timer = 12.0
        SyncToNativePlugin(true)
        local labels = ui_zh and {
            [0] = "画质 -> 原版纯净",
            [1] = "画质 -> 护眼高清",
            [2] = "画质 -> 电影绘本",
            [3] = "画质 -> 自定义",
        } or {
            [0] = "Visual -> Vanilla",
            [1] = "Visual -> Eye-Care HD",
            [2] = "Visual -> Cinematic",
            [3] = "Visual -> Custom",
        }
        toast_msg = labels[current_visual_preset] or ((ui_zh and "画质 -> " or "Visual -> ") .. tostring(current_visual_preset))
        toast_timer = 2.5
        hud_dirty_now = true
    end,
    hotkey_panel = function()
        if type(HS_TogglePanel) == "function" then
            HS_TogglePanel()
        end
    end,
}

-- Mod-environment API bridge consumed by hs_panel.lua (Sleek In-Game Live Customizer Side-Panel)
HS_PanelApi = {
    SetPreset = function(preset_id)
        if type(preset_id) ~= "number" or preset_id < 0 or preset_id > 3 then return end
        current_visual_preset = preset_id
        visual_bench_timer = 12.0
        SyncToNativePlugin(true)
        hud_dirty_now = true
    end,
    ToggleVisualFlag = function(field)
        if type(field) ~= "string" or custom_vis_flags[field] == nil then return end
        SnapshotFlagsToCustom()
        custom_vis_flags[field] = not custom_vis_flags[field]
        EnsureFlagTuneOnToggle(field, custom_vis_flags[field])
        current_visual_preset = HasAnyCustomActive() and 3 or 0
        visual_bench_timer = 12.0
        SyncToNativePlugin(true)
        hud_dirty_now = true
    end,
    StepTune = function(field, delta)
        if type(field) ~= "string" or TUNE_BOUNDS[field] == nil or type(delta) ~= "number" then return end
        local cur_val = GetActiveVisTune()[field]
        if type(cur_val) ~= "number" then cur_val = TUNE_BOUNDS[field].default end
        ApplyTuneValue(field, cur_val + delta)
    end,
    SetTune = function(field, val)
        ApplyTuneValue(field, val)
    end,
    ToggleTuneBool = function(field)
        local b = TUNE_BOUNDS[field]
        if not (b and b.is_bool) then return end
        local cur_on = (GetActiveVisTune()[field] or 0) > 0.5
        ApplyTuneValue(field, cur_on and 0 or 1)
    end,
    ResetTuneField = function(field)
        local b = TUNE_BOUNDS[field]
        if not b then return end
        ApplyTuneValue(field, b.default)
    end,
    ResetTuneGroup = function(keys)
        if type(keys) ~= "table" then return end
        SnapshotFlagsToCustom()
        local touched_grade, touched_filter = false, false
        for i = 1, #keys do
            local k = keys[i]
            local b = TUNE_BOUNDS[k]
            if b then
                vis_tune[k] = b.default
                if GRADE_KEY_SET[k] then touched_grade = true end
                if FILTER_KEY_SET[k] then touched_filter = true end
            end
        end
        if touched_grade then
            custom_vis_flags.tone = HasAnyGradingTune(vis_tune)
        end
        if touched_filter then
            custom_vis_flags.clean_filter = HasAnyCleanFilterTune(vis_tune)
        end
        current_visual_preset = HasAnyCustomActive() and 3 or 0
        visual_bench_timer = 12.0
        SyncToNativePlugin(true)
        hud_dirty_now = true
    end,
    ApplyColorMood = function(mood)
        SnapshotFlagsToCustom()
        -- Always start mood cleanly with neutral CDL so prior mood channels don't bleed!
        vis_tune.slope_r = 1.00; vis_tune.slope_g = 1.00; vis_tune.slope_b = 1.00
        vis_tune.offset_r = 0.00; vis_tune.offset_g = 0.00; vis_tune.offset_b = 0.00
        vis_tune.power_r = 1.00; vis_tune.power_g = 1.00; vis_tune.power_b = 1.00
        vis_tune.lightness = 0.00; vis_tune.gamma = 1.00; vis_tune.original_mix = 0.00
        if mood == "warm" then
            vis_tune.exposure   = 0.06
            vis_tune.temp       = 0.32
            vis_tune.tint       = -0.03
            vis_tune.contrast   = 0.03
            vis_tune.sat        = 1.02
            vis_tune.vibrance   = 0.22
            vis_tune.filmic     = 0.32
            vis_tune.hl_desat   = 0.35
            vis_tune.sun_fill   = 0.35
            vis_tune.vignette   = 0.06
            vis_tune.grain      = 0.00
            vis_tune.vanilla_cc = 0.75
            custom_vis_flags.tone = true
        elseif mood == "cool" then
            vis_tune.exposure   = 0.02
            vis_tune.temp       = -0.18
            vis_tune.tint       = 0.02
            vis_tune.contrast   = 0.04
            vis_tune.sat        = 0.96
            vis_tune.vibrance   = 0.12
            vis_tune.filmic     = 0.20
            vis_tune.hl_desat   = 0.25
            vis_tune.sun_fill   = 0.00
            vis_tune.vignette   = 0.04
            vis_tune.grain      = 0.00
            vis_tune.vanilla_cc = 0.85
            custom_vis_flags.tone = true
        elseif mood == "film" then
            vis_tune.exposure   = 0.04
            vis_tune.temp       = 0.18
            vis_tune.tint       = -0.04
            vis_tune.contrast   = 0.08
            vis_tune.gamma      = 0.96
            vis_tune.sat        = 0.94
            vis_tune.vibrance   = 0.28
            vis_tune.filmic     = 0.55
            vis_tune.hl_desat   = 0.50
            vis_tune.sun_fill   = 0.45
            vis_tune.slope_r    = 1.03
            vis_tune.slope_b    = 0.96
            vis_tune.offset_b   = 0.01
            vis_tune.vignette   = 0.12
            vis_tune.grain      = 0.08
            vis_tune.vanilla_cc = 0.70
            custom_vis_flags.tone = true
        else
            vis_tune.exposure   = 0.00
            vis_tune.temp       = 0.00
            vis_tune.tint       = 0.00
            vis_tune.contrast   = 0.00
            vis_tune.sat        = 1.00
            vis_tune.vibrance   = 0.00
            vis_tune.filmic     = 0.00
            vis_tune.hl_desat   = 0.00
            vis_tune.sun_fill   = 0.00
            vis_tune.vignette   = 0.00
            vis_tune.grain      = 0.00
            vis_tune.vanilla_cc = 1.00
            custom_vis_flags.tone = false
        end
        current_visual_preset = HasAnyCustomActive() and 3 or 0
        visual_bench_timer = 12.0
        SyncToNativePlugin(true)
        hud_dirty_now = true
    end,
    SaveCustomSlot = function(slot_idx)
        local ok = SaveUserCustomSlot(slot_idx)
        if ok then
            ExportJsonPresetLibrary()
            local ui_zh = ResolveUiIsZh()
            toast_msg = ui_zh and ("已保存方案至自存槽 " .. tostring(slot_idx)) or ("Saved Look to Slot " .. tostring(slot_idx))
            toast_timer = 2.5
            hud_dirty_now = true
            if type(HS_PanelNotifySync) == "function" then HS_PanelNotifySync() end
        end
        return ok
    end,
    LoadCustomSlot = function(slot_idx)
        local ok = LoadUserCustomSlot(slot_idx)
        if ok then
            local ui_zh = ResolveUiIsZh()
            toast_msg = ui_zh and ("已载入自存槽 " .. tostring(slot_idx)) or ("Loaded Custom Slot " .. tostring(slot_idx))
            toast_timer = 2.5
            hud_dirty_now = true
        end
        return ok
    end,
    PreviewShareCode = function(scheme_name)
        return EncodeCurrentShareCode(scheme_name)
    end,
    ExportShareCode = function(scheme_name)
        local code, diffs, cname, base_id = EncodeCurrentShareCode(scheme_name)
        if #cname > 0 then current_scheme_name = cname end
        local copied = TryWriteClipboard(code)
        return code, copied, diffs, cname, base_id
    end,
    ValidateShareCode = function(raw_str)
        return ValidateSharePayload(raw_str)
    end,
    ImportShareCode = function(raw_str)
        local ok, err, staged = ImportSharePayloadTransactional(raw_str)
        if ok then
            local ui_zh = ResolveUiIsZh()
            local lbl = (staged and #staged.name > 0) and ("：" .. staged.name) or ""
            toast_msg = ui_zh and ("已校验并导入方案" .. lbl) or ("Imported visual preset" .. lbl)
            toast_timer = 3.0
        end
        return ok, err, staged
    end,
    UndoLastImport = function()
        local ok = UndoLastShareImport()
        if ok then
            local ui_zh = ResolveUiIsZh()
            toast_msg = ui_zh and "已撤销导入，恢复原配置" or "Reverted to previous config"
            toast_timer = 2.5
        end
        return ok
    end,
    ExportJsonLibrary = function()
        local payload, cnt = ExportJsonPresetLibrary()
        local ui_zh = ResolveUiIsZh()
        toast_msg = ui_zh and ("已导出 JSON 方案库 (" .. tostring(cnt) .. "套)") or ("Exported JSON library (" .. tostring(cnt) .. ")")
        toast_timer = 2.5
        hud_dirty_now = true
        return payload, cnt
    end,
    ReloadJsonLibrary = function(cb)
        return ReloadJsonPresetLibrary(cb)
    end,
    ReadClipboard = TryReadClipboard,
    SetClipboard = TryWriteClipboard,
    GetExtSnapshot = function()
        if type(HS_ExtRuntime) == "table" and type(HS_ExtRuntime.GetSnapshot) == "function" then
            return HS_ExtRuntime.GetSnapshot()
        end
        return { abi_version = 1, master_bypass = false, total_count = 0, active_count = 0, fault_count = 0, total_gpu_ms = 0, plugins = {} }
    end,
    SetExtPluginEnabled = function(id, enabled)
        if type(HS_ExtRuntime) == "table" then
            return HS_ExtRuntime.SetPluginEnabled(id, enabled)
        end
        return false, "ext runtime unavailable"
    end,
    SetExtPluginParam = function(id, key, val)
        if type(HS_ExtRuntime) == "table" then
            return HS_ExtRuntime.SetPluginParam(id, key, val)
        end
        return false, "ext runtime unavailable"
    end,
    ResetExtPlugin = function(id)
        if type(HS_ExtRuntime) == "table" then
            return HS_ExtRuntime.ResetPlugin(id)
        end
        return false, "ext runtime unavailable"
    end,
    SetExtMasterBypass = function(bypass)
        if type(HS_ExtRuntime) == "table" then
            return HS_ExtRuntime.SetMasterBypass(bypass)
        end
        return false
    end,
    ResetTune = function()
        SnapshotFlagsToCustom()
        for tk, b in pairs(TUNE_BOUNDS) do
            vis_tune[tk] = b.default
        end
        custom_vis_flags.tone = HasAnyGradingTune(vis_tune)
        custom_vis_flags.clean_filter = HasAnyCleanFilterTune(vis_tune)
        current_visual_preset = HasAnyCustomActive() and 3 or 0
        SyncToNativePlugin(true)
        hud_dirty_now = true
    end,
    ToggleSkeletal = function()
        local ui_zh = ResolveUiIsZh()
        current_skeletal_tween = not current_skeletal_tween
        SyncToNativePlugin(true)
        toast_msg = ui_zh
            and ("240帧补帧 -> " .. (current_skeletal_tween and "开" or "关"))
            or ("240Hz Smooth -> " .. (current_skeletal_tween and "ON" or "OFF"))
        toast_timer = 2.5
        hud_dirty_now = true
    end,
    ToggleLowLatency = function()
        local ui_zh = ResolveUiIsZh()
        current_low_latency = not current_low_latency
        SyncToNativePlugin(true)
        toast_msg = ui_zh
            and ("低延迟模式 -> " .. (current_low_latency and "开" or "关"))
            or ("Low Latency -> " .. (current_low_latency and "ON" or "OFF"))
        toast_timer = 2.5
        hud_dirty_now = true
    end,
    CycleHudMode = CycleHudStateWithToast,
    GetSnapshot = function()
        local has_dll = HasNativePlugin()
        local avg_fps, low1_fps = last_hud_avg_fps, last_hud_low1_fps
        local busy_ms, vram_mb = 0, 0
        local ex_act, cur_lat, orig_lat = false, 3, 3
        local vfx_ok, light_ok, lm_scale = false, false, 0.5
        local render_backend = "D3D9"
        local vb_hit_pct, vb_kb = 0.0, 0.0
        local net_connected, ping_avg, ping_last, loss_cur, rpc_imm, rpc_off_ch0 = false, -1, -1, 0.0, 0, 0
        local async_save_active, async_save_ok, async_save_comp = false, 0, 0

        local fnStats = rawget(GLOBAL, "HyperSmooth_GetFrameStats")
        if type(fnStats) == "function" then
            local ok, a, l1 = pcall(fnStats)
            if ok and type(a) == "number" and a > 0 then
                avg_fps = a
                low1_fps = (type(l1) == "number" and l1 > 0) and l1 or a
            end
        end
        local fnPerf = rawget(GLOBAL, "HyperSmooth_GetVisualPerf")
        if type(fnPerf) == "function" then
            local ok, _, _, _, _, _, bMs, _, vCur = pcall(fnPerf)
            if ok then
                busy_ms = type(bMs) == "number" and bMs or 0
                vram_mb = type(vCur) == "number" and vCur or 0
            end
        end
        local fnVis = rawget(GLOBAL, "HyperSmooth_GetVisualStats")
        if type(fnVis) == "function" then
            local ok, _, _, _, _, lms = pcall(fnVis)
            if ok and type(lms) == "number" then
                lm_scale = lms
            end
        end
        local fnLat = rawget(GLOBAL, "HyperSmooth_GetLatencyStats")
        if type(fnLat) == "function" then
            local ok, ea, cl, ol, _, _, vo, _, _, _, _, lo = pcall(fnLat)
            if ok then
                ex_act   = ea == true
                cur_lat  = type(cl) == "number" and cl or 3
                orig_lat = type(ol) == "number" and ol or 3
                vfx_ok   = vo == true
                light_ok = lo == true
            end
        end
        local fnBe = rawget(GLOBAL, "HyperSmooth_GetRenderBackend")
        if type(fnBe) == "function" then
            local okB, rawBe = pcall(fnBe)
            if okB then
                render_backend = IsRenderBackendOpenGL(rawBe) and "GL" or "D3D9"
            end
        end
        local fnVb = rawget(GLOBAL, "HyperSmooth_GetVbPoolStats")
        if type(fnVb) == "function" then
            local okV, vh, vm, _, _, _, vkb = pcall(fnVb)
            if okV then
                local tot = (vh or 0) + (vm or 0)
                vb_hit_pct = (tot > 0) and (100.0 * (vh or 0) / tot) or 0.0
                vb_kb = type(vkb) == "number" and vkb or 0.0
            end
        end
        local fnNet = rawget(GLOBAL, "HyperSmooth_GetNetStats")
        if type(fnNet) == "function" then
            local okN, nConn, pAvg, pLast, lCur, _, _, _, _, rImm, rOff = pcall(fnNet)
            if okN then
                net_connected = nConn == true
                ping_avg      = type(pAvg) == "number" and pAvg or -1
                ping_last     = type(pLast) == "number" and pLast or -1
                loss_cur      = type(lCur) == "number" and lCur or 0.0
                rpc_imm       = type(rImm) == "number" and rImm or 0
                rpc_off_ch0   = type(rOff) == "number" and rOff or 0
            end
        end
        local fnSav = rawget(GLOBAL, "HyperSmooth_GetAsyncSaveStats")
        if type(fnSav) == "function" then
            local okS, sAct, _, sOk, _, sComp = pcall(fnSav)
            if okS then
                async_save_active = sAct == true
                async_save_ok     = type(sOk) == "number" and sOk or 0
                async_save_comp   = type(sComp) == "number" and sComp or 0
            end
        end

        local dll_status, dll_ver, expected_dll_ver = GetDllVersionStatus()

        return {
            preset            = current_visual_preset,
            flags             = GetActiveVisualFlags(),
            tune              = GetActiveVisTune(),
            bcas_coexist      = IsExternalBcasActive(),
            has_world_studio  = HasNativeWorldStudio(),
            slot_saved        = {
                [1] = custom_slots[1] ~= nil,
                [2] = custom_slots[2] ~= nil,
                [3] = custom_slots[3] ~= nil,
            },
            scheme_name       = current_scheme_name,
            has_undo          = last_import_undo ~= nil,
            has_clip_write    = type(rawget(GLOBAL, "HyperSmooth_SetClipboard")) == "function",
            json_lib_count    = #json_named_library,
            skeletal          = current_skeletal_tween,
            low_latency       = current_low_latency,
            hud_show          = current_show_fps_hud,
            hud_mode          = current_hud_mode,
            mod_lang          = current_mod_lang,
            ui_zh             = ResolveUiIsZh(),
            has_dll           = has_dll,
            dll_status        = dll_status,
            dll_ver           = dll_ver,
            expected_dll_ver  = expected_dll_ver,
            avg_fps           = avg_fps,
            low1_fps          = low1_fps,
            busy_ms           = busy_ms,
            vram_mb           = vram_mb,
            d3d9ex_active     = ex_act,
            cur_lat           = cur_lat,
            orig_lat          = orig_lat,
            vfx_ok            = vfx_ok,
            light_ok          = light_ok,
            lm_scale          = lm_scale,
            render_backend    = render_backend,
            vb_hit_pct        = vb_hit_pct,
            vb_kb             = vb_kb,
            net_connected     = net_connected,
            ping_avg          = ping_avg,
            ping_last         = ping_last,
            loss_cur          = loss_cur,
            rpc_imm           = rpc_imm,
            rpc_off_ch0       = rpc_off_ch0,
            async_save_active = async_save_active,
            async_save_ok     = async_save_ok,
            async_save_comp   = async_save_comp,
            flick_rescues     = latency_stats.flick_rescues,
            map_skip          = latency_stats.map_skip,
            placer_skip       = latency_stats.placer_skip,
            deploy_skip       = latency_stats.deploy_skip,
        }
    end,
}

rawset(GLOBAL, "c_hshud", function(arg1, arg2)
    for _, a in ipairs({ arg1, arg2 }) do
        if type(a) == "string" then
            local s = a:lower()
            if s == "zh" or s == "cn" then
                current_mod_lang = "zh"
            elseif s == "en" or s == "eng" then
                current_mod_lang = "en"
            elseif s == "auto" then
                current_mod_lang = "auto"
            elseif s == "simple" or s == "compact" or s == "min" then
                current_hud_mode = "simple"
                current_show_fps_hud = true
            elseif s == "detail" or s == "detailed" or s == "full" then
                current_hud_mode = "detail"
                current_show_fps_hud = true
            elseif s == "on" or s == "show" then
                current_show_fps_hud = true
            elseif s == "off" or s == "hide" then
                current_show_fps_hud = false
            end
        elseif type(a) == "boolean" then
            current_show_fps_hud = a
        end
    end
    if arg1 == nil and arg2 == nil then
        current_show_fps_hud = true
        current_hud_mode = (current_hud_mode == "detail") and "simple" or "detail"
    end
    SyncToNativePlugin(true)
    hud_dirty_now = true
    local ui_zh = ResolveUiIsZh()
    local mode_lbl = not current_show_fps_hud
        and (ui_zh and "已隐藏" or "Hidden")
        or (current_hud_mode == "detail" and (ui_zh and "详细模式" or "Detailed") or (ui_zh and "简洁模式" or "Compact"))
    local lang_lbl = current_mod_lang == "zh" and "简体中文"
        or (current_mod_lang == "en" and "English" or (ui_zh and "跟随游戏(中文)" or "Auto(EN)"))
    if GLOBAL.print then
        GLOBAL.print(ui_zh
            and string.format("[HyperSmooth] HUD=%s | 语言=%s", mode_lbl, lang_lbl)
            or string.format("[HyperSmooth] HUD=%s | Lang=%s", mode_lbl, lang_lbl))
    end
end)

local active_key_handlers = {}

function HS_ApplyKeyBind(action_name, keycode)
    local prev = active_key_handlers[action_name]
    if prev and prev.Remove then
        prev:Remove()
    end
    active_key_handlers[action_name] = nil

    local action_fn = HOTKEY_ACTIONS[action_name]
    if type(action_fn) ~= "function" or type(keycode) ~= "number" or not TheInput then
        return
    end
    if keycode >= 1000 then
        if TheInput.AddMouseButtonHandler then
            active_key_handlers[action_name] = TheInput:AddMouseButtonHandler(function(btn, down)
                if btn == keycode and down and CanTriggerInGameHotkey() then
                    action_fn()
                end
            end)
        end
    else
        if TheInput.AddKeyUpHandler then
            active_key_handlers[action_name] = TheInput:AddKeyUpHandler(keycode, function()
                if CanTriggerInGameHotkey() then
                    action_fn()
                end
            end)
        end
    end
end

--------------------------------------------------------------------------------
-- OptionsScreen Lazy Controls (cuts ~1,240 C++ UI Entities from open/close!)
-- & Honest 1% Low Transition Guard (holds ring only on Pause/Fade/Modal Menu;
-- 100% raw & unfiltered during live HUD gameplay including T-key & combat!)
--------------------------------------------------------------------------------
local function InstallOptionsScreenLazyControls()
    local ok_opt, OptionsScreen = pcall(GLOBAL.require, "screens/redux/optionsscreen")
    if not (ok_opt and type(OptionsScreen) == "table") or OptionsScreen._hs_lazy_ctrl_patched then
        return
    end
    if type(OptionsScreen._CollectKeyboardControlWidgets) ~= "function" or
       type(OptionsScreen._CollectControllerControlWidgets) ~= "function" then
        return
    end
    OptionsScreen._hs_lazy_ctrl_patched = true

    local orig_collect_kb     = OptionsScreen._CollectKeyboardControlWidgets
    local orig_collect_ctrl   = OptionsScreen._CollectControllerControlWidgets
    local orig_update_menu    = OptionsScreen.UpdateMenu
    local orig_refresh_ctrl   = OptionsScreen.RefreshControls
    local orig_load_default   = OptionsScreen.LoadDefaultControls
    local orig_on_mapped      = OptionsScreen.OnControlMapped
    local orig_rebuild_ctrl   = OptionsScreen._RebuildControllerControls
    local orig_become_active  = OptionsScreen.OnBecomeActive

    local function EnsureKbControls(scr)
        if not scr or not scr._hs_kb_lazy or not scr.kb_controllist then
            return false
        end
        scr._hs_kb_lazy = false
        local list = scr.kb_controllist
        local mod_items = {}
        if type(list.items) == "table" then
            for i = 1, #list.items do
                mod_items[i] = list.items[i]
            end
        end
        scr._hs_force_kb = true
        local ok_b = pcall(orig_collect_kb, scr)
        scr._hs_force_kb = false
        if not ok_b or type(scr.kb_controlwidgets) ~= "table" then
            scr.kb_controlwidgets = scr.kb_controlwidgets or {}
            return false
        end
        local merged = {}
        for i = 1, #scr.kb_controlwidgets do
            local w = scr.kb_controlwidgets[i]
            if w then
                list:AddChild(w)
                merged[#merged + 1] = w
            end
        end
        for i = 1, #mod_items do
            merged[#merged + 1] = mod_items[i]
        end
        if list.scroll_bar_container and list.scroll_bar_container.MoveToFront then
            list.scroll_bar_container:MoveToFront()
        end
        list:SetList(merged, true)
        return true
    end

    local function EnsureControllerControls(scr)
        if not scr or not scr._hs_ctrl_lazy or not scr.controller_controllist then
            return false
        end
        scr._hs_ctrl_lazy = false
        local list = scr.controller_controllist
        local mod_items = {}
        if type(list.items) == "table" then
            for i = 1, #list.items do
                mod_items[i] = list.items[i]
            end
        end
        scr._hs_force_ctrl = true
        local ok_b = pcall(orig_collect_ctrl, scr)
        scr._hs_force_ctrl = false
        if not ok_b or type(scr.controller_controlwidgets) ~= "table" then
            scr.controller_controlwidgets = scr.controller_controlwidgets or {}
            return false
        end
        local merged = {}
        for i = 1, #scr.controller_controlwidgets do
            local w = scr.controller_controlwidgets[i]
            if w then
                list:AddChild(w)
                merged[#merged + 1] = w
            end
        end
        for i = 1, #mod_items do
            merged[#merged + 1] = mod_items[i]
        end
        if list.scroll_bar_container and list.scroll_bar_container.MoveToFront then
            list.scroll_bar_container:MoveToFront()
        end
        list:SetList(merged, true)
        return true
    end

    local function EnsureActiveControls(scr)
        if not scr then return false end
        local dev_id = (scr.deviceSpinner and scr.deviceSpinner.GetSelectedData)
            and scr.deviceSpinner:GetSelectedData() or 0
        if dev_id ~= nil and dev_id ~= 0 then
            return EnsureControllerControls(scr)
        end
        return EnsureKbControls(scr)
    end

    OptionsScreen._CollectKeyboardControlWidgets = function(scr, ...)
        if not scr._hs_force_kb then
            scr.kb_controlwidgets = {}
            scr._hs_kb_lazy = true
            return
        end
        return orig_collect_kb(scr, ...)
    end

    OptionsScreen._CollectControllerControlWidgets = function(scr, ...)
        if not scr._hs_force_ctrl then
            scr.controller_controlwidgets = {}
            scr._hs_ctrl_lazy = true
            return
        end
        return orig_collect_ctrl(scr, ...)
    end

    if type(orig_update_menu) == "function" then
        OptionsScreen.UpdateMenu = function(scr, ...)
            if scr and scr.selected_tab == "controls" and EnsureActiveControls(scr) then
                if type(scr.RefreshControls) == "function" then
                    scr:RefreshControls()
                    return
                end
            end
            return orig_update_menu(scr, ...)
        end
    end

    if type(orig_refresh_ctrl) == "function" then
        OptionsScreen.RefreshControls = function(scr, ...)
            if scr and scr.selected_tab == "controls" then
                EnsureActiveControls(scr)
            end
            local r1, r2 = orig_refresh_ctrl(scr, ...)
            if scr and (scr._hs_kb_lazy or scr._hs_ctrl_lazy) and
               scr.integratedbackpackSpinner and scr.deviceSpinner and
               scr.deviceSpinner.GetSelectedData then
                local dev_id = scr.deviceSpinner:GetSelectedData()
                if dev_id == 0 then
                    scr.integratedbackpackSpinner:Enable()
                    if scr.integratedbackpackSpinner.selectedIndex then
                        scr.integratedbackpackSpinner:SetSelectedIndex(scr.integratedbackpackSpinner.selectedIndex)
                    end
                else
                    scr.integratedbackpackSpinner:Disable()
                    local opt_s = GLOBAL.STRINGS and GLOBAL.STRINGS.UI and GLOBAL.STRINGS.UI.OPTIONS
                    if opt_s and opt_s.INTEGRATEDBACKPACK_ENABLED then
                        scr.integratedbackpackSpinner:UpdateText(opt_s.INTEGRATEDBACKPACK_ENABLED)
                    end
                end
            end
            return r1, r2
        end
    end

    if type(orig_load_default) == "function" then
        OptionsScreen.LoadDefaultControls = function(scr, ...)
            EnsureKbControls(scr)
            EnsureActiveControls(scr)
            return orig_load_default(scr, ...)
        end
    end

    if type(orig_on_mapped) == "function" then
        OptionsScreen.OnControlMapped = function(scr, ...)
            EnsureActiveControls(scr)
            return orig_on_mapped(scr, ...)
        end
    end

    if type(orig_rebuild_ctrl) == "function" then
        OptionsScreen._RebuildControllerControls = function(scr, ...)
            scr._hs_ctrl_lazy = false
            scr._hs_force_ctrl = true
            local ok_r, r1, r2 = pcall(orig_rebuild_ctrl, scr, ...)
            scr._hs_force_ctrl = false
            if not ok_r then error(r1, 0) end
            return r1, r2
        end
    end

    if type(orig_become_active) == "function" then
        OptionsScreen.OnBecomeActive = function(scr, ...)
            if scr and scr.default_section == "CONTROLSCHEME" then
                EnsureControllerControls(scr)
            end
            return orig_become_active(scr, ...)
        end
    end
end

local function InstallStatsTransitionGuard()
    local fe = rawget(GLOBAL, "TheFrontEnd")
    if not (fe and type(fe.Update) == "function") or fe._hs_stats_hold_hooked then
        return
    end
    fe._hs_stats_hold_hooked = true
    local last_hold = nil
    local function SyncHold(self_fe)
        local fnHold = rawget(GLOBAL, "HyperSmooth_SetStatsHold")
        if type(fnHold) ~= "function" then return end
        local scr = self_fe.GetActiveScreen and self_fe:GetActiveScreen() or nil
        if not scr then return end
        local is_hud = (scr.name == "HUD")
        local is_fading = (self_fe.fadedir ~= nil)
        local fnPaused = rawget(GLOBAL, "IsPaused")
        local is_paused = (type(fnPaused) == "function" and fnPaused() == true)
        local want_hold = not (is_hud and not is_fading and not is_paused)
        if want_hold ~= last_hold then
            last_hold = want_hold
            pcall(fnHold, want_hold)
        end
    end

    local orig_update = fe.Update
    fe.Update = function(self_fe, dt, ...)
        SyncHold(self_fe)
        local r1, r2, r3 = orig_update(self_fe, dt, ...)
        SyncHold(self_fe)
        return r1, r2, r3
    end
    if type(fe.Fade) == "function" then
        local orig_fade = fe.Fade
        fe.Fade = function(self_fe, ...)
            local r1, r2, r3 = orig_fade(self_fe, ...)
            SyncHold(self_fe)
            return r1, r2, r3
        end
    end
    if type(fe.PushScreen) == "function" then
        local orig_push = fe.PushScreen
        fe.PushScreen = function(self_fe, ...)
            local r1, r2, r3 = orig_push(self_fe, ...)
            SyncHold(self_fe)
            return r1, r2, r3
        end
    end
    if type(fe.PopScreen) == "function" then
        local orig_pop = fe.PopScreen
        fe.PopScreen = function(self_fe, ...)
            local r1, r2, r3 = orig_pop(self_fe, ...)
            SyncHold(self_fe)
            return r1, r2, r3
        end
    end
end

if type(AddGamePostInit) == "function" then
    AddGamePostInit(function()
        pcall(InstallOptionsScreenLazyControls)
        pcall(InstallStatsTransitionGuard)
    end)
end

if type(modimport) == "function" then
    pcall(modimport, "keybind")
    pcall(modimport, "hs_panel")
end
