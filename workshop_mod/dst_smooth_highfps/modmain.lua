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

-- Player-First: 9 independent per-effect toggles (ALL default OFF -> 100% Vanilla Pure out of the box!)
local CFG_VIS_AF16       = GetModConfigData("visual_af16") == true
local CFG_VIS_LIGHTMAP   = GetModConfigData("visual_full_lightmap") == true
local CFG_VIS_CAS        = GetModConfigData("visual_cas") == true
local CFG_VIS_AO         = GetModConfigData("visual_contact_ao") == true
local CFG_VIS_ENV        = GetModConfigData("visual_calm_env") == true
local CFG_VIS_SOFT_BLOOM = GetModConfigData("visual_soft_bloom") == true
local CFG_VIS_TONE       = GetModConfigData("visual_storybook_tone") == true
local CFG_VIS_MIP_SHARP  = GetModConfigData("visual_mip_sharp") == true
local CFG_VIS_DARK       = GetModConfigData("visual_dark_clarity") == true
local CFG_VIS_MASK = (CFG_VIS_AF16 and 1 or 0)
    + (CFG_VIS_LIGHTMAP and 2 or 0)
    + (CFG_VIS_CAS and 4 or 0)
    + (CFG_VIS_AO and 8 or 0)
    + (CFG_VIS_ENV and 16 or 0)
    + (CFG_VIS_SOFT_BLOOM and 32 or 0)
    + (CFG_VIS_TONE and 64 or 0)
    + (CFG_VIS_MIP_SHARP and 128 or 0)
    + (CFG_VIS_DARK and 256 or 0)
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
}
local current_skeletal_tween = SKELETAL_TWEEN and true or false
local current_low_latency = LOW_LATENCY_INPUT and true or false
local current_show_fps_hud = SHOW_FPS_HUD and true or false
local fps_hud_text = nil
local hud_dirty_now = false
local visual_bench_timer = 0
local visual_bench_pinned = false

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
}
local last_hud_avg_fps  = 0
local last_hud_low1_fps = 0

local visual_shaders_ready = false
local user_bloom_enabled = false
local orig_SetBloomEnabled = nil
local hd2d_smooth_wetness = 0.0

-- Player-First Presets (0 = 100% Vanilla Pure default; 1 = Eye-Care HD; 2 = Soft Storybook; 3 = Custom)
-- Note: dark_clarity stays false in ALL presets unless explicitly opted in by the player!
local VISUAL_PRESET_FLAGS = {
    [0] = { af16 = false, full_lm = false, cas = false, ao = false, env = false, soft_bloom = false, tone = false, mip_sharp = false, dark_clarity = false },
    [1] = { af16 = true,  full_lm = true,  cas = true,  ao = true,  env = false, soft_bloom = true,  tone = false, mip_sharp = true,  dark_clarity = false },
    [2] = { af16 = true,  full_lm = true,  cas = true,  ao = true,  env = true,  soft_bloom = true,  tone = true,  mip_sharp = true,  dark_clarity = false },
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
end

-- Zero-allocation independent World/ColourCube/Lighting Shader uniform sync:
--   params[1] = contactAoStr   (ANIM only: gentle base Contact AO + white-glare soft shoulder)
--   params[2] = calmEnvStr     (OCEAN_COMB + GROUND only: whisper coastal blend + matte rain darkening)
--   params[3] = darkClarityStr (COLOURCUBE only: hue-preserving crushed dark-toe clarity + 31.5 LUT fix)
--   sun[3]    = lightDitherStr (LIGHTING only: 0-tap IGN anti-banding on campfire/torch 8-bit halos)
-- When all four are 0.0 -> bit-for-bit identical vanilla fragment output!
local function PushHd2dEnvToNative(dt_step)
    local fnHd2d = rawget(GLOBAL, "HyperSmooth_SetHd2d")
    if type(fnHd2d) ~= "function" then
        return
    end
    local fl = GetActiveVisualFlags()
    local ao_str       = fl.ao  and (fl.tone and 1.10 or 1.0) or 0.0
    local env_str      = fl.env and 1.0 or 0.0
    local dark_str     = fl.dark_clarity and 1.0 or 0.0
    local light_dither = (fl.full_lm or fl.cas) and 1.0 or 0.0
    if ao_str <= 0.001 and env_str <= 0.001 and dark_str <= 0.001 and light_dither <= 0.001 then
        pcall(fnHd2d, 0.0, 0.0, 0.0, 0.70, 0.707, 0.707, 0.0, -1.0, 1.0, 0.94, 0.82, 0.0)
        return
    end

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
    local step = (type(dt_step) == "number" and dt_step > 0) and math.min(1.0, dt_step * 0.45) or 1.0
    hd2d_smooth_wetness = hd2d_smooth_wetness + (target_wet - hd2d_smooth_wetness) * step

    pcall(fnHd2d, ao_str, env_str, dark_str, 0.70, 0.707, 0.707, light_dither, -1.0, 1.0, 0.94, 0.82, hd2d_smooth_wetness)
end

local function ApplyVisualPostProcess()
    local pp = rawget(GLOBAL, "PostProcessor")
    local ppe = rawget(GLOBAL, "PostProcessorEffects")
    local uv = rawget(GLOBAL, "UniformVariables")
    if not (visual_shaders_ready and pp and ppe and uv) then
        return
    end
    local fl = GetActiveVisualFlags()

    local bloom_str   = fl.tone and 0.82 or 0.68
    local bloom_knee  = fl.tone and 0.50 or 0.55
    local bloom_chr   = fl.tone and 0.14 or 0.10
    local bloom_prot  = 0.85 -- strong highlight protection: keeps white faces & marble crisp even if Bloom ON
    local cas_val     = fl.cas  and (fl.tone and 0.28 or 0.24) or 0.0
    local grade_val   = fl.tone and 0.35 or 0.0
    local dither_val  = fl.cas  and 1.5  or 0.0
    local vig_val     = fl.tone and 0.06 or 0.0
    local use_final   = fl.cas or fl.tone

    if uv.HS_BLOOM_PARAMS then
        pcall(pp.SetUniformVariable, pp, uv.HS_BLOOM_PARAMS, bloom_str, bloom_knee, bloom_chr, bloom_prot)
    end
    if uv.HS_VISUAL_PARAMS then
        pcall(pp.SetUniformVariable, pp, uv.HS_VISUAL_PARAMS, cas_val, grade_val, dither_val, vig_val)
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

local function HasNativePlugin()
    return type(rawget(GLOBAL, "HyperSmooth_SetFps")) == "function"
end

local EXPECTED_DLL_VERSION = (modinfo and modinfo.version) or "2.6.0"

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
        return "outdated", "<2.6.0", EXPECTED_DLL_VERSION
    end
    local ok, raw_ver = pcall(fnVer)
    local dll_num, dll_clean = ParseSemVer(ok and raw_ver or nil)
    local exp_num = ParseSemVer(EXPECTED_DLL_VERSION) or 20600
    if not dll_num then
        return "outdated", "unknown", EXPECTED_DLL_VERSION
    end
    if dll_num < exp_num then
        return "outdated", dll_clean, EXPECTED_DLL_VERSION
    end
    return "ok", dll_clean, EXPECTED_DLL_VERSION
end

GLOBAL.HyperSmooth_GetDllStatus = GetDllVersionStatus

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

local function SyncToNativePlugin(save_persist)
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
        pcall(fnVis, fl.af16 and 16 or 1, fl.full_lm, fl.mip_sharp and -0.45 or 0.0)
    end
    PushHd2dEnvToNative(0)
    ApplyVisualPostProcess()
    ApplyHudVisibility()
    if type(HS_PanelNotifySync) == "function" then
        HS_PanelNotifySync()
    end
    -- Persist user choices (and modinfo fingerprints mf/ms/mh/mvp/mvm/mll so explicit modinfo changes win cleanly)
    if save_persist ~= false and TheSim and TheSim.SetPersistentString then
        local payload = string.format("fps=%d;skeletal=%d;hud=%d;vp=%d;vm=%d;ll=%d;mf=%d;ms=%d;mh=%d;mvp=%d;mvm=%d;mll=%d",
            current_target_fps,
            current_skeletal_tween and 1 or 0,
            current_show_fps_hud and 1 or 0,
            current_visual_preset,
            EncodeCustomVisMask(custom_vis_flags),
            current_low_latency and 1 or 0,
            TARGET_FPS,
            SKELETAL_TWEEN and 1 or 0,
            SHOW_FPS_HUD and 1 or 0,
            VISUAL_PRESET,
            CFG_VIS_MASK,
            LOW_LATENCY_INPUT and 1 or 0)
        TheSim:SetPersistentString(CFG_FILENAME, payload, false)
    end
end

-- Restore last in-game OptionsScreen choices unless user explicitly changed modinfo.lua config
if TheSim and TheSim.GetPersistentString then
    TheSim:GetPersistentString(CFG_FILENAME, function(load_success, str)
        if load_success and type(str) == "string" and #str > 0 then
            local sf = tonumber(str:match("fps=(%d+)"))
            local ss = tonumber(str:match("skeletal=(%d+)"))
            local sh = tonumber(str:match("hud=(%d+)"))
            local svp = tonumber(str:match("vp=(%d+)"))
            local svm = tonumber(str:match("vm=(%d+)"))
            local sll = tonumber(str:match("ll=(%d+)"))
            local mf = tonumber(str:match("mf=(%d+)"))
            local ms = tonumber(str:match("ms=(%d+)"))
            local mh = tonumber(str:match("mh=(%d+)"))
            local mvp = tonumber(str:match("mvp=(%d+)"))
            local mvm = tonumber(str:match("mvm=(%d+)"))
            local mll = tonumber(str:match("mll=(%d+)"))
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
            if svp ~= nil and svp >= 0 and svp <= 3 and mvp == VISUAL_PRESET and mvm == CFG_VIS_MASK then
                current_visual_preset = svp
                if svm ~= nil then
                    DecodeCustomVisMask(svm)
                end
            end
        end
        SyncToNativePlugin(true)
    end)
else
    SyncToNativePlugin(true)
end

--------------------------------------------------------------------------------
-- 0. Player-First Opt-In Visual Fidelity Pipeline (v2.5.1-remaster)
--    - Default state is 100% Vanilla Pure (ZERO forced post-processing, ZERO visual burden!)
--    - Every effect is independently toggleable (Mod Config or live c_hsopt)
--    - Disabling restores 100% vanilla rendering cleanly
--------------------------------------------------------------------------------
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

            -- Player-First: only upgrade BloomSampler to 0.5x when user explicitly enabled Soft Bloom!
            local init_fl = GetActiveVisualFlags()
            if init_fl.soft_bloom then
                pp:SetBloomSamplerParams(SS.Relative, 0.5, 0.5, SCM.RGB)
            end

            uv.HS_BLOOM_PARAMS  = pp:AddUniformVariable("HS_BLOOM_PARAMS", 4)
            uv.HS_VISUAL_PARAMS = pp:AddUniformVariable("HS_VISUAL_PARAMS", 4)

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
            pp:AddSampler(ppe.HS_Bloom, SEB.Shader, se.HS_KawaseUp)
            pp:SetEffectUniformVariables(ppe.HS_Bloom, uv.HS_BLOOM_PARAMS)

            ppe.HS_VisualFinal = pp:AddPostProcessEffect(sh_final)
            pp:SetEffectUniformVariables(ppe.HS_VisualFinal, uv.HS_VISUAL_PARAMS)

            visual_shaders_ready = true
        end)

        if not ok_build then
            visual_shaders_ready = false
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
                    end
                end
                idx.SetBloomEnabled = function(self, enabled)
                    user_bloom_enabled = enabled and true or false
                    if orig_SetBloomEnabled then
                        orig_SetBloomEnabled(self, user_bloom_enabled)
                    end
                    ApplyVisualPostProcess()
                end
            end

            ApplyVisualPostProcess()
        end)
    end)
end

-- Diagnostic & live per-effect console helpers:
--   c_hsvisual()                  - print active toggles + live GPU/VRAM telemetry
--   c_hslatency()                 - print D3D9Ex queue latency + 240Hz VFX + flick-click rescue stats
--   c_hsopt("af16"|"lightmap"|"mip"|"cas"|"ao"|"env"|"bloom"|"tone"|"dark"|"off", [true|false])
--   c_hsbench([true|false])       - pin/unpin compact HUD benchmark strip
local function PrintVisualReport()
    local names = { [0] = "Vanilla(0)", [1] = "EyeCareHD(1)", [2] = "SoftStorybook(2)", [3] = "Custom(3)" }
    local fl = GetActiveVisualFlags()
    local fnV = rawget(GLOBAL, "HyperSmooth_GetVisualStats")
    local fnP = rawget(GLOBAL, "HyperSmooth_GetVisualPerf")
    local line1 = string.format(
        "[HyperSmooth Visual] mode=%s | flags(af16=%s lm=%s mip=%s cas=%s ao=%s env=%s softBloom=%s tone=%s dark=%s) | gameBloom=%s",
        names[current_visual_preset] or tostring(current_visual_preset),
        tostring(fl.af16), tostring(fl.full_lm), tostring(fl.mip_sharp), tostring(fl.cas), tostring(fl.ao),
        tostring(fl.env), tostring(fl.soft_bloom), tostring(fl.tone), tostring(fl.dark_clarity), tostring(user_bloom_enabled))
    if type(fnV) == "function" then
        local ok, af, hw, hits, lmp, lms, mipOk, mipVal, mipHits = pcall(fnV)
        if ok then
            line1 = line1 .. string.format(" | AF=%dx (hw=%.0fx hits=%d) LM=%.1fx MipBias=%.2f(ok=%s hits=%d)",
                af or 1, hw or 1, hits or 0, lms or 0.5, mipVal or 0.0, tostring(mipOk == true), mipHits or 0)
        end
    end
    if type(fnP) == "function" then
        local ok, hdAct, progs, mStr, wMs, pMs, bMs, gMs, vCur, vPk, vBud = pcall(fnP)
        if ok then
            GLOBAL.print(line1)
            GLOBAL.print(string.format(
                "[HyperSmooth Perf] world_progs=%d (active_str=%.2f) | Busy=%.2fms (World=%.2fms Post=%.2fms GpuDrain=%.2fms) | VRAM=%.0fMB (Peak=%.0fMB / Budget=%.0fMB)",
                progs or 0, mStr or 0,
                bMs or 0, wMs or 0, pMs or 0, gMs or 0, vCur or 0, vPk or 0, vBud or 0))
            return
        end
    end
    GLOBAL.print(line1 .. " (no dinput8.dll bridge)")
end

local function PrintLatencyReport()
    local fnL = rawget(GLOBAL, "HyperSmooth_GetLatencyStats")
    local fnH = rawget(GLOBAL, "HyperSmooth_GetHostPerf")
    local luaLine = string.format(
        "[HyperSmooth Latency] LowLat=%s | Lua(WASD_instant=%d, WalkCancel=%d, FlickRefresh=%d, FlickRescued=%d, ModClickRescued=%d, Cam240=%d, FollowText240=%d) | CraftZeroStall(rebuilds=%d hasSaved=%d uiDeferred=%d)",
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
        latency_stats.craft_ui_deferred)
    if type(fnL) == "function" then
        local ok, exAct, curLat, origLat, gpuMs, gpuDrains, vfxOk, vfxCmds, vfxPts, mipOk, mipVal, lightOk, lightPts, lmSwapOk = pcall(fnL)
        if ok then
            GLOBAL.print(luaLine)
            GLOBAL.print(string.format(
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
                    GLOBAL.print(string.format(
                        "[HyperSmooth Host] FastDump(%s): calls=%d (%.1fMB %.2fms fb=%d) | Compress2(%s): calls=%d (%.1fMB %.2fms) | FastAStar(%s): inits=%d steps=%d (%.2fms fb=%d)",
                        tostring(fdOk == true), fdCalls or 0, fdMB or 0, fdMs or 0, fdFb or 0,
                        tostring(c2Ok == true), c2Calls or 0, c2MB or 0, c2Ms or 0,
                        tostring(asOk == true), asInits or 0, asSteps or 0, asMs or 0, asFb or 0))
                end
            end
            return
        end
    end
    GLOBAL.print(luaLine .. " (no dinput8.dll bridge)")
end

rawset(GLOBAL, "c_hsvisual", PrintVisualReport)
rawset(GLOBAL, "c_hslatency", PrintLatencyReport)

local OPT_ALIASES = {
    af16 = "af16", af = "af16",
    lightmap = "full_lm", lm = "full_lm", full_lm = "full_lm",
    mip = "mip_sharp", mip_sharp = "mip_sharp", lod = "mip_sharp",
    cas = "cas", sharpen = "cas",
    ao = "ao", contact_ao = "ao",
    env = "env", calm_env = "env",
    bloom = "soft_bloom", soft_bloom = "soft_bloom",
    tone = "tone", storybook = "tone",
    dark = "dark_clarity", dark_clarity = "dark_clarity", night = "dark_clarity",
}

rawset(GLOBAL, "c_hsopt", function(key, val)
    if type(key) ~= "string" then
        GLOBAL.print("[HyperSmooth] Usage: c_hsopt('af16'|'lightmap'|'mip'|'cas'|'ao'|'env'|'bloom'|'tone'|'dark'|'off', true|false)")
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
        GLOBAL.print("[HyperSmooth] Unknown option '" .. tostring(key) .. "'. Valid: af16, lightmap, mip, cas, ao, env, bloom, tone, dark, off")
        return
    end
    -- Snapshot current effective flags into custom_vis_flags before flipping the chosen single switch!
    local cur = GetActiveVisualFlags()
    if current_visual_preset ~= 3 then
        custom_vis_flags.af16         = cur.af16
        custom_vis_flags.full_lm      = cur.full_lm
        custom_vis_flags.cas          = cur.cas
        custom_vis_flags.ao           = cur.ao
        custom_vis_flags.env          = cur.env
        custom_vis_flags.soft_bloom   = cur.soft_bloom
        custom_vis_flags.tone         = cur.tone
        custom_vis_flags.mip_sharp    = cur.mip_sharp
        custom_vis_flags.dark_clarity = cur.dark_clarity
    end
    if val == nil then
        custom_vis_flags[field] = not custom_vis_flags[field]
    else
        custom_vis_flags[field] = (val == true or val == 1)
    end
    if EncodeCustomVisMask(custom_vis_flags) == 0 then
        current_visual_preset = 0
    else
        current_visual_preset = 3
    end
    visual_bench_timer = 12.0
    SyncToNativePlugin(true)
    hud_dirty_now = true
    PrintVisualReport()
end)

rawset(GLOBAL, "c_hsbench", function(on)
    if on == nil then
        visual_bench_pinned = not visual_bench_pinned
    else
        visual_bench_pinned = on and true or false
    end
    hud_dirty_now = true
    GLOBAL.print("[HyperSmooth] HUD GPU/VRAM benchmark strip -> " .. (visual_bench_pinned and "PINNED ON" or "AUTO"))
end)

--------------------------------------------------------------------------------
-- 1. Fast Mod Scan: cache parsed modinfo for disabled mods so switching menus
--    doesn't re-read & re-compile 70+ unused modinfo.lua files from disk.
--    v2.2.1: preserves savedata.known_mods[modname].modinfo write-back and
--    bypasses cache on ServerCreationScreen / game_modes mods (100% DSA safe).
--------------------------------------------------------------------------------
if FAST_MOD_SCAN and KnownModIndex and KnownModIndex.LoadModInfo then
    local old_LoadModInfo = KnownModIndex.LoadModInfo
    KnownModIndex.LoadModInfo = function(self, modname, prev_info)
        if prev_info ~= nil and modname ~= nil and prev_info.version ~= nil and prev_info.game_modes == nil then
            local fe = GLOBAL.TheFrontEnd
            local scr = fe and fe:GetActiveScreen()
            local in_server_creation = scr ~= nil and scr.name == "ServerCreationScreen"
            if not in_server_creation and modname ~= "workshop-2657513551" then
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
        self.OnUpdate = function(scr, dt)
            local orig_sens = scr.zoomsensitivity
            if orig_sens and dt and dt > 0 and dt < 0.25 then
                scr.zoomsensitivity = orig_sens * (dt * 60.0)
                old_OnUpdate(scr, dt)
                scr.zoomsensitivity = orig_sens
            else
                old_OnUpdate(scr, dt)
            end
        end
    end)

    local _dbg_getinfo = (type(GLOBAL.debug) == "table" and type(GLOBAL.debug.getinfo) == "function") and GLOBAL.debug.getinfo or nil
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
    local is_zh = (enabled_str ~= "Enabled")
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
    self.options.hc_show_hud = current_show_fps_hud
    self.working.hc_show_hud = current_show_fps_hud
    self.options.hc_visual = current_visual_preset
    self.working.hc_visual = current_visual_preset

    -- Update official Refresh Rate tooltip to reflect live HyperSmooth v2.6.0 RTSS control
    if self.refreshRateSpinner.parent then
        if is_zh then
            self.refreshRateSpinner.parent.tooltip_text = has_dll
                and "由 HyperSmooth v2.6.0 零争抢 RTSS + D3D9Ex 1帧低延迟引擎实时接管：支持全屏/窗口自由切帧（60~360/无限制）"
                or "需安装配套 bin64/dinput8.dll 原生插件方可突破官方 60 帧上限与启用 RTSS 稳帧（当前仅控制全屏显示器模式）"
        else
            self.refreshRateSpinner.parent.tooltip_text = has_dll
                and "Powered live by HyperSmooth v2.6.0 RTSS Pacer + D3D9Ex 1-Frame Reflex Queue: 60-360 FPS or Unlimited."
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

        local hud_label = is_zh and "帧率显示 HUD" or "FPS Overlay HUD"
        local hud_tip = is_zh
            and "在游戏画面右上角显示轻量级实时 FPS 与帧生成时间 (ms)"
            or "Show lightweight real-time FPS and frame-time (ms) overlay in the top-right corner."
        self.hcHudSpinner = CreateNativeGraphicsSpinner(hud_label, toggle_opts, hud_tip)
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
        self.hcHudSpinner:SetSelectedIndex(self.working.hc_show_hud and 2 or 1)
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
            scr.hcHudSpinner:SetSelectedIndex(scr.working.hc_show_hud and 2 or 1)
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
            current_show_fps_hud = scr.working.hc_show_hud and true or false
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
        toast_msg = "HyperCore 60Hz SimTick ACTIVE"
        toast_timer = 4.0
        hud_dirty_now = true
        if GLOBAL.print then
            GLOBAL.print(string.format("[HyperSmooth v2.4.0] Auto-negotiated 60Hz SimTick via %s (GetTickTime=%.6f)",
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
        toast_msg = "HyperCore 30Hz Vanilla SimTick RESTORED"
        toast_timer = 3.0
        hud_dirty_now = true
        if GLOBAL.print then
            GLOBAL.print(string.format("[HyperSmooth v2.4.0] Restored 30Hz SimTick via %s (GetTickTime=%.6f)",
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
-- 10. Lightweight Zero-Churn FPS + 1% Low + P99 HUD & Optional Custom Hotkeys
--------------------------------------------------------------------------------
local function FormatTargetLabel(fps)
    if fps == 0 then
        return "UNLIMITED"
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
            local opt_str = GLOBAL.STRINGS and GLOBAL.STRINGS.UI and GLOBAL.STRINGS.UI.OPTIONS
            local is_zh = not (opt_str and opt_str.ENABLED == "Enabled")
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
    txt:SetRegionSize(1160, 30)
    txt:SetHAlign(GLOBAL.ANCHOR_RIGHT)
    txt:SetPosition(-735, -18, 0) -- right edge @ -155px: completely clear of the Day Clock!
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
        if dt and dt > 0 and dt < 1.0 then
            hd2d_accum = hd2d_accum + dt
            if hd2d_accum >= 0.10 then
                PushHd2dEnvToNative(hd2d_accum)
                hd2d_accum = 0
            end
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
            local metric_str = nil
            local color_metric = last_real_fps

            if SHOW_1PCT_LOW and type(fnStats) == "function" then
                local ok, avg, low1, p99, _, _, simhz = pcall(fnStats)
                if ok and type(avg) == "number" and avg > 1.0 and type(low1) == "number" and low1 > 1.0 then
                    disp_fps = avg
                    color_metric = low1
                    last_hud_avg_fps = avg
                    last_hud_low1_fps = low1
                    local badge = (simhz == 60) and " [60Hz]" or ""
                    metric_str = string.format("%.0f FPS | 1%%L %.0f (%.1fms)%s", avg, low1, p99 or last_ms, badge)
                end
            end
            if not metric_str then
                last_hud_avg_fps = disp_fps
                last_hud_low1_fps = disp_fps
                local badge = (active_sim_tick_hz == 60) and " [60Hz]" or ""
                metric_str = string.format("%.0f FPS (%.1fms)%s", disp_fps, last_ms, badge)
            end
            if type(HS_PanelTickTelemetry) == "function" then
                HS_PanelTickTelemetry()
            end

            -- Append compact GPU/Busy/VRAM telemetry for 12s after visual switch or when pinned via c_hsbench()
            if visual_bench_timer > 0 or visual_bench_pinned then
                local fnPerf = rawget(GLOBAL, "HyperSmooth_GetVisualPerf")
                if type(fnPerf) == "function" then
                    local okP, _, _, _, wMs, _, bMs, _, vCur = pcall(fnPerf)
                    if okP then
                        local mode_tags = { [0] = "Vanilla", [1] = "EyeHD", [2] = "Storybook", [3] = "Custom" }
                        local tag = mode_tags[current_visual_preset] or "Vanilla"
                        local vram_part = (type(vCur) == "number" and vCur > 0.5)
                            and string.format(" %.0fMB", vCur) or ""
                        metric_str = metric_str .. string.format(
                            " | %s %.1fms (W%.1f)%s",
                            tag, bMs or 0, wMs or 0, vram_part)
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
local HOTKEY_ACTIONS = {
    hotkey_cycle = function()
        local next_idx = 1
        for i, v in ipairs(FPS_PRESETS) do
            if v == current_target_fps then
                next_idx = (i % #FPS_PRESETS) + 1
                break
            end
        end
        current_target_fps = FPS_PRESETS[next_idx]
        SyncToNativePlugin(true)
        if HasNativePlugin() then
            toast_msg = "Target -> " .. FormatTargetLabel(current_target_fps)
        else
            toast_msg = "Target -> " .. FormatTargetLabel(current_target_fps) .. " (Need bin64/dinput8.dll)"
        end
        toast_timer = 3.0
        hud_dirty_now = true
    end,
    hotkey_skeletal = function()
        current_skeletal_tween = not current_skeletal_tween
        SyncToNativePlugin(true)
        if HasNativePlugin() then
            toast_msg = "Skeletal 240fps -> " .. (current_skeletal_tween and "ON" or "OFF")
        else
            toast_msg = "Skeletal 240fps -> " .. (current_skeletal_tween and "ON" or "OFF") .. " (Need bin64/dinput8.dll)"
        end
        toast_timer = 3.0
        hud_dirty_now = true
    end,
    hotkey_hud = function()
        current_show_fps_hud = not current_show_fps_hud
        SyncToNativePlugin(true)
        toast_msg = "FPS HUD -> " .. (current_show_fps_hud and "ON" or "OFF")
        toast_timer = 2.0
        hud_dirty_now = true
    end,
    hotkey_visual = function()
        local has_custom = EncodeCustomVisMask(custom_vis_flags) > 0
        local cycle_n = has_custom and 4 or 3
        current_visual_preset = (current_visual_preset + 1) % cycle_n
        visual_bench_timer = 12.0
        SyncToNativePlugin(true)
        local labels = {
            [0] = "Visual -> Vanilla (OFF)",
            [1] = "Visual -> Eye-Care HD",
            [2] = "Visual -> Soft Storybook",
            [3] = "Visual -> Custom",
        }
        toast_msg = labels[current_visual_preset] or ("Visual -> " .. tostring(current_visual_preset))
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
        local cur = GetActiveVisualFlags()
        if current_visual_preset ~= 3 then
            custom_vis_flags.af16         = cur.af16
            custom_vis_flags.full_lm      = cur.full_lm
            custom_vis_flags.cas          = cur.cas
            custom_vis_flags.ao           = cur.ao
            custom_vis_flags.env          = cur.env
            custom_vis_flags.soft_bloom   = cur.soft_bloom
            custom_vis_flags.tone         = cur.tone
            custom_vis_flags.mip_sharp    = cur.mip_sharp
            custom_vis_flags.dark_clarity = cur.dark_clarity
        end
        custom_vis_flags[field] = not custom_vis_flags[field]
        current_visual_preset = (EncodeCustomVisMask(custom_vis_flags) == 0) and 0 or 3
        visual_bench_timer = 12.0
        SyncToNativePlugin(true)
        hud_dirty_now = true
    end,
    ToggleSkeletal = function()
        current_skeletal_tween = not current_skeletal_tween
        SyncToNativePlugin(true)
        toast_msg = "240Hz Smooth -> " .. (current_skeletal_tween and "ON" or "OFF")
        toast_timer = 2.5
        hud_dirty_now = true
    end,
    ToggleLowLatency = function()
        current_low_latency = not current_low_latency
        SyncToNativePlugin(true)
        toast_msg = "Low Latency -> " .. (current_low_latency and "ON" or "OFF")
        toast_timer = 2.5
        hud_dirty_now = true
    end,
    GetSnapshot = function()
        local has_dll = HasNativePlugin()
        local avg_fps, low1_fps = last_hud_avg_fps, last_hud_low1_fps
        local busy_ms, vram_mb = 0, 0
        local ex_act, cur_lat, orig_lat = false, 3, 3
        local vfx_ok, light_ok, lm_scale = false, false, 0.5

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

        local dll_status, dll_ver, expected_dll_ver = GetDllVersionStatus()

        return {
            preset           = current_visual_preset,
            flags            = GetActiveVisualFlags(),
            skeletal         = current_skeletal_tween,
            low_latency      = current_low_latency,
            has_dll          = has_dll,
            dll_status       = dll_status,
            dll_ver          = dll_ver,
            expected_dll_ver = expected_dll_ver,
            avg_fps          = avg_fps,
            low1_fps         = low1_fps,
            busy_ms          = busy_ms,
            vram_mb          = vram_mb,
            d3d9ex_active    = ex_act,
            cur_lat          = cur_lat,
            orig_lat         = orig_lat,
            vfx_ok           = vfx_ok,
            light_ok         = light_ok,
            lm_scale         = lm_scale,
            flick_rescues    = latency_stats.flick_rescues,
        }
    end,
}

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

if type(modimport) == "function" then
    pcall(modimport, "keybind")
    pcall(modimport, "hs_panel")
end
