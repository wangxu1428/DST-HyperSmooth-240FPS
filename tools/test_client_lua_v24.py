#!/usr/bin/env python3
"""
Executes Client v2.4.0 modinfo.lua (in Klei's EXACT bare sandbox — zero globals!)
and modmain.lua (OptionsScreen takeover + 1% Low HUD + Route B 60Hz handshake &
public-server 30Hz isolation) inside Klei's REAL Lua 5.1 VM!
"""
import os
import shutil
import subprocess

CLIENT_MOD = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_fps_unlocker\workshop_mod\dst_smooth_highfps"
SERVER_RT = r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_hypercore_server\bench_runtime"
BIN64 = os.path.join(SERVER_RT, "bin64")
MODS_DIR = os.path.join(SERVER_RT, "mods")
STORAGE = os.path.join(SERVER_RT, "storage")
BASE_BACKUP = os.path.join(SERVER_RT, "base_world_backup")
CLUSTER_NAME = "ClientLuaTestCluster"
CLUSTER_DIR = os.path.join(STORAGE, "DoNotStarveTogether", CLUSTER_NAME)

HARNESS_NAME = "client_v24_lua_test"
HARNESS_DIR = os.path.join(MODS_DIR, HARNESS_NAME)

HARNESS_MODINFO = """
name = "Client v2.4.0 Lua Harness"
description = "Runs bare-sandbox modinfo.lua + modmain.lua behavioral tests in Klei Lua 5.1."
author = "test"
version = "1.0.0"
api_version = 10
dst_compatible = true
all_clients_require_mod = false
client_only_mod = false
"""

HARNESS_MODMAIN = r"""
local G = GLOBAL
local assert = G.assert
local type = G.type
local pairs = G.pairs
local ipairs = G.ipairs
local tostring = G.tostring
local setfenv = G.setfenv
local setmetatable = G.setmetatable
local string = G.string
local math = G.math

AddSimPostInit(function()
    -- =========================================================================
    -- TEST 1: Exact Klei Bare-Sandbox modinfo.lua Check (ZERO Lua globals!)
    -- =========================================================================
    local fn_info, err_info = G.kleiloadlua("../mods/client_v24_lua_test/target_modinfo.lua")
    assert(fn_info ~= nil, "Failed loading modinfo.lua: " .. tostring(err_info))
    local bare_env = {
        folder_name = "dst_smooth_highfps",
        locale = "zh",
        ChooseTranslationTable = function(t) return t.zh or t[1] end,
    }
    setfenv(fn_info, bare_env)
    fn_info()
    assert(bare_env.version == "2.8.0", "Expected modinfo version 2.8.0, got " .. tostring(bare_env.version))
    assert(bare_env.client_only_mod == true and bare_env.all_clients_require_mod == false, "Wrong mod flags")
    local cfg_map = {}
    for i = 1, #bare_env.configuration_options do
        local opt = bare_env.configuration_options[i]
        cfg_map[opt.name] = opt
    end
    assert(cfg_map.visual_preset ~= nil and cfg_map.visual_preset.default == 0, "Player-First violated: visual_preset default must be 0 (Vanilla Pure)!")
    for _, k in ipairs({"visual_af16", "visual_full_lightmap", "visual_cas", "visual_contact_ao", "visual_calm_env", "visual_soft_bloom", "visual_storybook_tone", "visual_mip_sharp", "visual_dark_clarity"}) do
        assert(cfg_map[k] ~= nil and cfg_map[k].default == false, "Player-First violated: " .. k .. " must exist and default to false!")
    end
    assert(#bare_env.configuration_options == 30, "Expected 30 modinfo options (29 + visual_clean_filter), got " .. tostring(#bare_env.configuration_options))
    assert(cfg_map.hud_mode ~= nil and cfg_map.hud_mode.default == "simple", "Expected hud_mode default simple")
    assert(cfg_map.low_latency_input ~= nil and cfg_map.low_latency_input.default == true, "Missing low_latency_input")
    assert(cfg_map.show_1pct_low ~= nil and cfg_map.show_1pct_low.default == true, "Missing show_1pct_low")
    assert(cfg_map.auto_60hz_sim ~= nil and cfg_map.auto_60hz_sim.default == true, "Missing auto_60hz_sim")
    assert(cfg_map.hotkey_cycle ~= nil and cfg_map.hotkey_cycle.default == "KEY_DISABLED", "Missing hotkey_cycle")
    assert(cfg_map.hotkey_skeletal ~= nil and cfg_map.hotkey_skeletal.default == "KEY_DISABLED", "Missing hotkey_skeletal")
    assert(cfg_map.hotkey_hud ~= nil and cfg_map.hotkey_hud.default == "KEY_DISABLED", "Missing hotkey_hud")
    assert(cfg_map.hotkey_visual ~= nil and cfg_map.hotkey_visual.default == "KEY_DISABLED", "Missing hotkey_visual")
    assert(cfg_map.hotkey_panel ~= nil and cfg_map.hotkey_panel.default == "KEY_DISABLED", "Missing hotkey_panel")
    assert(bare_env.keys ~= nil and #bare_env.keys >= 90, "Expected >=90 bindable keys, got " .. tostring(bare_env.keys and #bare_env.keys))
    local extra_ok = { KEY_DISABLED = true, KEY_QUOTE = true, KEY_BACKQUOTE = true, ["\238\132\130"] = true, ["\238\132\131"] = true, ["\238\132\132"] = true }
    for i = 1, #bare_env.keys do
        local kname = bare_env.keys[i].data
        assert(extra_ok[kname] or G[kname] ~= nil, "Invalid Klei KEY constant in modinfo.lua: " .. tostring(kname))
    end
    local bare_env_en = {
        folder_name = "dst_smooth_highfps",
        locale = "en",
        ChooseTranslationTable = function(t) return t.en or t[1] end,
    }
    setfenv(fn_info, bare_env_en)
    fn_info()
    assert(bare_env_en.version == "2.8.0", "Expected EN modinfo version 2.8.0, got " .. tostring(bare_env_en.version))
    assert(type(bare_env_en.name) == "string" and bare_env_en.name ~= bare_env.name,
           "Expected localized EN modinfo name to differ from ZH")
    assert(type(bare_env_en.description) == "string" and bare_env_en.description ~= bare_env.description,
           "Expected localized EN modinfo description to differ from ZH")
    assert(#bare_env_en.configuration_options == #bare_env.configuration_options,
           "Expected EN/ZH option count match")
    for idx = 1, #bare_env.configuration_options do
        local zopt = bare_env.configuration_options[idx]
        local eopt = bare_env_en.configuration_options[idx]
        assert(zopt.name == eopt.name, "Option key mismatch at " .. tostring(idx))
        assert(zopt.default == eopt.default, "Option default mismatch for " .. tostring(zopt.name))
        assert(#zopt.options == #eopt.options, "Option values count mismatch for " .. tostring(zopt.name))
        if zopt.options then
            for j = 1, #zopt.options do
                assert(zopt.options[j].data == eopt.options[j].data,
                       "Sub-option data mismatch for " .. tostring(zopt.name))
            end
        end
    end
    print("[CLIENT_V24_TEST] 1. Bare-sandbox modinfo.lua PASS (v2.8.0 auto-locale ZH/EN, Compact HUD default, " .. tostring(#bare_env.configuration_options) .. " opts)")

    -- =========================================================================
    -- TEST 2: modmain.lua Full Behavioral Suite (OptionsScreen + 1% Low HUD + HD-2D World Shaders + Route B)
    -- =========================================================================
    local function RunModMainScenario(cfg_overrides, expect_60hz, trigger_mode)
        local hooks = { class = {}, comp = {}, prefab = {}, sim = {}, rpc = {}, sh_init = {}, sh_sort = {} }
        local native_fps = 240
        local native_skel = true
        local native_vfx = true
        local native_lowlat = true
        local native_sim_hz = 30
        local native_af = 1
        local native_full_lm = false
        local native_mip_bias = 0.0
        local native_hd2d = nil
        local mock_tick = 120
        local mock_rpc_table = {}
        local mock_rpc_handlers = {}
        local pp_enabled = {}
        local pp_uniforms = {}
        local pp_bloom_params = nil
        local pp_next_id = 100
        local pp_bloom_flag = true
        local pp_mt = { __index = {} }
        function pp_mt.__index:SetBloomSamplerParams(sm, w, h, cm) pp_bloom_params = {sm, w, h, cm} end
        function pp_mt.__index:AddUniformVariable(name, cnt) pp_next_id = pp_next_id + 1; return pp_next_id end
        function pp_mt.__index:AddSamplerEffect(sh, sm, w, h, cm, b, bid) pp_next_id = pp_next_id + 1; return pp_next_id end
        function pp_mt.__index:AddPostProcessEffect(sh) pp_next_id = pp_next_id + 1; return pp_next_id end
        function pp_mt.__index:AddSampler() end
        function pp_mt.__index:SetEffectUniformVariables() end
        function pp_mt.__index:SetSamplerEffectState() end
        function pp_mt.__index:SetSamplerEffectFilter() end
        function pp_mt.__index:SetPostProcessEffectAfter() end
        function pp_mt.__index:SetUniformVariable(id, a, b, c, d) pp_uniforms[id] = {a, b, c, d} end
        function pp_mt.__index:EnablePostProcessEffect(id, en) pp_enabled[id] = en and true or false end
        function pp_mt.__index:SetBloomEnabled(en) pp_bloom_flag = en and true or false; pp_enabled[1] = pp_bloom_flag end
        function pp_mt.__index:IsBloomEnabled() return pp_bloom_flag end
        local mock_pp = setmetatable({}, pp_mt)

        local fakeG = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print,
            NUMBERFONT = "numberfont", ANCHOR_RIGHT = 2, ANCHOR_TOP = 1,
            KEY_ALT = 308, KEY_LALT = 308, KEY_RALT = 307, KEY_F8 = 289, KEY_F9 = 290,
            CONTROL_MOVE_UP = 5, CONTROL_MOVE_DOWN = 6, CONTROL_MOVE_LEFT = 7, CONTROL_MOVE_RIGHT = 8,
            CONTROL_CONTROLLER_ATTACK = 39,
            TUNING = { CONTROLLER_DEADZONE_RADIUS = 0.3 },
            IsPaused = function() return false end,
            STRINGS = { UI = { OPTIONS = { ENABLED = "启用", DISABLED = "禁用" } } },
            CLIENT_MOD_RPC = mock_rpc_table,
            CLIENT_MOD_RPC_HANDLERS = mock_rpc_handlers,
            PostProcessor = mock_pp,
            PostProcessorEffects = { ZoomBlur = 0, Bloom = 1, ColourCube = 2, MoonPulseGrading = 3 },
            SamplerEffects = {},
            UniformVariables = { SAMPLER_PARAMS = 10 },
            SamplerSizes = { Relative = 0, Static = 1 },
            SamplerColourMode = { RGBA = 0, RGB = 1 },
            SamplerEffectBase = { PostProcessSampler = 0, BloomSampler = 1, Shader = 2, Texture = 3, Smoke = 4 },
            FILTER_MODE = { POINT = 0, LINEAR = 1, ANISOTROPIC = 2, NONE = 3 },
            MIP_FILTER_MODE = { NONE = 0, POINT = 1, LINEAR = 2 },
            WRAP_MODE = { WRAP = 0, CLAMP = 1, MIRROR = 2, CLAMP_TO_EDGE = 3 },
            resolvefilepath = function(rel) return "../mods/dst_smooth_highfps/" .. rel end,
            TheWorld = {
                state = { time = 0.68, phase = "dusk", isfullmoon = false, israining = true, precipitationrate = 0.8, season = "autumn" },
                HasTag = function() return false end,
            },
            HyperSmooth_SetFps = function(v) native_fps = v; return true end,
            HyperSmooth_SetSkeletal = function(v) native_skel = v; return true end,
            HyperSmooth_SetVfxSmooth = function(v) native_vfx = v; return true end,
            HyperSmooth_SetLowLatency = function(v) native_lowlat = v; return true end,
            HyperSmooth_SetVisual = function(af, flm, mip) native_af = af; native_full_lm = flm; native_mip_bias = mip or 0.0; return true end,
            HyperSmooth_GetVisualStats = function() return native_af, 16, 42, true, native_full_lm and 1.0 or 0.5, true, native_mip_bias, 18 end,
            HyperSmooth_GetLatencyStats = function() return native_lowlat, native_lowlat and 1 or 3, 3, 0.35, 120, true, 90, 4800, true, native_mip_bias, true, 640, true end,
            HyperSmooth_SetHd2d = function(...) native_hd2d = {...}; return true end,
            HyperSmooth_GetVisualPerf = function()
                local mStr = (native_hd2d and native_hd2d[1]) or 0.0
                return true, 10, mStr, 1.5, 0.3, 2.2, 0.4, 445, 452, 7200
            end,
            HyperSmooth_GetFrameStats = function() return 239.8, 226.4, 4.21, 0.04, 15, native_sim_hz end,
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.8.0" end,
            HyperSmooth_SetSimTickRate = function(hz)
                if hz == 30 or hz == 60 then native_sim_hz = hz; return true end
                return false
            end,
            HyperSmooth_GetSimTickRate = function() return native_sim_hz end,
            TheSim = {
                GetTick = function() return mock_tick end,
                GetStaticTick = function() return mock_tick end,
                GetTickTime = function() return 1.0 / native_sim_hz end,
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                GetPosition = function() return 0, 0 end,
            },
            TheInput = {
                OnUpdate = function() end,
                IsKeyDown = function() return false end,
                AddKeyUpHandler = function() end,
                GetAnalogControlValue = function() return 0 end,
            },
            TheCamera = {
                GetRightVec = function()
                    local v = { x = 1, y = 0, z = 0 }
                    local mt = {}
                    mt.__mul = function(a, s) return setmetatable({ x = a.x * s, y = a.y * s, z = a.z * s }, mt) end
                    mt.__sub = function(a, b) return setmetatable({ x = a.x - b.x, y = a.y - b.y, z = a.z - b.z }, mt) end
                    mt.__index = {
                        GetNormalized = function(self)
                            local len = math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)
                            if len <= 0 then return { x = 0, y = 0, z = 0 } end
                            return { x = self.x / len, y = self.y / len, z = self.z / len }
                        end,
                    }
                    return setmetatable(v, mt)
                end,
                GetDownVec = function()
                    local v = { x = 0, y = 0, z = 1 }
                    local mt = {}
                    mt.__mul = function(a, s) return setmetatable({ x = a.x * s, y = a.y * s, z = a.z * s }, mt) end
                    mt.__sub = function(a, b) return setmetatable({ x = a.x - b.x, y = a.y - b.y, z = a.z - b.z }, mt) end
                    mt.__index = {
                        GetNormalized = function(self)
                            local len = math.sqrt(self.x * self.x + self.y * self.y + self.z * self.z)
                            if len <= 0 then return { x = 0, y = 0, z = 0 } end
                            return { x = self.x / len, y = self.y / len, z = self.z / len }
                        end,
                    }
                    return setmetatable(v, mt)
                end,
            },
            TheFrontEnd = {
                GetGraphicsOptions = function()
                    return {
                        GetDisplayMode = function() return 1920, 1080 end,
                        GetNumRefreshRates = function() return 2 end,
                        GetRefreshRate = function(_, _, _, _, idx) return idx == 0 and 60 or 144 end,
                    }
                end,
                GetActiveScreen = function() return { name = "HUD" } end,
            },
            require = function(path)
                if path == "widgets/text" then
                    return function()
                        local w = { str = "", visible = true, colour = nil, inst = { IsValid = function() return true end } }
                        function w:SetHAnchor() end
                        function w:SetVAnchor() end
                        function w:SetRegionSize() end
                        function w:SetHAlign() end
                        function w:SetPosition() end
                        function w:SetColour(r,g,b,a) self.colour = {r,g,b,a} end
                        function w:SetClickable() end
                        function w:Hide() self.visible = false end
                        function w:Show() self.visible = true end
                        function w:SetString(s) self.str = s end
                        return w
                    end
                elseif path == "widgets/redux/templates" then
                    return {
                        LabelSpinner = function(label, data, ...)
                            local sp = { options = data, idx = 1, enabled = true, has_mod = false }
                            function sp:SetOptions(o) self.options = o end
                            function sp:SetSelectedIndex(i) self.idx = i end
                            function sp:GetSelectedIndex() return self.idx end
                            function sp:Enable() self.enabled = true end
                            function sp:Disable() self.enabled = false end
                            function sp:EnablePendingModificationBackground() end
                            function sp:SetHasModification(v) self.has_mod = v end
                            local w = { spinner = sp, tooltip_text = label }
                            sp.parent = w
                            function w:AddChild(c) return c end
                            return w
                        end,
                        ListItemBackground = function()
                            return { SetPosition = function() end, MoveToBack = function() end }
                        end,
                    }
                end
                return {}
            end,
        }

        -- Simulate co-loaded Server RPC slot id=1 if requested
        if trigger_mode == "COLOAD_RPC" then
            mock_rpc_table["HyperCoreProto"] = { SimTickRate = { namespace = "HyperCoreProto", id = 1 } }
            mock_rpc_handlers["HyperCoreProto"] = { [1] = function() end }
        end

        local env = {
            GLOBAL = fakeG,
            MODROOT = "../mods/dst_smooth_highfps/",
            Asset = function(t, f) return { type = t, file = f } end,
            GetModConfigData = function(k) return cfg_overrides[k] end,
            AddClassPostConstruct = function(cls, fn) hooks.class[cls] = fn end,
            AddComponentPostInit = function(c, fn) hooks.comp[c] = fn end,
            AddPrefabPostInit = function(p, fn) hooks.prefab[p] = fn end,
            AddSimPostInit = function(fn) hooks.sim[#hooks.sim + 1] = fn end,
            AddModShadersInit = function(fn) hooks.sh_init[#hooks.sh_init + 1] = fn end,
            AddModShadersSortAndEnable = function(fn) hooks.sh_sort[#hooks.sh_sort + 1] = fn end,
            GetClientModRPC = function(ns, name)
                -- Mirrors Klei scripts/networkclientrpc.lua:1977 (throws if ns unregistered!)
                return mock_rpc_table[ns][name]
            end,
            AddClientModRPCHandler = function(ns, name, fn)
                mock_rpc_table[ns] = mock_rpc_table[ns] or {}
                mock_rpc_handlers[ns] = mock_rpc_handlers[ns] or {}
                mock_rpc_handlers[ns][#mock_rpc_handlers[ns] + 1] = fn
                mock_rpc_table[ns][name] = { namespace = ns, id = #mock_rpc_handlers[ns] }
            end,
        }
        local fn_main, err_main = G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua")
        assert(fn_main ~= nil, "Failed loading modmain.lua: " .. tostring(err_main))
        setfenv(fn_main, env)
        fn_main()

        -- Verify Player-First Default: 100% Vanilla Pure (0 forced post-process, 0 world shaders!)
        -- Note: BloomSampler canvas pre-allocates at 1.0x HD during ModShadersInit (only phase Klei allows resize)
        -- so live F7 switching to Preset 2 gets full-res shadows immediately; costs 0ms when Bloom & Shadows OFF!
        assert(type(env.Assets) == "table" and #env.Assets == 4, "Expected 4 SHADER Assets")
        assert(#hooks.sh_init == 1 and #hooks.sh_sort == 1, "Expected ModShadersInit & Sort hooks")
        hooks.sh_init[1]()
        hooks.sh_sort[1]()
        assert(pp_bloom_params ~= nil and pp_bloom_params[2] == 1.0 and pp_bloom_params[3] == 1.0,
            "Fluid Shadow Engine v2 must pre-configure 1.0x HD BloomSampler during ModShadersInit!")
        assert(fakeG.PostProcessorEffects.HS_Bloom ~= nil and fakeG.PostProcessorEffects.HS_VisualFinal ~= nil, "Visual shaders missing")
        assert(pp_enabled[1] == true and pp_enabled[fakeG.PostProcessorEffects.HS_Bloom] == false, "Default Vanilla must keep stock Bloom untouched")
        assert(pp_enabled[fakeG.PostProcessorEffects.HS_VisualFinal] == false, "Default Vanilla must NOT enable HS_VisualFinal!")
        assert(native_af == 1 and native_full_lm == false and math.abs(native_mip_bias) < 1e-4, "Default Vanilla must keep AF=1x, FullLM=false, MipBias=0")
        assert(native_hd2d ~= nil and math.abs(native_hd2d[1]) < 1e-4 and math.abs(native_hd2d[2]) < 1e-4 and math.abs(native_hd2d[3]) < 1e-4 and math.abs(native_hd2d[7]) < 1e-4, "Default Vanilla must keep ao=0, env=0, dark=0, lightDither=0")
        assert(native_vfx == true and native_lowlat == true, "Default 240Hz VFX smooth & LowLatency must be enabled")

        -- Verify independent per-effect toggling via c_hsopt() without affecting other toggles!
        fakeG.c_hsopt("ao", true)
        assert(math.abs(native_hd2d[1] - 1.0) < 1e-4 and math.abs(native_hd2d[2]) < 1e-4 and math.abs(native_hd2d[3]) < 1e-4 and math.abs(native_hd2d[7]) < 1e-4, "c_hsopt('ao') should enable ONLY ao")
        assert(native_af == 1 and native_full_lm == false and math.abs(native_mip_bias) < 1e-4 and pp_enabled[fakeG.PostProcessorEffects.HS_VisualFinal] == false, "c_hsopt('ao') leaked into other toggles!")
        fakeG.c_hsopt("lightmap", true)
        assert(native_full_lm == true and math.abs(native_hd2d[7] - 1.0) < 1e-4, "c_hsopt('lightmap') should enable 1.0x LM + campfire IGN anti-banding!")
        fakeG.c_hsopt("lightmap", false)
        assert(native_full_lm == false and math.abs(native_hd2d[7]) < 1e-4, "c_hsopt('lightmap', false) should disable LM + anti-banding!")
        fakeG.c_hsopt("mip", true)
        assert(math.abs(native_mip_bias + 0.45) < 1e-4, "c_hsopt('mip') should set MipLodBias=-0.45")
        fakeG.c_hsopt("dark", true)
        assert(math.abs(native_hd2d[3] - 1.0) < 1e-4, "c_hsopt('dark') should set darkClarity=1.0")
        fakeG.c_hsopt("env", true)
        assert(math.abs(native_hd2d[2] - 1.0) < 1e-4 and native_hd2d[12] > 0.5, "c_hsopt('env') should enable calmEnv + rainWetness")
        fakeG.c_hsopt("cas", true)
        assert(pp_enabled[fakeG.PostProcessorEffects.HS_VisualFinal] == true and math.abs(native_hd2d[7] - 1.0) < 1e-4, "c_hsopt('cas') should enable HS_VisualFinal + lighting anti-banding")
        local vu = pp_uniforms[fakeG.UniformVariables.HS_VISUAL_PARAMS]
        local ga = pp_uniforms[fakeG.UniformVariables.HS_GRADE_A]
        assert(vu ~= nil and math.abs(vu[1] - 5.20) < 1e-2 and math.abs(vu[2] - 1.25) < 1e-2 and math.abs(vu[4]) < 1e-4, "c_hsopt('cas') BCAS+Deconv params wrong")
        assert(ga ~= nil and math.abs(ga[1]) < 1e-4 and math.abs(ga[2]) < 1e-4 and math.abs(ga[4] - 1.0) < 1e-4, "c_hsopt('cas') must keep neutral grading")
        -- Disabling via c_hsopt("off") must 100% restore Vanilla Pure!
        fakeG.c_hsopt("off")
        assert(native_af == 1 and native_full_lm == false and math.abs(native_mip_bias) < 1e-4 and math.abs(native_hd2d[1]) < 1e-4 and math.abs(native_hd2d[2]) < 1e-4 and math.abs(native_hd2d[3]) < 1e-4 and math.abs(native_hd2d[7]) < 1e-4, "c_hsopt('off') failed to restore native Vanilla")
        assert(pp_enabled[1] == true and pp_enabled[fakeG.PostProcessorEffects.HS_Bloom] == false and pp_enabled[fakeG.PostProcessorEffects.HS_VisualFinal] == false, "c_hsopt('off') failed to restore post-process Vanilla")

        -- Ensure co-loading NEVER creates duplicate slot id=2!
        assert(mock_rpc_table["HyperCoreProto"]["SimTickRate"].id == 1, "RPC slot duplicated!")
        assert(#mock_rpc_handlers["HyperCoreProto"] == 1, "RPC handler table grew past 1!")

        -- Trigger world scenario
        if trigger_mode == "TAG" then
            local net = { HasTag = function(_, t) return t == "hypercore_sim60" end }
            hooks.prefab["forest_network"](net)
        elseif trigger_mode == "RPC" or trigger_mode == "COLOAD_RPC" then
            mock_rpc_handlers["HyperCoreProto"][1](60)
        elseif trigger_mode == "VANILLA" then
            local net = { HasTag = function() return false end }
            hooks.prefab["forest_network"](net)
        end

        if expect_60hz then
            assert(native_sim_hz == 60, "Expected 60Hz SimTick for " .. trigger_mode)
            assert(math.abs(fakeG.GetTime() - 2.0) < 1e-6, "Dynamic GetTime wrong at 60Hz!")
        else
            assert(native_sim_hz == 30, "Expected 30Hz stock for " .. trigger_mode .. ", got " .. tostring(native_sim_hz))
        end

        -- Test PlayerHUD auto-ZH Compact default + Detailed toggle + [60Hz] formatting
        local hud_child = nil
        local hud = {
            AddChild = function(_, w) hud_child = w; return w end,
            OnUpdate = function() end,
        }
        hooks.class["screens/playerhud"](hud)
        hud:OnUpdate(0.30)
        assert(hud_child ~= nil and hud_child.visible == true, "HUD should be visible")
        assert(hud_child.str:find("帧率 240") ~= nil and hud_child.str:find("最低帧") == nil,
               "Expected Compact ZH HUD default: " .. tostring(hud_child.str))
        if expect_60hz then
            assert(hud_child.str:find("%[60Hz%]") ~= nil, "HUD missing [60Hz] badge: " .. tostring(hud_child.str))
        else
            assert(hud_child.str:find("%[60Hz%]") == nil, "Vanilla HUD should NOT show [60Hz]: " .. tostring(hud_child.str))
        end
        env.HS_PanelApi.CycleHudMode() -- simple -> detail
        hud:OnUpdate(0.30)
        assert(hud_child.str:find("帧率 240 · 最低帧 226 · 帧时 4%.2ms") ~= nil,
               "Detailed ZH HUD missing 1% Low telemetry: " .. tostring(hud_child.str))
        env.HS_PanelApi.CycleHudMode() -- detail -> hidden
        assert(env.HS_PanelApi.GetSnapshot().hud_show == false, "Expected HUD hidden after 2nd cycle")
        env.HS_PanelApi.CycleHudMode() -- hidden -> simple
        assert(env.HS_PanelApi.GetSnapshot().hud_show == true and env.HS_PanelApi.GetSnapshot().hud_mode == "simple",
               "Expected HUD back to simple after 3rd cycle")

        -- Verify DLL Version Handshake & Update Banner status detection ("ok" / "outdated" / "missing")
        do
            local st, ver, exp = fakeG.HyperSmooth_GetDllStatus()
            assert(st == "ok" and ver == "2.8.0" and exp == "2.8.0",
                   "Expected ok/2.8.0/2.8.0, got " .. tostring(st) .. "/" .. tostring(ver))
            local snap_ok = env.HS_PanelApi.GetSnapshot()
            assert(snap_ok.dll_status == "ok" and snap_ok.dll_ver == "2.8.0", "Panel snapshot dll_status ok mismatch")

            local saved_ver = fakeG.HyperSmooth_Version
            local saved_fps = fakeG.HyperSmooth_SetFps
            fakeG.HyperSmooth_Version = function() return "DST-HyperSmooth/2.7.0" end
            local st2, ver2 = fakeG.HyperSmooth_GetDllStatus()
            assert(st2 == "outdated" and ver2 == "2.7.0", "Older 2.7.0 DLL must report outdated, got " .. tostring(st2))

            fakeG.HyperSmooth_Version = nil
            local st3, ver3 = fakeG.HyperSmooth_GetDllStatus()
            assert(st3 == "outdated" and ver3 == "<2.8.0", "Pre-2.8.0 bridge DLL must report outdated/<2.8.0, got " .. tostring(st3))

            fakeG.HyperSmooth_SetFps = nil
            local st4, ver4 = fakeG.HyperSmooth_GetDllStatus()
            assert(st4 == "missing" and ver4 == nil, "Missing/v2.2.0 DLL must report missing, got " .. tostring(st4))

            fakeG.HyperSmooth_Version = saved_ver
            fakeG.HyperSmooth_SetFps = saved_fps
        end

        -- Test OptionsScreen native Refresh Rate takeover
        local rr_sp = { options = {}, idx = 1, enabled = false, has_mod = false, parent = {} }
        function rr_sp:SetOptions(o) self.options = o end
        function rr_sp:SetSelectedIndex(i) self.idx = i end
        function rr_sp:GetSelectedIndex() return self.idx end
        function rr_sp:Enable() self.enabled = true end
        function rr_sp:SetHasModification(v) self.has_mod = v end
        local applied_hw_hz = nil
        local scr_grid_coords = {}
        for r = 1, 8 do scr_grid_coords[r] = { nil, nil } end
        local scr = {
            refreshRateSpinner = rr_sp,
            options = { refreshrate = 60, fullscreen = true, display = 0, mode_idx = 0 },
            working = { refreshrate = 60, fullscreen = true, display = 0, mode_idx = 0 },
            -- Real Klei Grid uses `.rows` + `.cols` + `.items_by_coords` (NO `.num_rows`!)
            grid_graphics = {
                rows = 8, cols = 2, items_by_coords = scr_grid_coords, items = {},
                AddItem = function(g, it, c, r)
                    assert(r <= g.rows and c <= g.cols, "Grid:AddItem out of bounds r=" .. tostring(r) .. " rows=" .. tostring(g.rows))
                    assert(g.items_by_coords[r] ~= nil, "Grid:AddItem missing items_by_coords[" .. tostring(r) .. "]")
                    g.items_by_coords[r][c] = it
                    g.items[r] = it
                end,
            },
            right_spinners_graphics = { 1, 2, 3, 4, 5 },
            UpdateResolutionsSpinner = function() end,
            UpdateRefreshRatesSpinner = function() end,
            IsGraphicsDirty = function() return false end,
            ChangeGraphicsMode = function(s) applied_hw_hz = s.working.refreshrate end,
            InitializeSpinners = function() end,
            Apply = function() end,
            Save = function(_, cb) if cb then cb() end end,
            UpdateMenu = function() end,
        }
        hooks.class["screens/redux/optionsscreen"](scr)
        assert(rr_sp.enabled == true, "refreshRateSpinner not enabled")
        assert(scr.hcSkeletalSpinner ~= nil and scr.hcHudSpinner ~= nil and scr.hcVisualSpinner ~= nil, "Right-column toggles missing")
        assert(scr.grid_graphics.rows == 8 and #scr.right_spinners_graphics == 8, "Expected exact 8 right-column spinners (rows 6-8)")
        -- Test Eye-Care HD (1) with game Bloom OFF (99% of DST players!): HS_Bloom MUST stay OFF, while AF/LM/CAS/AO work!
        mock_pp:SetBloomEnabled(false)
        scr.working.refreshrate = 0
        scr.working.hc_visual = 1
        scr:ChangeGraphicsMode()
        assert(applied_hw_hz == 144, "PickSafeHardwareHz failed: got " .. tostring(applied_hw_hz))
        scr:Apply()
        assert(native_fps == 0, "Apply failed to push Unlimited (0) to C++ bridge")
        assert(native_af == 16 and native_full_lm == true and math.abs(native_mip_bias + 0.45) < 1e-4 and math.abs(native_hd2d[1] - 1.0) < 1e-4 and math.abs(native_hd2d[2]) < 1e-4 and math.abs(native_hd2d[3]) < 1e-4, "Eye-Care HD preset flags wrong")
        assert(pp_enabled[1] == false and pp_enabled[fakeG.PostProcessorEffects.HS_Bloom] == false and pp_enabled[fakeG.PostProcessorEffects.HS_VisualFinal] == true, "Eye-Care HD must respect game Bloom OFF")
        hud:OnUpdate(0.30)
        assert(hud_child.str:find("护眼高清 耗时2%.2ms") ~= nil and hud_child.str:find("445MB") ~= nil,
               "HUD missing compact EyeHD/Busy/VRAM ZH telemetry: " .. tostring(hud_child.str))
        -- Test OptionsScreen 3-state hcHudSpinner (0=Off, 1=Compact, 2=Detail)
        scr.working.hc_show_hud = 2
        scr:Apply()
        assert(env.HS_PanelApi.GetSnapshot().hud_mode == "detail" and env.HS_PanelApi.GetSnapshot().hud_show == true,
               "OptionsScreen hc_show_hud=2 should enable detailed mode")
        scr.working.hc_show_hud = 1
        scr:Apply()
        assert(env.HS_PanelApi.GetSnapshot().hud_mode == "simple" and env.HS_PanelApi.GetSnapshot().hud_show == true,
               "OptionsScreen hc_show_hud=1 should restore compact mode")
        -- Test Soft Storybook (2) with game Bloom ON -> highlight-protected Soft Bloom active
        mock_pp:SetBloomEnabled(true)
        scr.working.hc_visual = 2
        scr:Apply()
        assert(math.abs(native_hd2d[1] - 1.10) < 1e-4 and math.abs(native_hd2d[2] - 1.0) < 1e-4, "Soft Storybook world flags wrong")
        assert(pp_enabled[1] == false and pp_enabled[fakeG.PostProcessorEffects.HS_Bloom] == true and pp_enabled[fakeG.PostProcessorEffects.HS_VisualFinal] == true, "Soft Storybook post flags wrong")
        -- Turn back to Vanilla Pure (0) -> 100% complete restore!
        scr.working.hc_visual = 0
        scr:Apply()
        assert(native_af == 1 and native_full_lm == false and math.abs(native_hd2d[1]) < 1e-4 and math.abs(native_hd2d[2]) < 1e-4, "Vanilla restore failed")
        assert(pp_enabled[1] == true and pp_enabled[fakeG.PostProcessorEffects.HS_Bloom] == false and pp_enabled[fakeG.PostProcessorEffects.HS_VisualFinal] == false, "Vanilla post restore failed")
        fakeG.c_hsvisual()

        -- Test 240Hz Immediate WASD RPC Dispatch (Lag Comp OFF only!)
        if trigger_mode == "VANILLA" then
            assert(type(hooks.comp["playercontroller"]) == "function", "playercontroller PostInit missing!")
            local walk_rpcs = {}
            local stop_rpcs = 0
            local tags = {}
            local mock_player = {
                components = {},
                IsValid = function() return true end,
                HasTag = function(_, t) return tags[t] == true end,
                ClearBufferedAction = function() end,
            }
            fakeG.ThePlayer = mock_player
            local pc = {
                ismastersim = false,
                locomotor = nil, -- Lag Compensation OFF
                handler = true,
                inst = mock_player,
                directwalking = false,
                dragwalking = false,
                draggingonground = false,
                enabled = true,
                hudblocking = false,
                busy = false,
                IsEnabled = function(s) return s.enabled, s.hudblocking end,
                IsBusy = function(s) return s.busy end,
                IsControlPressed = function() return false end,
                CooldownRemoteController = function() end,
                OnControl = function() return nil end,
                RemoteDirectWalking = function(_, x, z) walk_rpcs[#walk_rpcs + 1] = { x = x, z = z } end,
                RemoteStopWalking = function() stop_rpcs = stop_rpcs + 1 end,
            }
            local ctrl_attached = false
            fakeG.TheInput.ControllerAttached = function() return ctrl_attached end
            hooks.comp["playercontroller"](pc)
            -- 1. Press W (CONTROL_MOVE_UP, down=true) -> IMMEDIATE RemoteDirectWalking!
            pc:OnControl(5, true)
            assert(#walk_rpcs == 1 and math.abs(walk_rpcs[1].z + 1.0) < 1e-5, "Immediate WASD walk RPC failed!")
            assert(pc.directwalking == true, "directwalking should be true")
            -- 2. Release W (down=false) -> IMMEDIATE RemoteStopWalking!
            pc:OnControl(5, false)
            assert(stop_rpcs == 1 and pc.directwalking == false, "Immediate WASD stop RPC failed!")
            -- 3. Lag Comp ON (locomotor ~= nil) -> MUST NOT fire!
            pc.locomotor = {}
            pc:OnControl(5, true)
            assert(#walk_rpcs == 1, "Must NOT fire when Lag Comp is ON!")
            pc.locomotor = nil
            -- 4. Gamepad attached -> MUST bypass binary snap (handled natively in OnUpdate)!
            ctrl_attached = true
            pc:OnControl(8, true)
            assert(#walk_rpcs == 1, "Must bypass binary WASD snap when gamepad attached!")
            ctrl_attached = false
            -- 5. Busy / Disabled / SteeringBoat guards -> MUST NOT fire!
            pc.busy = true
            pc:OnControl(8, true)
            assert(#walk_rpcs == 1, "Must NOT start walking while busy!")
            pc.busy = false
            pc.enabled = false
            pc.hudblocking = false
            pc:OnControl(8, true)
            assert(#walk_rpcs == 1, "Must NOT walk when controller disabled (chat/menu)!")
            -- 6. Crafting Menu open (enabled=false, hudblocking=true) -> MUST allow walking!
            pc.hudblocking = true
            pc:OnControl(8, true)
            assert(#walk_rpcs == 2, "Should allow 240Hz WASD while crafting menu open (ishudblocking=true)!")
            pc:OnControl(8, false)
            assert(stop_rpcs == 2, "Should stop cleanly while crafting menu open!")
            pc.enabled = true
            pc.hudblocking = false
            tags["steeringboat"] = true
            pc:OnControl(8, true)
            assert(#walk_rpcs == 2, "Must NOT walk while steering boat!")
            tags["steeringboat"] = nil

            -- 7. [v2.6.0 Pillar 2] 240Hz Fresh Flick-Click Action Resolution (eliminates 0-33.3ms stale click misses!)
            local cur_mx, cur_my = 100, 100
            local cur_hover_ent = nil
            local cur_hud_ent = nil
            local input_upd_cnt = 0
            local picker_calls = 0
            local lmb_seen_at_click = nil
            local rmb_seen_at_click = nil
            local spider = { prefab = "spider" }
            local berry = { prefab = "berrybush" }
            fakeG.TheSim.GetPosition = function() return cur_mx, cur_my end
            fakeG.TheInput.OnUpdate = function()
                input_upd_cnt = input_upd_cnt + 1
                if cur_mx == 260 and cur_my == 180 then cur_hover_ent = spider
                elseif cur_mx == 320 and cur_my == 220 then cur_hover_ent = berry
                else cur_hover_ent = nil end
            end
            fakeG.TheInput.GetWorldEntityUnderMouse = function() return cur_hover_ent end
            fakeG.TheInput.GetHUDEntityUnderMouse = function() return cur_hud_ent end
            mock_player.IsActionsVisible = function() return true end
            mock_player.components.playeractionpicker = {
                DoGetMouseActions = function()
                    picker_calls = picker_calls + 1
                    if cur_hover_ent == spider then
                        return { action = "ATTACK", target = spider }, nil
                    elseif cur_hover_ent == berry then
                        return { action = "PICK", target = berry }, { action = "DIG", target = berry }
                    end
                    return { action = "WALKTO", target = nil }, nil
                end
            }
            local pc2 = {
                ismastersim = false, locomotor = nil, handler = true, inst = mock_player,
                enabled = true, hudblocking = false, placer_recipe = nil, deployplacer = nil,
                LMBaction = { action = "WALKTO", target = nil }, RMBaction = nil,
                UsingMouse = function() return true end,
                IsEnabled = function(s) return s.enabled, s.hudblocking end,
                IsAOETargeting = function() return false end,
                OnControl = function() return nil end,
                OnUpdate = function() end,
                OnLeftClick = function(s, down) if down then lmb_seen_at_click = s.LMBaction end end,
                OnRightClick = function(s, down) if down then rmb_seen_at_click = s.RMBaction end end,
            }
            hooks.comp["playercontroller"](pc2)
            -- Record baseline SimTick at (100,100) with stale LMBaction = WALKTO
            pc2:OnUpdate(1/30)
            -- Stationary click at (100,100): ZERO redundant DoGetMouseActions!
            pc2:OnLeftClick(true)
            assert(picker_calls == 0 and lmb_seen_at_click.action == "WALKTO", "Stationary click should not waste picker call")
            -- Flick mouse to spider (260,180) WITHOUT calling OnUpdate (mid-30Hz tick!), then LMB click!
            cur_mx, cur_my = 260, 180
            pc2:OnLeftClick(true)
            assert(input_upd_cnt == 1 and picker_calls == 1, "Flick-click failed to refresh hover & mouse actions!")
            assert(lmb_seen_at_click ~= nil and lmb_seen_at_click.action == "ATTACK" and lmb_seen_at_click.target == spider,
                   "Flick-click fired stale WALKTO instead of ATTACK(spider)!")
            -- Flick mouse to berrybush (320,220) and RMB click -> refreshes RMBaction to DIG!
            cur_mx, cur_my = 320, 220
            pc2:OnRightClick(true)
            assert(picker_calls == 2 and rmb_seen_at_click ~= nil and rmb_seen_at_click.action == "DIG",
                   "RMB flick-click failed to refresh to DIG!")
            -- HUD hover guard: must NOT refresh when hovering HUD!
            cur_mx, cur_my = 400, 400
            cur_hud_ent = {}
            pc2:OnLeftClick(true)
            assert(picker_calls == 2, "Must skip flick-click refresh when hovering HUD!")
            cur_hud_ent = nil
            -- deployplacer RMB guard: must skip refresh on RMB when deployplacer active!
            pc2.deployplacer = {}
            pc2:OnRightClick(true)
            assert(picker_calls == 2, "Must skip RMB flick-click refresh when deployplacer active!")
            pc2.deployplacer = nil
            -- Verify c_hslatency() runs cleanly without error
            fakeG.c_hslatency()

            -- 8. [v2.6.0 Pillar 1] 240Hz Phase-Locked Lag-Comp-OFF FollowCamera Smoothing
            assert(type(hooks.class["cameras/followcamera"]) == "function", "FollowCamera hook missing!")
            local raw_px, raw_py, raw_pz = 0.0, 0.0, 0.0
            mock_player.Transform = { GetWorldPosition = function() return raw_px, raw_py, raw_pz end }
            local cam_apply_calls = 0
            local cam = {
                target = mock_player,
                paused = false,
                cutscene = false,
                pangain = 4.0, headinggain = 20.0, distancegain = 1.0, continuousdistancegain = 1.0,
                currentpos = { x = 0.0, y = 0.0, z = 0.0 },
                targetpos = { x = 0.0, y = 0.0, z = 0.0 },
                targetoffset = { x = 0.0, y = 0.0, z = 0.0 },
                Apply = function() cam_apply_calls = cam_apply_calls + 1 end,
                Snap = function(c)
                    local x, y, z = c.target.Transform:GetWorldPosition()
                    c.targetpos.x, c.targetpos.y, c.targetpos.z = x, y, z
                    c.currentpos.x, c.currentpos.y, c.currentpos.z = x, y, z
                end,
                Update = function(c, dt, dontupdatepos)
                    if not dontupdatepos then
                        local x, y, z = c.target.Transform:GetWorldPosition()
                        c.targetpos.x = x + c.targetoffset.x
                        c.targetpos.y = y + c.targetoffset.y
                        c.targetpos.z = z + c.targetoffset.z
                        local p = dt * c.pangain
                        c.currentpos.x = c.currentpos.x + (c.targetpos.x - c.currentpos.x) * p
                        c.currentpos.y = c.currentpos.y + (c.targetpos.y - c.currentpos.y) * p
                        c.currentpos.z = c.currentpos.z + (c.targetpos.z - c.currentpos.z) * p
                    end
                    c:Apply()
                end,
            }
            hooks.class["cameras/followcamera"](cam)
            -- Init at (0,0,0), then simulate 30Hz server step -> 0.20 while rendering 240Hz frames!
            cam:Update(1/240)
            raw_px = 0.20
            cam:Update(1/240) -- frame 0 of step: alpha=0 -> smoothed targetpos.x == 0.0
            assert(math.abs(cam.targetpos.x - 0.0) < 1e-4, "Frame 0 of 30Hz step should start at prev=0, got " .. tostring(cam.targetpos.x))
            cam:Update(1/240) -- frame 1: alpha=1/8 -> smoothed targetpos.x == 0.025
            assert(math.abs(cam.targetpos.x - 0.025) < 1e-3, "Frame 1 should interpolate to ~0.025, got " .. tostring(cam.targetpos.x))
            cam:Update(1/240) -- frame 2: alpha=2/8 -> smoothed targetpos.x == 0.050
            assert(math.abs(cam.targetpos.x - 0.050) < 1e-3, "Frame 2 should interpolate to ~0.050, got " .. tostring(cam.targetpos.x))
            -- Teleport >5 units -> MUST hard snap immediately!
            raw_px = 12.0
            cam:Update(1/240)
            assert(math.abs(cam.targetpos.x - 12.0) < 1e-4, "Teleport >5u must hard snap, got " .. tostring(cam.targetpos.x))
            -- Snap() resets tracker cleanly
            cam:Snap()
            assert(math.abs(cam.currentpos.x - 12.0) < 1e-4, "Snap() failed")
            -- Lag Comp ON (locomotor ~= nil) -> MUST NOT alter targetpos!
            mock_player.components.locomotor = {}
            raw_px = 12.20
            cam:Update(1/240)
            assert(math.abs(cam.targetpos.x - 12.20) < 1e-4, "Lag Comp ON must bypass FollowCamera 240Hz smoother!")
            mock_player.components.locomotor = nil
        end
    end

    RunModMainScenario({ target_fps = 240, show_fps_hud = true, show_1pct_low = true, auto_60hz_sim = true }, false, "VANILLA")
    print("[CLIENT_V24_TEST] 2. Vanilla/Public isolation + 240Hz Immediate WASD RPC + all safety guards PASS")

    RunModMainScenario({ target_fps = 240, show_fps_hud = true, show_1pct_low = true, auto_60hz_sim = true }, true, "TAG")
    print("[CLIENT_V24_TEST] 3. HyperCore 60Hz WorldNetTag handshake PASS (60Hz active + [60Hz] badge + dynamic GetTime)")

    RunModMainScenario({ target_fps = 240, show_fps_hud = true, show_1pct_low = true, auto_60hz_sim = true }, true, "RPC")
    RunModMainScenario({ target_fps = 240, show_fps_hud = true, show_1pct_low = true, auto_60hz_sim = true }, true, "COLOAD_RPC")
    print("[CLIENT_V24_TEST] 4. HyperCore 60Hz ClientModRPC & co-load slot-id=1 safety PASS")

    RunModMainScenario({ target_fps = 240, show_fps_hud = true, show_1pct_low = true, auto_60hz_sim = false }, false, "TAG")
    RunModMainScenario({ target_fps = 240, show_fps_hud = true, show_1pct_low = true, auto_60hz_sim = false }, false, "RPC")
    print("[CLIENT_V24_TEST] 5. User opt-out (auto_60hz_sim=false) PASS (ignores 60Hz handshake, stays 30Hz)")

    -- =========================================================================
    -- TEST 6: Cross-SimReset Cave <-> Surface Zero-Reload & Zero Double-Unload
    -- =========================================================================
    do
        local load_log, unload_log = {}, {}
        local sim_methods = {
            GetTick = function() return 10 end,
            GetStaticTick = function() return 10 end,
            GetTickTime = function() return 1/30 end,
            GetPersistentString = function(_, _, cb) cb(false, "") end,
            SetPersistentString = function() end,
            GetPosition = function() return 0, 0 end,
            LoadPrefabs = function(_, list) load_log[#load_log + 1] = list end,
            UnloadPrefabs = function(_, list) unload_log[#unload_log + 1] = list end,
        }
        local sim_obj = setmetatable({}, { __index = sim_methods })
        local carried_params = nil
        local function BootVmWithSettings(settings_tbl)
            local fakeG2 = {
                pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
                getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
                math = G.math, string = G.string, table = G.table,
                tonumber = G.tonumber, tostring = G.tostring, print = G.print,
                Settings = settings_tbl,
                TheNet = { GetIsServer = function() return false end },
                TheSim = sim_obj,
                TheInput = { OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end, AddMouseButtonHandler = function() end },
                TheFrontEnd = { GetActiveScreen = function() return { name = "HUD" } end },
                SimReset = function(ip) carried_params = ip end,
                require = function() return {} end,
            }
            -- Restore fresh metatable __index before each simulated VM boot
            local fresh_idx = {}
            for k, v in pairs(sim_methods) do fresh_idx[k] = v end
            setmetatable(sim_obj, { __index = fresh_idx })
            local env2 = {
                GLOBAL = fakeG2,
                MODROOT = "../mods/dst_smooth_highfps/",
                Asset = function(t, f) return { type = t, file = f } end,
                GetModConfigData = function(k)
                    if k == "target_fps" then return 240 end
                    return k == "keep_shard_assets" and true or false
                end,
                AddClassPostConstruct = function() end,
                AddComponentPostInit = function() end,
                AddPrefabPostInit = function() end,
                AddSimPostInit = function() end,
                AddModShadersInit = function() end,
                AddModShadersSortAndEnable = function() end,
                AddClientModRPCHandler = function() end,
            }
            local fn_m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
            setfenv(fn_m, env2)
            fn_m()
            return fakeG2, env2
        end

        -- 1) Enter Forest from Main Menu
        local s1 = { current_asset_set = "BACKEND", last_asset_set = "FRONTEND", loaded_characters = { "wilson", "wendy" } }
        local g1 = BootVmWithSettings(s1)
        sim_obj:LoadPrefabs({ "forest" })
        assert(#load_log == 1 and load_log[1][1] == "forest", "1st forest load must execute")

        -- 2) Migrate Forest -> Cave (SimReset must carry _hakimi_kept_worlds!)
        g1.SimReset({})
        assert(carried_params ~= nil and carried_params._hakimi_kept_worlds.forest == true, "SimReset failed to persist kept worlds!")
        local s2 = {
            current_asset_set = "BACKEND", last_asset_set = "BACKEND",
            loaded_characters = { "wilson", "wendy" },
            _hakimi_kept_worlds = carried_params._hakimi_kept_worlds,
            _hakimi_kept_chars  = carried_params._hakimi_kept_chars,
        }
        local g2 = BootVmWithSettings(s2)
        sim_obj:UnloadPrefabs(s2.loaded_characters) -- gamelogic L196: MUST skip!
        sim_obj:LoadPrefabs({ "cave" })             -- gamelogic L281: 1st cave load executes
        sim_obj:UnloadPrefabs({ "forest" })         -- gamelogic L284: MUST skip!
        sim_obj:LoadPrefabs({ "wilson", "wendy" })  -- gamelogic L426: MUST skip!
        assert(#load_log == 2 and load_log[2][1] == "cave", "Expected only 1st cave load, got " .. tostring(#load_log))
        assert(#unload_log == 0, "Expected 0 unloads during BACKEND->BACKEND, got " .. tostring(#unload_log))

        -- 3) Return Cave -> Forest (2nd+ trip: ZERO loads, ZERO unloads!)
        g2.SimReset({})
        local s3 = {
            current_asset_set = "BACKEND", last_asset_set = "BACKEND",
            loaded_characters = { "wilson", "wendy" },
            _hakimi_kept_worlds = carried_params._hakimi_kept_worlds,
            _hakimi_kept_chars  = carried_params._hakimi_kept_chars,
        }
        local g3 = BootVmWithSettings(s3)
        sim_obj:UnloadPrefabs(s3.loaded_characters)
        sim_obj:LoadPrefabs({ "forest" })           -- MUST SKIP (already hot!)
        sim_obj:UnloadPrefabs({ "cave" })
        sim_obj:LoadPrefabs({ "wilson", "wendy" })  -- MUST SKIP (already hot!)
        assert(#load_log == 2, "2nd forest return should trigger ZERO LoadPrefabs, got " .. tostring(#load_log))
        assert(#unload_log == 0, "2nd forest return should trigger ZERO UnloadPrefabs")

        -- 4) Exit to Main Menu (FRONTEND): MUST unload forest, cave, and characters with ZERO double-unload!
        g3.SimReset({})
        local s4 = {
            current_asset_set = "FRONTEND", last_asset_set = "BACKEND",
            loaded_characters = { "wilson", "wendy" },
            _hakimi_kept_worlds = carried_params._hakimi_kept_worlds,
            _hakimi_kept_chars  = carried_params._hakimi_kept_chars,
        }
        BootVmWithSettings(s4)
        sim_obj:UnloadPrefabs(s4.loaded_characters)
        sim_obj:UnloadPrefabs({ "forest" })
        local unloaded_counts = {}
        for _, batch in ipairs(unload_log) do
            for _, name in ipairs(batch) do
                unloaded_counts[name] = (unloaded_counts[name] or 0) + 1
            end
        end
        assert(unloaded_counts.forest == 1 and unloaded_counts.cave == 1, "Both worlds must unload exactly once!")
        assert(unloaded_counts.wilson == 1 and unloaded_counts.wendy == 1, "Both chars must unload exactly once!")
        print("[CLIENT_V24_TEST] 6. Cross-SimReset Cave<->Surface zero-reload & clean FRONTEND unload PASS")
    end

    -- =========================================================================
    -- TEST 7: Live Any-Key & Mouse-Button Binding (keybind.lua + HS_ApplyKeyBind)
    -- =========================================================================
    do
        local key_handlers, mouse_handlers = {}, {}
        local native_fps = 240
        local fakeG3 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print,
            HyperSmooth_SetFps = function(v) native_fps = v; return true end,
            HyperSmooth_SetSkeletal = function() return true end,
            ThePlayer = { HUD = {} },
            TheFrontEnd = { GetActiveScreen = function() return { name = "HUD" } end },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
            },
            TheInput = {
                OnUpdate = function() end,
                IsKeyDown = function() return false end,
                AddKeyUpHandler = function(_, k, fn)
                    key_handlers[k] = fn
                    return { Remove = function() key_handlers[k] = nil end }
                end,
                AddMouseButtonHandler = function(_, fn)
                    mouse_handlers[#mouse_handlers + 1] = fn
                    local idx = #mouse_handlers
                    return { Remove = function() mouse_handlers[idx] = nil end }
                end,
            },
            require = function() return {} end,
        }
        local env3 = {
            GLOBAL = fakeG3,
            GetModConfigData = function(k) return k == "target_fps" and 240 or false end,
            AddClassPostConstruct = function() end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function() end,
            AddClientModRPCHandler = function() end,
        }
        local fn_m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_m, env3)
        fn_m()
        assert(type(env3.HS_ApplyKeyBind) == "function", "HS_ApplyKeyBind missing!")
        -- Bind cycle FPS to Keyboard KEY_F8 (289), fire -> 240 -> 360!
        env3.HS_ApplyKeyBind("hotkey_cycle", 289)
        assert(type(key_handlers[289]) == "function", "Keyboard key handler not registered!")
        key_handlers[289]()
        assert(native_fps == 360, "Expected cycle 240->360 on KEY_F8, got " .. tostring(native_fps))
        -- Live rebind to Mouse Button 4 (1005): old KEY_F8 handler MUST be removed, mouse 1005 cycles 360->0!
        env3.HS_ApplyKeyBind("hotkey_cycle", 1005)
        assert(key_handlers[289] == nil, "Old KEY_F8 handler was not removed on rebind!")
        mouse_handlers[1](1005, true)
        assert(native_fps == 0, "Expected cycle 360->0 on Mouse Button 4 (1005), got " .. tostring(native_fps))
        -- Verify keybind.lua compiles cleanly in Klei Lua 5.1 VM!
        local fn_kb, err_kb = G.kleiloadlua("../mods/client_v24_lua_test/target_keybind.lua")
        assert(fn_kb ~= nil, "keybind.lua syntax/load error: " .. tostring(err_kb))
        print("[CLIENT_V24_TEST] 7. Live Any-Key & Mouse-Button rebinding + keybind.lua Lua 5.1 syntax PASS")
    end

    -- =========================================================================
    -- TEST 8: Sleek In-Game Live Customizer Side-Panel (hs_panel.lua + c_hsmenu)
    -- =========================================================================
    do
        local native_af, native_full_lm, native_mip_bias, native_lowlat, native_skel = 1, false, 0.0, true, true
        local native_hd2d = nil
        local function MakeMockWidget()
            local w = { visible = true, children = {}, inst = { IsValid = function() return true end } }
            function w:AddChild(c) self.children[#self.children + 1] = c; return c end
            function w:SetHAnchor() end
            function w:SetVAnchor() end
            function w:SetScaleMode() end
            function w:SetPosition(x, y, z) self._pos = { x, y, z } end
            function w:ScaleToSize() end
            function w:SetTint() end
            function w:SetColour() end
            function w:SetRegionSize() end
            function w:SetHAlign() end
            function w:SetClickable() end
            function w:ForceImageSize() end
            function w:SetText(t) self._txt = t end
            function w:SetString(s) self._str = s end
            function w:SetTextSize() end
            function w:SetFont() end
            function w:SetTextColour() end
            function w:SetTextFocusColour() end
            function w:SetOnClick(fn) self._onclick = fn end
            function w:Show() self.visible = true end
            function w:Hide() self.visible = false end
            function w:MoveToFront() end
            return w
        end
        local mock_hud = MakeMockWidget()
        local fakeG4 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print,
            BODYTEXTFONT = "bodyfont", NUMBERFONT = "numfont",
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, ANCHOR_TOP = 1,
            SCALEMODE_PROPORTIONAL = 2,
            STRINGS = { UI = { OPTIONS = { ENABLED = "启用", DISABLED = "禁用" } } },
            ThePlayer = { HUD = mock_hud },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
            },
            TheInput = { OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_SetSkeletal = function(v) native_skel = v; return true end,
            HyperSmooth_SetVfxSmooth = function() return true end,
            HyperSmooth_SetLowLatency = function(v) native_lowlat = v; return true end,
            HyperSmooth_SetVisual = function(af, lm, mip) native_af = af; native_full_lm = lm; native_mip_bias = mip or 0.0; return true end,
            HyperSmooth_SetHd2d = function(...) native_hd2d = {...}; return true end,
            HyperSmooth_GetFrameStats = function() return 240.0, 228.0, 4.18, 0.03, 0, 30 end,
            HyperSmooth_GetVisualPerf = function() return true, 12, 1.0, 1.4, 0.2, 2.0, 0.3, 440, 450, 7200 end,
            HyperSmooth_GetVisualStats = function() return native_af, 16, 10, true, native_full_lm and 1.0 or 0.5, true, native_mip_bias, 5 end,
            HyperSmooth_GetLatencyStats = function() return native_lowlat, native_lowlat and 1 or 3, 3, 0.3, 20, true, 10, 500, true, native_mip_bias, true, 200, true end,
            require = function(p)
                if p == "widgets/widget" or p == "widgets/image" or p == "widgets/imagebutton" or p == "widgets/text" then
                    return function() return MakeMockWidget() end
                end
                return {}
            end,
        }
        local env4 = nil
        env4 = {
            GLOBAL = fakeG4,
            GetModConfigData = function(k)
                if k == "target_fps" then return 240 end
                if k == "skeletal_tween" or k == "low_latency_input" then return true end
                return false
            end,
            AddClassPostConstruct = function() end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function() end,
            AddClientModRPCHandler = function() end,
            modimport = function(name)
                if name == "hs_panel" then
                    local fn_p, err_p = G.kleiloadlua("../mods/client_v24_lua_test/target_hs_panel.lua")
                    assert(fn_p ~= nil, "hs_panel.lua syntax/load error: " .. tostring(err_p))
                    setfenv(fn_p, env4)
                    fn_p()
                end
            end,
        }
        local fn_m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_m, env4)
        fn_m()
        assert(type(fakeG4.c_hsmenu) == "function", "c_hsmenu() global not registered!")
        assert(type(env4.HS_TogglePanel) == "function" and type(env4.HS_PanelApi) == "table", "HS_PanelApi / HS_TogglePanel missing!")
        fakeG4.c_hsmenu(true)
        assert(#mock_hud.children == 1 and mock_hud.children[1].visible == true, "Live panel failed to build & show on PlayerHUD!")
        assert(mock_hud.children[1]._pos == nil, "Proportional anchor root MUST remain at (0,0,0) to prevent raw-pixel clipping!")
        local body = mock_hud.children[1].children[1]
        assert(body ~= nil and body._pos ~= nil and body._pos[1] <= -210,
               "Panel body proportional X offset missing or too far right: " .. tostring(body and body._pos and body._pos[1]))
        -- Toggle 1.0x LightMap via panel API -> sets native_full_lm=true & light_dither=1.0!
        env4.HS_PanelApi.ToggleVisualFlag("full_lm")
        assert(native_full_lm == true and native_hd2d ~= nil and math.abs(native_hd2d[7] - 1.0) < 1e-4, "Panel full_lm toggle failed!")
        -- Toggle Low Latency OFF and back ON via panel API
        env4.HS_PanelApi.ToggleLowLatency()
        assert(native_lowlat == false, "Panel ToggleLowLatency OFF failed!")
        env4.HS_PanelApi.ToggleLowLatency()
        assert(native_lowlat == true, "Panel ToggleLowLatency ON failed!")
        -- Toggle 240Hz Smooth OFF and back ON via panel API
        env4.HS_PanelApi.ToggleSkeletal()
        assert(native_skel == false, "Panel ToggleSkeletal OFF failed!")
        env4.HS_PanelApi.ToggleSkeletal()
        assert(native_skel == true, "Panel ToggleSkeletal ON failed!")
        -- 1-click Restore Vanilla Pure via panel API
        -- Cycle HUD mode through all 3 states (initially false -> simple -> detail -> hidden -> simple)
        assert(type(env4.HS_PanelApi.CycleHudMode) == "function", "HS_PanelApi.CycleHudMode missing!")
        env4.HS_PanelApi.CycleHudMode() -- hidden -> simple
        assert(env4.HS_PanelApi.GetSnapshot().hud_show == true and env4.HS_PanelApi.GetSnapshot().hud_mode == "simple",
               "Expected simple HUD after 1st cycle from hidden")
        env4.HS_PanelApi.CycleHudMode() -- simple -> detail
        assert(env4.HS_PanelApi.GetSnapshot().hud_mode == "detail", "Expected detail HUD after 2nd cycle")
        fakeG4.c_hshud("en")
        env4.HS_PanelNotifySync()
        assert(env4.HS_PanelApi.GetSnapshot().ui_zh == false, "Expected EN UI after c_hshud('en')")
        fakeG4.c_hshud("auto", "simple")
        env4.HS_PanelNotifySync()
        assert(env4.HS_PanelApi.GetSnapshot().ui_zh == true and env4.HS_PanelApi.GetSnapshot().hud_mode == "simple",
               "Expected auto ZH compact restored")
        env4.HS_PanelApi.SetPreset(0)
        assert(native_af == 1 and native_full_lm == false and math.abs(native_mip_bias) < 1e-4 and math.abs(native_hd2d[7]) < 1e-4, "Panel 1-click Vanilla Restore failed!")
        env4.HS_PanelTickTelemetry()
        fakeG4.c_hsmenu(false)
        assert(mock_hud.children[1].visible == false, "Live panel failed to hide!")
        print("[CLIENT_V24_TEST] 8. Sleek In-Game Live Customizer Side-Panel (hs_panel.lua + c_hsmenu + 1-click Vanilla Restore) PASS")
    end

    -- =========================================================================
    -- TEST 9: Combat Low-Latency Walk-Cancel, LowLat OFF Parity & Ctrl+LMB Modifier Rescue
    -- =========================================================================
    do
        local rpc_walk, rpc_stop, click_actions = {}, 0, {}
        local cur_mods = 0
        local cur_mx, cur_my = 400, 300
        local hover_ent = { prefab = "beefalo" }
        local mkvec
        mkvec = function(x, y, z)
            return setmetatable({ x = x, y = y, z = z }, {
                __mul = function(a, s) return mkvec(a.x * s, a.y * s, a.z * s) end,
                __sub = function(a, b) return mkvec(a.x - b.x, a.y - b.y, a.z - b.z) end,
                __index = {
                    GetNormalized = function(v)
                        local l = math.sqrt(v.x*v.x + v.y*v.y + v.z*v.z)
                        return l > 0 and mkvec(v.x/l, v.y/l, v.z/l) or mkvec(0, 0, 0)
                    end
                }
            })
        end
        local analog_vals = {}
        local fakePlayer
        fakePlayer = {
            IsValid = function() return true end,
            IsActionsVisible = function() return true end,
            HasTag = function() return false end,
            GetBufferedAction = function(self) return self._ba end,
            ClearBufferedAction = function(self) self._ba = nil end,
            components = {
                playeractionpicker = {
                    DoGetMouseActions = function()
                        if cur_mods == 4 then
                            return { action = "ATTACK", target = hover_ent }, nil
                        end
                        return { action = "WALKTO", target = nil }, nil
                    end,
                },
            },
        }
        local fakeG5 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print,
            CONTROL_MOVE_UP = 5, CONTROL_MOVE_DOWN = 6, CONTROL_MOVE_LEFT = 7, CONTROL_MOVE_RIGHT = 8,
            CONTROL_CONTROLLER_ATTACK = 39,
            TUNING = { CONTROLLER_DEADZONE_RADIUS = 0.3 },
            IsPaused = function() return false end,
            ThePlayer = fakePlayer,
            TheCamera = {
                GetRightVec = function() return mkvec(1, 0, 0) end,
                GetDownVec  = function() return mkvec(0, 0, 1) end,
            },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                GetPosition = function() return cur_mx, cur_my end,
            },
            TheInput = {
                OnUpdate = function() end,
                IsKeyDown = function() return false end,
                AddKeyUpHandler = function() end,
                ControllerAttached = function() return false end,
                GetHUDEntityUnderMouse = function() return nil end,
                GetWorldEntityUnderMouse = function() return hover_ent end,
                GetAnalogControlValue = function(_, c) return analog_vals[c] or 0 end,
            },
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_SetSkeletal = function() return true end,
            HyperSmooth_SetVfxSmooth = function() return true end,
            HyperSmooth_SetLowLatency = function() return true end,
            HyperSmooth_SetVisual = function() return true end,
            HyperSmooth_SetHd2d = function() return true end,
            require = function() return {} end,
        }
        local pc_post = nil
        local env5 = {
            GLOBAL = fakeG5,
            GetModConfigData = function(k)
                if k == "target_fps" then return 240 end
                if k == "low_latency_input" then return true end
                return false
            end,
            AddClassPostConstruct = function() end,
            AddComponentPostInit = function(name, fn)
                if name == "playercontroller" then pc_post = fn end
            end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function() end,
            AddClientModRPCHandler = function() end,
        }
        local fn_m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_m, env5)
        fn_m()
        assert(type(pc_post) == "function", "playercontroller postinit not registered!")
        local pc = {
            inst = fakePlayer, ismastersim = false, locomotor = nil, handler = {},
            directwalking = false, dragwalking = false, draggingonground = false,
            recent_bufferedaction = {},
            LMBaction = { action = "WALKTO", target = nil },
            RMBaction = nil,
            UsingMouse = function() return true end,
            IsEnabled = function() return true, false end,
            IsBusy = function() return false end,
            IsLocalOrRemoteHopping = function() return false end,
            IsAOETargeting = function() return false end,
            IsControlPressed = function() return false end,
            CooldownRemoteController = function() end,
            EncodeControlMods = function() return cur_mods end,
            RemoteDirectWalking = function(_, x, z) rpc_walk[#rpc_walk + 1] = {x, z} end,
            RemoteStopWalking = function() rpc_stop = rpc_stop + 1 end,
            OnControl = function() end,
            OnUpdate = function() end,
            OnLeftClick = function(self, down)
                if down then click_actions[#click_actions + 1] = self.LMBaction and self.LMBaction.action or "NONE" end
            end,
            OnRightClick = function() end,
        }
        pc_post(pc)
        -- 9a) Baseline OnUpdate caches (400, 300, beefalo, mods=0 -> WALKTO)
        pc:OnUpdate(1/30)
        -- 9b) Hold Ctrl (cur_mods=4) WITHOUT moving mouse and click LMB -> MUST rescue to ATTACK!
        cur_mods = 4
        pc:OnLeftClick(true)
        assert(click_actions[1] == "ATTACK", "Stationary Ctrl+LMB Force-Attack failed to refresh from stale WALKTO, got " .. tostring(click_actions[1]))
        -- 9c) Walk-cancel directional guard: same-dir recent_bufferedaction is protected, new dodge dir fires immediately!
        local ba = { action = "ATTACK" }
        fakePlayer._ba = ba
        pc.recent_bufferedaction = { act = ba, x = 1, z = 0 } -- right (+1, 0)
        analog_vals[8] = 1
        pc:OnControl(8, true) -- press RIGHT (same dir) -> protected!
        assert(#rpc_walk == 0 and pc.recent_bufferedaction.act == ba, "Same-dir late release guard violated!")
        analog_vals[8] = 0
        analog_vals[7] = 1
        pc:OnControl(7, true) -- press LEFT (-1, 0) dodge -> MUST walk-cancel immediately!
        assert(#rpc_walk == 1 and rpc_walk[1][1] < -0.9 and pc.recent_bufferedaction.act == nil, "Walk-cancel new direction was stalled!")
        analog_vals[7] = 0
        pc:OnControl(7, false) -- release LEFT -> MUST stop walking immediately!
        assert(rpc_stop == 1 and pc.directwalking == false, "Instant StopWalking failed!")
        -- 9d) Toggle Low Latency OFF -> 9b WASD immediate dispatch MUST restore 100% vanilla (0 instant RPCs)!
        env5.HS_PanelApi.ToggleLowLatency()
        analog_vals[8] = 1
        pc:OnControl(8, true)
        assert(#rpc_walk == 1, "Low Latency OFF failed to disable 9b WASD immediate dispatch!")
        print("[CLIENT_V24_TEST] 9. Combat Low-Latency (Ctrl+LMB Modifier Rescue + Walk-Cancel Dir Guard + LowLat OFF Parity) PASS")
    end

    -- =========================================================================
    -- TEST 10: CraftingMenu Zero-Stall (O(slots) Snapshot + Mod-Safe Exact Memo
    --          + Closed-Menu Hidden-UI Deferral & Open() Flush + Real Speedup!)
    -- =========================================================================
    do
        local class_hooks = {}
        local fakeG10 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error, debug = G.debug,
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                GetTick = function() return 10 end,
                GetTickTime = function() return 1/30 end,
                GetPosition = function() return 0, 0 end,
            },
            TheInput = { OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            require = function() return {} end,
        }
        local env10 = {
            GLOBAL = fakeG10,
            GetModConfigData = function(k)
                if k == "reduce_240hz_churn" then return true end
                if k == "smooth_ui_240hz" then return true end
                if k == "skeletal_tween" then return true end
                return false
            end,
            AddClassPostConstruct = function(cls, fn) class_hooks[cls] = fn end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function() end,
            AddClientModRPCHandler = function() end,
        }
        local fn_m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_m, env10)
        fn_m()
        assert(type(class_hooks["widgets/redux/craftingmenu_hud"]) == "function", "craftingmenu_hud hook missing!")
        assert(type(class_hooks["widgets/redux/craftingmenu_widget"]) == "function", "craftingmenu_widget hook missing!")

        -- Build 600 recipes over 40 ingredient prefabs
        local prefabs = {}
        for i = 1, 40 do prefabs[i] = "mat_" .. i end
        local recipes = {}
        for r = 1, 600 do
            local ing = {}
            for j = 1, 3 do
                ing[j] = { type = prefabs[((r * 7 + j * 11) % 40) + 1], amount = ((r + j) % 5) + 1 }
            end
            recipes["rec_" .. r] = { name = "rec_" .. r, ingredients = ing }
        end

        -- 10a) Simulate third-party "Craft From Nearby Chests" mod with EARLY-EXIT Has()!
        -- Nearby chest has 4 of mat_1, 1 of mat_2, 0 of mat_3... (early exits when found >= amount!)
        local mod_has_calls = 0
        local real_counts = { mat_1 = 4, mat_2 = 1, mat_5 = 3, mat_9 = 6, mat_12 = 2 }
        local function EarlyExitModHas(_, prefab, amount, checkall)
            mod_has_calls = mod_has_calls + 1
            local total = real_counts[prefab] or 0
            -- Simulate an early-exiting mod that stops scanning once found >= amount!
            local scanned = math.min(total, amount)
            return scanned >= amount, scanned
        end

        local inv_rep = { Has = EarlyExitModHas }
        local owner = { replica = { inventory = inv_rep } }
        local baseline_results = {}
        for name, rec in pairs(recipes) do
            local ok = true
            for _, ing in ipairs(rec.ingredients) do
                local h = EarlyExitModHas(inv_rep, ing.type, ing.amount, true)
                if not h then ok = false break end
            end
            baseline_results[name] = ok
        end
        local vanilla_calls = mod_has_calls
        mod_has_calls = 0

        local hud_results = {}
        local open_flushed = { apply = 0, grid = 0, details = 0, filter = 0 }
        local cm = {
            crafting_hud = nil,
            UpdateFilterButtons = function() open_flushed.filter = open_flushed.filter + 1 return true end,
            ApplyFilters = function() open_flushed.apply = open_flushed.apply + 1 end,
            recipe_grid = { RefreshView = function() open_flushed.grid = open_flushed.grid + 1 end },
            details_root = { Refresh = function() open_flushed.details = open_flushed.details + 1 end },
            OnCraftingMenuOpen = function() end,
            Refresh = function(self)
                local rb = self:UpdateFilterButtons()
                if rb then self:ApplyFilters() else self.recipe_grid:RefreshView() end
                self.details_root:Refresh()
            end,
        }
        local hud = {
            owner = owner,
            is_open = false,
            craftingmenu = cm,
            IsCraftingOpen = function(self) return self.is_open end,
            RebuildRecipes = function(self)
                for name, rec in pairs(recipes) do
                    local ok = true
                    for _, ing in ipairs(rec.ingredients) do
                        local h = self.owner.replica.inventory:Has(ing.type, ing.amount, true)
                        if not h then ok = false break end
                    end
                    hud_results[name] = ok
                end
            end,
            Open = function(self)
                self.is_open = true
                self.craftingmenu:OnCraftingMenuOpen()
            end,
        }
        cm.crafting_hud = hud
        class_hooks["widgets/redux/craftingmenu_hud"](hud)
        class_hooks["widgets/redux/craftingmenu_widget"](cm)

        -- Run RebuildRecipes with our hook + early-exiting third-party mod Has()
        hud:RebuildRecipes()
        assert(inv_rep.Has == EarlyExitModHas, "inv_rep.Has not restored after RebuildRecipes!")
        for name, exp in pairs(baseline_results) do
            assert(hud_results[name] == exp, "Early-exit mod memo mismatch on " .. name .. ": got " .. tostring(hud_results[name]) .. " exp " .. tostring(exp))
        end
        assert(mod_has_calls < vanilla_calls * 0.25, "Memoization did not drastically cut Has() calls: " .. mod_has_calls .. " vs " .. vanilla_calls)

        -- 10b) Closed-menu hidden UI deferral + Open() seamless flush
        cm:Refresh(false)
        cm:Refresh(false)
        cm:Refresh(false)
        assert(open_flushed.filter == 3, "UpdateFilterButtons should stay live while closed for lightbulb/ding!")
        assert(open_flushed.apply == 0 and open_flushed.grid == 0 and open_flushed.details == 0,
               "Hidden CraftingMenuWidget UI should be deferred while closed!")
        hud:Open()
        assert(open_flushed.apply == 1 and open_flushed.details == 1,
               "Deferred hidden UI did not flush cleanly on CraftingMenuHUD:Open()!")

        -- 10c) REAL Vanilla Inventory + Backpack + Open Chest O(slots) Snapshot Parity & Speedup!
        local RealInv = G.require("components/inventory")
        local RealInvRep = G.require("components/inventory_replica")
        local RealCont = G.require("components/container")
        local function mk_item(prefab, stack, nocraft)
            return {
                prefab = prefab,
                HasTag = function(_, tag) return nocraft and tag == "nocrafting" or false end,
                components = { stackable = stack > 1 and { StackSize = function() return stack end } or nil },
            }
        end
        local bp_slots = {}
        for i = 1, 14 do bp_slots[i] = mk_item(prefabs[((i * 3) % 40) + 1], (i % 4) + 1, false) end
        local bp = setmetatable({ slots = bp_slots }, { __index = RealCont })
        local chest_slots = {}
        for i = 1, 9 do chest_slots[i] = mk_item(prefabs[((i * 5) % 40) + 1], (i % 3) + 1, i == 3) end
        local chest_ok = setmetatable({ slots = chest_slots, excludefromcrafting = false, readonlycontainer = false }, { __index = RealCont })
        local chest_ro = setmetatable({ slots = { mk_item("mat_1", 99, false) }, readonlycontainer = true }, { __index = RealCont })
        local inv_slots = {}
        for i = 1, 15 do inv_slots[i] = mk_item(prefabs[((i * 2) % 40) + 1], (i % 5) + 1, i == 7) end
        local real_inv_cmp = setmetatable({
            itemslots = inv_slots,
            activeitem = mk_item("mat_1", 2, false),
            GetOverflowContainer = function() return bp end,
            opencontainers = {
                [{ components = { container = chest_ok } }] = true,
                [{ components = { container = chest_ro } }] = true,
            },
        }, { __index = RealInv })
        local real_owner = { components = { inventory = real_inv_cmp }, replica = {} }
        local real_inv_rep = setmetatable({ inst = real_owner }, { __index = RealInvRep })
        real_owner.replica.inventory = real_inv_rep

        -- Baseline vanilla 600-recipe pass
        local van_res = {}
        local t0 = G.GetTimeRealSeconds()
        for pass = 1, 30 do
            for name, rec in pairs(recipes) do
                local ok = true
                for _, ing in ipairs(rec.ingredients) do
                    local h = real_inv_rep:Has(ing.type, ing.amount, true)
                    if not h then ok = false break end
                end
                if pass == 1 then van_res[name] = ok end
            end
        end
        local dt_van = (G.GetTimeRealSeconds() - t0) * 1000.0

        -- Optimized O(slots) snapshot 600-recipe pass via our hooked CraftingMenuHUD!
        local opt_res = {}
        local hud2 = {
            owner = real_owner,
            is_open = true,
            IsCraftingOpen = function() return true end,
            RebuildRecipes = function(self)
                for name, rec in pairs(recipes) do
                    local ok = true
                    for _, ing in ipairs(rec.ingredients) do
                        local h = self.owner.replica.inventory:Has(ing.type, ing.amount, true)
                        if not h then ok = false break end
                    end
                    opt_res[name] = ok
                end
            end,
        }
        class_hooks["widgets/redux/craftingmenu_hud"](hud2)
        local t1 = G.GetTimeRealSeconds()
        for _ = 1, 30 do hud2:RebuildRecipes() end
        local dt_opt = (G.GetTimeRealSeconds() - t1) * 1000.0
        for name, exp in pairs(van_res) do
            assert(opt_res[name] == exp, "O(slots) snapshot parity mismatch on " .. name .. ": got " .. tostring(opt_res[name]) .. " exp " .. tostring(exp))
        end

        print(string.format("[CLIENT_V24_TEST] 10. CraftingMenu Zero-Stall PASS (600/600 vanilla O(slots) + early-exit mod memo exact parity, Has %d->%d, hidden UI deferred 3->0 & flushed on Open)",
            vanilla_calls, mod_has_calls))
    end

    -- =========================================================================
    -- TEST 11: TrueScrollArea 240Hz Parity + FollowText 240Hz Interp/Teleport
    --          + Solo/Local-Host DataDumper -> HyperSmooth_FastDump Bridge
    -- =========================================================================
    do
        local class_hooks = {}
        local sim_post = {}
        local screen_pos_calls = {}
        local fastdump_calls = 0
        local orig_dump_calls = 0
        local fakeG11 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error, debug = G.debug,
            TheWorld = { ismastersim = true },
            TheFrontEnd = { GetHUDScale = function() return 1.0 end },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                GetTick = function() return 10 end,
                GetTickTime = function() return 1/30 end,
                GetPosition = function() return 0, 0 end,
                GetScreenPos = function(_, x, y, z) return x * 10, z * 10 end,
            },
            TheInput = { OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            DataDumper = function(v, vn, fm, id) orig_dump_calls = orig_dump_calls + 1 return "orig" end,
            HyperSmooth_FastDump = function(v, vn) fastdump_calls = fastdump_calls + 1 return "fast" end,
            require = function() return {} end,
        }
        local env11 = {
            GLOBAL = fakeG11,
            GetModConfigData = function(k)
                if k == "smooth_ui_240hz" or k == "skeletal_tween" or k == "reduce_240hz_churn" then return true end
                return false
            end,
            AddClassPostConstruct = function(cls, fn) class_hooks[cls] = fn end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function(fn) sim_post[#sim_post + 1] = fn end,
            AddClientModRPCHandler = function() end,
        }
        local fn_m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_m, env11)
        fn_m()

        -- 11a) TrueScrollArea 4x 240Hz frames MUST equal 1x 60Hz frame (0.30 step)!
        assert(type(class_hooks["widgets/truescrollarea"]) == "function", "truescrollarea hook missing!")
        local area = {
            current_scroll_pos = 0, target_scroll_pos = 100, scroll_pos_end = 500,
            RefreshView = function() end,
            OnUpdate = function(self, dt)
                self.current_scroll_pos = self.current_scroll_pos * 0.7 + self.target_scroll_pos * 0.3
            end,
        }
        class_hooks["widgets/truescrollarea"](area)
        for _ = 1, 4 do area:OnUpdate(1/240) end
        assert(math.abs(area.current_scroll_pos - 30.0) < 0.15,
               "TrueScrollArea 4x 240Hz should equal 30.0 (1x 60Hz), got " .. tostring(area.current_scroll_pos))
        assert(area.target_scroll_pos == 100, "TrueScrollArea target_scroll_pos drifted!")

        -- 11b) FollowText 240Hz smooth interpolation + >5u teleport snap
        assert(type(class_hooks["widgets/followtext"]) == "function", "followtext hook missing!")
        local ent_x = 0.0
        local last_ft_x = 0
        local ft = {
            hashud = true,
            symbol = "",
            offset = { x = 0, y = 0, z = 0 },
            screen_offset = { x = 0, y = 0 },
            text = { SetScale = function() end },
            target = {
                IsValid = function() return true end,
                Transform = { GetWorldPosition = function() return ent_x, 0, 0 end },
            },
            SetPosition = function(_, x, y) last_ft_x = x end,
            SetTarget = function(self, t) self.target = t end,
            OnUpdate = function(self)
                local x, y = fakeG11.TheSim:GetScreenPos(self.target.Transform:GetWorldPosition())
                self:SetPosition(x, y)
            end,
        }
        class_hooks["widgets/followtext"](ft)
        ft:OnUpdate(1/240) -- init at x=0
        assert(last_ft_x == 0)
        for _ = 1, 7 do ft:OnUpdate(1/240) end -- advance ~33ms
        ent_x = 1.0 -- 30Hz sim step!
        ft:OnUpdate(1/240) -- right on step edge -> C0 smooth near 0
        local x_edge = last_ft_x
        ft:OnUpdate(1/240) -- mid-step -> MUST smoothly interpolate between 0 and 10!
        ft:OnUpdate(1/240)
        local x_mid = last_ft_x
        assert(x_mid > x_edge and x_mid < 9.5, "FollowText did not smoothly interpolate at 240Hz: edge=" .. x_edge .. " mid=" .. x_mid)
        -- Teleport >5u -> MUST hard-snap immediately!
        ent_x = 20.0
        ft:OnUpdate(1/240)
        assert(math.abs(last_ft_x - 200.0) < 0.01, "FollowText did not snap on >5u teleport: got " .. last_ft_x)

        -- 11c) Solo / Non-Caves Local Host DataDumper -> HyperSmooth_FastDump bridge + R3.1 fail-reason safety (#06)
        local fastdump_mode = "ok"
        local last_orig_fastmode = nil
        fakeG11.error = G.error
        fakeG11.HyperSmooth_FastDump = function(val, vname)
            fastdump_calls = fastdump_calls + 1
            if fastdump_mode == "ok" then return "fast" end
            return nil, fastdump_mode
        end
        fakeG11.DataDumper = function(val, vname, fm, id)
            orig_dump_calls = orig_dump_calls + 1
            last_orig_fastmode = fm
            return fm and "orig_fast" or "orig_slow"
        end
        for _, fn in ipairs(sim_post) do fn() end
        assert(fakeG11.DataDumper({ a = 1 }, "t", true, nil) == "fast" and fastdump_calls == 1,
               "Local Host FastDataDumper bridge failed!")
        assert(fakeG11.DataDumper({ a = 1 }, "t", false, nil) == "orig_slow" and orig_dump_calls == 1,
               "Local Host FastDataDumper did not fall back on fastmode=false!")
        assert(fakeG11.DataDumper({ a = 1 }, "t", true, 2) == "orig_fast" and orig_dump_calls == 2,
               "Local Host FastDataDumper did not fall back on ident~=nil!")
        -- Normal unsupported type ("fallback") -> keeps fastmode=true
        fastdump_mode = "fallback"
        assert(fakeG11.DataDumper({ a = 1 }, "t", true, nil) == "orig_fast" and last_orig_fastmode == true,
               "fallback reason should forward fastmode=true!")
        -- Cycle / node-limit -> MUST downgrade to cycle-safe slow path (fastmode=false)!
        fastdump_mode = "cycle_detected"
        assert(fakeG11.DataDumper({ a = 1 }, "t", true, nil) == "orig_slow" and last_orig_fastmode == false,
               "cycle_detected MUST downgrade to cycle-safe fastmode=false!")
        fastdump_mode = "node_limit_exceeded"
        assert(fakeG11.DataDumper({ a = 1 }, "t", true, nil) == "orig_slow" and last_orig_fastmode == false,
               "node_limit_exceeded MUST downgrade to cycle-safe fastmode=false!")
        -- Buffer-cap / corrupt_number -> MUST raise controlled Lua error (refuse corrupt/OOM write)!
        fastdump_mode = "buffer_limit_exceeded"
        local ok_buf = G.pcall(fakeG11.DataDumper, { a = 1 }, "t", true, nil)
        assert(not ok_buf, "buffer_limit_exceeded MUST abort via controlled error!")
        fastdump_mode = "corrupt_number"
        local ok_nan = G.pcall(fakeG11.DataDumper, { a = 1 }, "t", true, nil)
        assert(not ok_nan, "corrupt_number MUST abort via controlled error!")

        -- 11d) R3.1 (#04): ClientModRPC 30 -> 60 -> 30 Hz round-trip rollback!
        local rpc_hz_cur = 30
        local rpc_hooks = {}
        local fakeG_rpc = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable, next = G.next,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, tostring = G.tostring, tonumber = G.tonumber,
            string = G.string, math = G.math, table = G.table, print = G.print, error = G.error,
            require = function() return {} end,
            CLIENT_MOD_RPC = {}, CLIENT_MOD_RPC_HANDLERS = {},
            HyperSmooth_SetSimTickRate = function(hz)
                if hz == 30 or hz == 60 then rpc_hz_cur = hz; return true end
                return false
            end,
            HyperSmooth_GetSimTickRate = function() return rpc_hz_cur end,
            TheSim = { GetTick = function() return 10 end, GetStaticTick = function() return 10 end, GetTickTime = function() return 1.0 / rpc_hz_cur end, GetPersistentString = function(_,_,cb) cb(false,"") end, SetPersistentString = function() end },
            TheInput = { AddKeyUpHandler = function() end, IsKeyDown = function() return false end },
            TheFrontEnd = { GetActiveScreen = function() return nil end },
        }
        local env_rpc = {
            GLOBAL = fakeG_rpc,
            GetModConfigData = function(k) if k == "auto_60hz_sim" then return true end return nil end,
            AddClassPostConstruct = function() end, AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end, AddSimPostInit = function() end,
            AddClientModRPCHandler = function(ns, name, fn) rpc_hooks[ns .. ":" .. name] = fn end,
            modimport = function(path)
                local f = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_" .. path))
                setfenv(f, env_rpc)
                return f()
            end,
        }
        local fn_rpc = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_rpc, env_rpc)
        fn_rpc()
        local sim_rpc = rpc_hooks["HyperCoreProto:SimTickRate"]
        assert(type(sim_rpc) == "function", "Missing HyperCoreProto:SimTickRate handler!")
        sim_rpc(60)
        assert(rpc_hz_cur == 60, "RPC(60) failed to activate 60Hz!")
        sim_rpc(30)
        assert(rpc_hz_cur == 30, "RPC(30) failed to roll back to 30Hz!")

        print("[CLIENT_V24_TEST] 11. TrueScrollArea 240Hz + FollowText 240Hz + FastDataDumper Fail-Reasons + RPC 30->60->30 PASS")
    end

    -- =========================================================================
    -- TEST 12: MapScreen 240Hz Zoom Norm & Stationary Throttle + Placer & DeployHelper
    -- =========================================================================
    do
        local class_hooks = {}
        local comp_hooks = {}
        local sim_post = {}
        local mock_tick = 500
        local mock_time = 10.0
        local mock_mx, mock_my = 400, 300
        local mock_hud_ent = nil
        local mock_world_ent = nil
        local deploy_raw_calls = 0

        local fakeG12 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error, debug = G.debug,
            GetTimeRealSeconds = function() return mock_time end,
            TheFrontEnd = { GetHUDScale = function() return 1.0 end },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                GetTick = function() return mock_tick end,
                GetTickTime = function() return 1/30 end,
                GetPosition = function() return mock_mx, mock_my end,
            },
            TheInput = {
                OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end,
                GetHUDEntityUnderMouse = function() return mock_hud_ent end,
                GetWorldEntityUnderMouse = function() return mock_world_ent end,
            },
            TriggerDeployHelpers = function(x, y, z, range, recipe, placerinst)
                deploy_raw_calls = deploy_raw_calls + 1
            end,
            require = function() return {} end,
        }
        local env12 = {
            GLOBAL = fakeG12,
            GetModConfigData = function(k)
                if k == "smooth_ui_240hz" or k == "reduce_240hz_churn" then return true end
                return false
            end,
            AddClassPostConstruct = function(cls, fn) class_hooks[cls] = fn end,
            AddComponentPostInit = function(c, fn) comp_hooks[c] = fn end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function(fn) sim_post[#sim_post + 1] = fn end,
            AddClientModRPCHandler = function() end,
        }
        local fn_m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_m, env12)
        fn_m()
        for _, fn in ipairs(sim_post) do fn() end

        -- 12a) MapScreen zoom scaling (dt=1/240 -> 0.25x, dt=1/60 -> 1.0x, orig_sens ALWAYS restored!)
        assert(type(class_hooks["screens/mapscreen"]) == "function", "mapscreen hook missing!")
        local sens_seen_240, sens_seen_60 = nil, nil
        local autoaim_calls, mapact_calls = 0, 0
        local ms = {
            zoomsensitivity = 15,
            zoom_target_time = 0,
            decorationdata = { dirty = false, alwaysdirty = false },
            minimap = { GetZoom = function() return 2.5 end },
            GetWorldPositionAtCursor = function() return 10, 0, 20 end,
            AutoAimToStaticDecorations = function(s, x, y, z) autoaim_calls = autoaim_calls + 1; return x, y, z end,
            UpdateMapActions = function(s, x, y, z) mapact_calls = mapact_calls + 1; return { action = "LOOK" }, nil end,
            UpdateMapActionsDecorations = function() end,
            OnControl = function() return true end,
            OnMinimapMoved = function() end,
            OnBecomeActive = function() end,
            OnUpdate = function(s, dt)
                if math.abs(dt - 1/240) < 1e-4 then sens_seen_240 = s.zoomsensitivity end
                if math.abs(dt - 1/60) < 1e-4 then sens_seen_60 = s.zoomsensitivity end
                local x, y, z = s:GetWorldPositionAtCursor()
                local ax, ay, az = s:AutoAimToStaticDecorations(x, y, z)
                local lmb, rmb = s:UpdateMapActions(ax, ay, az)
                s:UpdateMapActionsDecorations(x, y, z, lmb, rmb)
            end,
        }
        class_hooks["screens/mapscreen"](ms)
        ms:OnUpdate(1/60)
        assert(math.abs(sens_seen_60 - 15.0) < 1e-4 and ms.zoomsensitivity == 15, "MapScreen 60Hz zoomsensitivity wrong!")
        ms.zoom_target_time = 0
        mock_time = mock_time + 0.004
        ms:OnUpdate(1/240) -- stationary -> throttle hit!
        assert(math.abs(sens_seen_240 - 3.75) < 1e-3 and ms.zoomsensitivity == 15, "MapScreen 240Hz zoomsensitivity scaling/restore wrong!")
        mock_time = mock_time + 0.004
        ms:OnUpdate(1/240)
        assert(autoaim_calls == 1 and mapact_calls == 1, "Stationary MapScreen should skip redundant 240Hz scans: got " .. autoaim_calls .. "/" .. mapact_calls)
        assert((fakeG12.HS_MAP_SKIP_COUNT or 0) >= 2, "HS_MAP_SKIP_COUNT not incremented!")
        -- Mouse move or OnControl or 34ms elapsed must recompute immediately
        mock_mx = 405
        mock_time = mock_time + 0.004
        ms:OnUpdate(1/240)
        assert(autoaim_calls == 2 and mapact_calls == 2, "Mouse movement on MapScreen must bypass throttle!")
        mock_time = mock_time + 0.040
        ms:OnUpdate(1/240)
        assert(autoaim_calls == 3 and mapact_calls == 3, ">33.3ms expiry on MapScreen must refresh!")
        -- Error inside wrapped MapScreen:OnUpdate MUST still restore zoomsensitivity!
        local saved_inner = ms.AutoAimToStaticDecorations
        ms.AutoAimToStaticDecorations = function() error("boom") end
        mock_time = mock_time + 0.040
        local ok_err = G.pcall(ms.OnUpdate, ms, 1/240)
        assert(not ok_err and ms.zoomsensitivity == 15, "zoomsensitivity leaked after MapScreen error!")
        ms.AutoAimToStaticDecorations = saved_inner

        -- 12b) Placer:TestCanBuild same-tick pose memoization + invalidation
        assert(type(comp_hooks["placer"]) == "function", "placer hook missing!")
        local px, py, pz, prot = 4.0, 0.0, 8.0, 0.0
        local active_item_obj = { prefab = "boards" }
        local tcb_calls = 0
        local pl = {
            axisalignedplacementtoggle = false,
            builder = { inst = { replica = { inventory = { GetActiveItem = function() return active_item_obj end } } } },
            inst = { Transform = { GetWorldPosition = function() return px, py, pz end, GetRotation = function() return prot end } },
            TestCanBuild = function(s)
                tcb_calls = tcb_calls + 1
                return px >= 0, (mock_hud_ent ~= nil)
            end,
        }
        comp_hooks["placer"](pl)
        local c1, b1 = pl:TestCanBuild()
        local c2, b2 = pl:TestCanBuild()
        local c3, b3 = pl:TestCanBuild()
        assert(c1 == true and b1 == false and c2 == true and c3 == true and tcb_calls == 1,
               "Placer:TestCanBuild failed same-tick identical-pose memo: calls=" .. tcb_calls)
        assert((fakeG12.HS_PLACER_SKIP_COUNT or 0) == 2, "HS_PLACER_SKIP_COUNT wrong!")
        px = 4.02 -- move > 0.001u
        pl:TestCanBuild()
        assert(tcb_calls == 2, "Placer pose move must invalidate cache!")
        mock_hud_ent = {} -- hover HUD mid-tick
        local c4, b4 = pl:TestCanBuild()
        assert(tcb_calls == 3 and b4 == true, "HUD hover change must invalidate Placer cache!")
        mock_hud_ent = nil
        mock_tick = mock_tick + 1 -- next sim tick
        pl:TestCanBuild()
        assert(tcb_calls == 4, "Next sim tick must invalidate Placer cache!")

        -- 12c) TriggerDeployHelpers same-tick dedup + move/tick/expiry pass-through
        local rec_a = { name = "wall_stone" }
        local pinst = {}
        fakeG12.TriggerDeployHelpers(10, 0, 10, 64, rec_a, pinst)
        fakeG12.TriggerDeployHelpers(10.05, 0, 10.05, 64, rec_a, pinst)
        fakeG12.TriggerDeployHelpers(10.10, 0, 10.10, 64, rec_a, pinst)
        assert(deploy_raw_calls == 1 and (fakeG12.HS_DEPLOY_SKIP_COUNT or 0) == 2,
               "TriggerDeployHelpers dedup failed: raw=" .. deploy_raw_calls)
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst) -- >0.25u move
        assert(deploy_raw_calls == 2, "TriggerDeployHelpers >0.25u move must pass through!")
        mock_tick = mock_tick + 1
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst) -- next tick
        assert(deploy_raw_calls == 3, "TriggerDeployHelpers next tick must pass through!")

        print("[CLIENT_V24_TEST] 12. MapScreen 240Hz Zoom/Throttle + Placer:TestCanBuild + TriggerDeployHelpers PASS")
    end

    do
        local loadfile = G.loadfile
        local setfenv = G.setfenv
        local setmetatable = G.setmetatable
        local getmetatable = G.getmetatable
        local tostring = G.tostring
        local type = G.type
        local pcall = G.pcall
        local error = G.error
        local assert = G.assert
        local print = G.print

        local modmain_path = "../mods/client_v24_lua_test/target_modmain.lua"
        local fn13, load_err13 = G.kleiloadlua(modmain_path)
        assert(type(fn13) == "function", "[TEST 13] Failed to load target modmain.lua: " .. tostring(fn13) .. " / " .. tostring(load_err13))

        local orig_enc_calls, orig_comp_calls, orig_dec_calls = 0, 0, 0
        local last_orig_sp = nil
        local mock_json = {
            encode = function(v)
                orig_enc_calls = orig_enc_calls + 1
                return "ORIG_ENC:" .. tostring(type(v) == "table" and v.k or v)
            end,
            encode_compliant = function(v)
                orig_comp_calls = orig_comp_calls + 1
                return "ORIG_COMP:" .. tostring(type(v) == "table" and v.k or v)
            end,
            decode = function(s, startPos)
                orig_dec_calls = orig_dec_calls + 1
                last_orig_sp = startPos
                return { orig = s, startPos = startPos }, (startPos or 1) + #s
            end,
        }

        local mock_ES = {}
        for k, v in G.pairs(G.EntityScript) do
            mock_ES[k] = v
        end
        mock_ES.__index = mock_ES

        local sgm_calls = 0
        local mock_sgm = {
            OnPushEvent = function(self, sg)
                sgm_calls = sgm_calls + 1
                return true
            end,
        }

        local fast_enc_calls, fast_dec_calls = 0, 0
        local hook_log = {}
        local scheduled_tasks = {}
        local sim_clock = 100.0

        local fake_DoTaskInTime = function(self, dt, fn, ...)
            scheduled_tasks[#scheduled_tasks + 1] = { dt = dt, fn = fn, args = { ... } }
            return { Cancel = function() end }
        end

        local mock_TGI = {}
        if G.TheGlobalInstance then
            for k, v in G.pairs(G.TheGlobalInstance) do mock_TGI[k] = v end
            setmetatable(mock_TGI, getmetatable(G.TheGlobalInstance))
        end
        mock_TGI.DoTaskInTime = fake_DoTaskInTime

        local mock_TW = {}
        if G.TheWorld then
            for k, v in G.pairs(G.TheWorld) do mock_TW[k] = v end
            setmetatable(mock_TW, getmetatable(G.TheWorld))
        end
        mock_TW.DoTaskInTime = fake_DoTaskInTime

        local mock_sched = {}
        if G.scheduler then
            for k, v in G.pairs(G.scheduler) do mock_sched[k] = v end
        end
        mock_sched.ExecuteInTime = fake_DoTaskInTime

        local mock_debug = {
            getinfo = G.debug.getinfo,
            sethook = function(fn, mask, count)
                hook_log[#hook_log + 1] = { fn = fn, mask = mask, count = count }
            end,
        }
        local mock_os = {
            clock = function()
                sim_clock = sim_clock + 0.05
                return sim_clock
            end,
            date = G.os.date,
            time = G.os.time,
        }

        local g13 = {}
        for k, v in G.pairs(G) do
            g13[k] = v
        end
        g13._G = g13
        g13.BRANCH = "release"
        g13.unpack = G.unpack
        g13.EntityScript = mock_ES
        g13.json = mock_json
        g13.SGManager = mock_sgm
        g13.TheGlobalInstance = mock_TGI
        g13.TheWorld = mock_TW
        g13.scheduler = mock_sched
        g13.debug = mock_debug
        g13.os = mock_os
        g13.table = G.table
        g13.HyperSmooth_Version = function() return "DST-HyperSmooth/2.8.0" end
        g13.HyperSmooth_FastJsonEncode = function(val, compliant)
            fast_enc_calls = fast_enc_calls + 1
            if type(val) == "table" and val.__fallback then
                return nil
            end
            return (compliant and "FAST_COMP:" or "FAST_ENC:") .. tostring(type(val) == "table" and val.k or val)
        end
        g13.HyperSmooth_FastJsonDecode = function(s)
            fast_dec_calls = fast_dec_calls + 1
            if s == "null" then
                return true, nil, 5
            elseif s == '{"fast":1}' then
                return true, { fast = 1 }, #s + 1
            elseif s == "/*fallback*/{}" then
                return false, nil, nil
            end
            return true, { fast_raw = s }, #s + 1
        end
        g13.require = function(modpath)
            if modpath == "json" then
                return mock_json
            elseif modpath == "entityscript" then
                return mock_ES
            end
            return {}
        end

        local noop = function() end
        local env13 = {
            GLOBAL = g13,
            modname = "dst_smooth_highfps",
            MODROOT = "C:/Users/Administrator/.gemini/antigravity/scratch/dst_fps_unlocker/workshop_mod/dst_smooth_highfps/",
            GetModConfigData = function(k) return nil end,
            AddSimPostInit = noop,
            AddGamePostInit = noop,
            AddPrefabPostInit = noop,
            AddPrefabPostInitAny = noop,
            AddComponentPostInit = noop,
            AddClassPostConstruct = noop,
            AddGlobalClassPostConstruct = noop,
            AddPlayerPostInit = noop,
            AddBrainPostInit = noop,
            AddStategraphPostInit = noop,
            AddUserCommand = noop,
            AddModUserCommand = noop,
            AddReplicableComponent = noop,
            AddClientModRPCHandler = noop,
            AddModRPCHandler = noop,
            AddShardModRPCHandler = noop,
            AddPopupPostInit = noop,
            AddModShadersInit = noop,
            AddModShadersSortAndEnable = noop,
            GetClientModRPC = function() return nil end,
            modimport = noop,
            Import = noop,
            Asset = function(t, f) return { type = t, file = f } end,
            PrefabFiles = {},
            Assets = {},
        }
        setmetatable(env13, { __index = g13 })
        setfenv(fn13, env13)
        fn13()

        -- 13a. Verify FastJson bridge (encode, encode_compliant, decode null vs fallback vs startPos != 1)
        assert(mock_json.encode({ k = "abc" }) == "FAST_ENC:abc" and orig_enc_calls == 0,
               "json.encode must take FastJson path")
        assert(mock_json.encode_compliant({ k = "xyz" }) == "FAST_COMP:xyz" and orig_comp_calls == 0,
               "json.encode_compliant must take FastJson compliant path")
        local enc_fb = mock_json.encode({ k = "fb", __fallback = true })
        assert(enc_fb == "ORIG_ENC:fb" and orig_enc_calls == 1,
               "json.encode nil return must fallback to original encoder")

        local dec_null = mock_json.decode("null")
        assert(dec_null == nil and orig_dec_calls == 0,
               "json.decode('null') must preserve nil result without fallback")
        local dec_ok, dec_np = mock_json.decode('{"fast":1}')
        assert(type(dec_ok) == "table" and dec_ok.fast == 1 and dec_np == 11 and orig_dec_calls == 0,
               "json.decode fast table mismatch")
        local dec_fb = mock_json.decode("/*fallback*/{}")
        assert(type(dec_fb) == "table" and dec_fb.orig == "/*fallback*/{}" and orig_dec_calls == 1,
               "json.decode ok=false must fallback to original decoder")
        local dec_sp = mock_json.decode("prefix{}", 2)
        assert(type(dec_sp) == "table" and dec_sp.startPos == 2 and last_orig_sp == 2 and orig_dec_calls == 2,
               "json.decode startPos != 1 must bypass FastJson")

        -- 13b. Verify 0-alloc EntityScript:PushEvent_Internal & listener/reentrancy/error safety
        local function make_inst(id)
            local brain_calls = 0
            local sg_pushed, sg_handled = 0, 0
            local ent_tags = {}
            local mock_entity = {
                IsValid = function() return true end,
                AddTag = function(self, t)
                    if t ~= nil then ent_tags[t] = true end
                end,
                RemoveTag = function(self, t)
                    if t ~= nil then ent_tags[t] = nil end
                end,
                HasTag = function(self, t)
                    return t ~= nil and ent_tags[t] == true
                end,
                HasAllTags = function(self, ...)
                    local n = G.select("#", ...)
                    for idx = 1, n do
                        local t = G.select(idx, ...)
                        if t ~= nil and not ent_tags[t] then
                            return false
                        end
                    end
                    return true
                end,
                HasAnyTag = function(self, ...)
                    local n = G.select("#", ...)
                    for idx = 1, n do
                        local t = G.select(idx, ...)
                        if t ~= nil and ent_tags[t] then
                            return true
                        end
                    end
                    return false
                end,
            }
            local inst = setmetatable({
                GUID = id,
                prefab = "test_" .. tostring(id),
                entity = mock_entity,
                tags = ent_tags,
                inlimbo = false,
                event_listeners = {},
                event_listening = {},
                sg = {
                    currentstate = "idle",
                    IsListeningForEvent = function() return true end,
                    PushEvent = function(self, ev, data) sg_pushed = sg_pushed + 1 end,
                    HandleEvent = function(self, ev, data) sg_handled = sg_handled + 1 end,
                },
                brain = { PushEvent = function(self, ev, data) brain_calls = brain_calls + 1 end },
                _get_brain_calls = function() return brain_calls end,
                _get_sg_pushed = function() return sg_pushed end,
            }, mock_ES)
            return inst
        end

        -- 1-listener, 1-callback + SGManager + brain forwarding
        local i1 = make_inst(101)
        local c1 = 0
        local cb1 = function(src, data) c1 = c1 + (data and data.v or 1) end
        i1:ListenForEvent("ev1", cb1, i1)
        i1:PushEvent_Internal("ev1", { v = 7 })
        assert(c1 == 7 and sgm_calls == 1 and i1._get_sg_pushed() == 1 and i1._get_brain_calls() == 1,
               "Single listener PushEvent_Internal + SGManager/sg/brain forwarding mismatch")

        -- 1-listener removing itself mid-dispatch
        local i2 = make_inst(102)
        local c2 = 0
        local cb2_rem
        cb2_rem = function(src, data)
            c2 = c2 + 1
            i2:RemoveEventCallback("ev_rem", cb2_rem, i2)
        end
        i2:ListenForEvent("ev_rem", cb2_rem, i2)
        i2:PushEvent_Internal("ev_rem", {})
        i2:PushEvent_Internal("ev_rem", {})
        assert(c2 == 1, "Mid-dispatch RemoveEventCallback must prevent second fire, got " .. tostring(c2))

        -- 1-listener adding another listener mid-dispatch
        local i3 = make_inst(103)
        local c3_a, c3_b = 0, 0
        local cb3_b = function() c3_b = c3_b + 1 end
        local cb3_a = function()
            c3_a = c3_a + 1
            i3:ListenForEvent("ev_add", cb3_b, i3)
        end
        i3:ListenForEvent("ev_add", cb3_a, i3)
        i3:PushEvent_Internal("ev_add", {})
        assert(c3_a == 1, "Initial listener should fire once")

        -- Multi-listener + nested re-entrant PushEvent_Internal
        local i4 = make_inst(104)
        local multi_sum, nested_sum = 0, 0
        i4:ListenForEvent("ev_nested", function() nested_sum = nested_sum + 10 end, i4)
        i4:ListenForEvent("ev_multi", function(src, d)
            multi_sum = multi_sum + 1
            i4:PushEvent_Internal("ev_nested", d)
        end, i4)
        i4:ListenForEvent("ev_multi", function() multi_sum = multi_sum + 2 end, i4)
        i4:ListenForEvent("ev_multi", function() multi_sum = multi_sum + 4 end, i4)
        i4:PushEvent_Internal("ev_multi", {})
        assert(multi_sum == 7 and nested_sum == 10,
               "Multi-listener + nested PushEvent_Internal mismatch: " .. tostring(multi_sum) .. "/" .. tostring(nested_sum))

        -- Error resilience leaving scratch pool intact
        local i5 = make_inst(105)
        i5:ListenForEvent("ev_err", function() error("intentional_boom") end, i5)
        local ok_err = pcall(function() i5:PushEvent_Internal("ev_err", {}) end)
        assert(not ok_err, "Expected error to propagate")
        local c5_ok = 0
        i5:ListenForEvent("ev_after_err", function() c5_ok = c5_ok + 1 end, i5)
        i5:PushEvent_Internal("ev_after_err", {})
        assert(c5_ok == 1, "Scratch pool corrupted after listener error")

        -- 13c. Verify unrolled HasTags / HasOneOfTags + HasAllTags / HasAnyTag
        assert(mock_ES.HasAllTags == mock_ES.HasTags, "HasAllTags must rebind to HasTags")
        assert(mock_ES.HasAnyTag == mock_ES.HasOneOfTags, "HasAnyTag must rebind to HasOneOfTags")
        local tinst = make_inst(106)
        tinst:AddTag("a")
        tinst:AddTag("b")
        tinst:AddTag("c")
        tinst:AddTag("d")
        assert(tinst:HasTags(nil) == true and tinst:HasOneOfTags(nil) == false, "nil tags edge case failed")
        assert(tinst:HasTags({}) == true and tinst:HasOneOfTags({}) == false, "empty table tags edge case failed")
        assert(tinst:HasTags({"a"}) == true and tinst:HasTags({"z"}) == false, "1-tag HasTags failed")
        assert(tinst:HasTags({"a", "b"}) == true and tinst:HasTags({"a", "z"}) == false, "2-tag HasTags failed")
        assert(tinst:HasTags({"a", "b", "c"}) == true and tinst:HasTags({"a", "b", "z"}) == false, "3-tag HasTags failed")
        assert(tinst:HasTags({"a", "b", "c", "d"}) == true and tinst:HasTags({"a", "b", "c", "z"}) == false, "4-tag HasTags failed")
        assert(tinst:HasTags({"a", "b", "c", "d", "e"}) == false, "5-tag negative HasTags failed")
        assert(tinst:HasOneOfTags({"z", "b"}) == true and tinst:HasOneOfTags({"y", "z"}) == false, "2-tag HasOneOfTags failed")
        assert(tinst:HasOneOfTags({"y", "z", "c"}) == true and tinst:HasOneOfTags({"x", "y", "z"}) == false, "3-tag HasOneOfTags failed")
        assert(tinst:HasOneOfTags({"x", "y", "z", "d"}) == true and tinst:HasOneOfTags({"w", "x", "y", "z"}) == false, "4-tag HasOneOfTags failed")
        assert(tinst:HasOneOfTags({"v", "w", "x", "y", "a"}) == true, "5-tag HasOneOfTags failed")
        assert(tinst:HasTags("a", "b") == true and tinst:HasTags("a", "z") == false, "vararg HasTags failed")
        assert(tinst:HasOneOfTags("z", "c") == true and tinst:HasOneOfTags("y", "z") == false, "vararg HasOneOfTags failed")

        -- 13d. Verify c_hsprof(1) returns true and cleans up debug hook
        assert(type(g13.c_hsprof) == "function", "Expected GLOBAL.c_hsprof to be registered")
        local prof_ok = g13.c_hsprof(1)
        assert(prof_ok == true, "Expected c_hsprof(1) to return true, got " .. tostring(prof_ok))
        assert(#hook_log >= 1 and type(hook_log[1].fn) == "function", "Expected c_hsprof(1) to arm debug.sethook")
        if #scheduled_tasks > 0 then
            for _, t in G.ipairs(scheduled_tasks) do
                t.fn(G.unpack(t.args))
            end
        elseif type(hook_log[1].fn) == "function" then
            sim_clock = sim_clock + 2.0
            hook_log[1].fn("count")
        end
        assert(#hook_log >= 2 and hook_log[#hook_log].fn == nil,
               "Expected c_hsprof(1) stop callback to restore debug.sethook()")

        print("[CLIENT_V24_TEST] 13. v2.7.0 FastJson + 0-alloc PushEvent_Internal + Unrolled Tags + c_hsprof PASS")
    end

    -- =========================================================================
    -- TEST 14: v2.8.0 Dynamic Lua 5.1 RPC Masks + HUD GL/Ping/Loss + c_hslatency
    -- =========================================================================
    do
        local fn_main, err = G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua")
        assert(fn_main ~= nil, "Failed to load modmain.lua for Test 14: " .. tostring(err))
        local captured_masks = nil
        local printed = {}
        local hud_child = nil
        local class_hooks = {}
        local sim_post_inits = {}

        local pp_mt = { __index = {} }
        function pp_mt.__index:AddPostProcessEffect() return 10 end
        function pp_mt.__index:AddSampler() end
        function pp_mt.__index:SetEffectUniformVariables() end
        function pp_mt.__index:SetSamplerEffectState() end
        function pp_mt.__index:SetSamplerEffectFilter() end
        function pp_mt.__index:SetPostProcessEffectAfter() end
        function pp_mt.__index:SetUniformVariable() end
        function pp_mt.__index:EnablePostProcessEffect() end
        function pp_mt.__index:SetBloomEnabled() end
        function pp_mt.__index:IsBloomEnabled() return true end

        local g14 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, select = G.select,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring,
        }
        g14._G = g14
        g14.TheNet = { GetIsClient = function() return true end, GetIsServer = function() return false end, GetServerIsClientHosted = function() return false end, IsDedicated = function() return false end }
        g14.TheSim = {
            GetTick = function() return 120 end,
            GetStaticTick = function() return 120 end,
            GetTickTime = function() return 1.0 / 30.0 end,
            GetPersistentString = function(_, _, cb) cb(false, "") end,
            SetPersistentString = function() end,
            GetPosition = function() return 0, 0 end,
        }
        g14.TheInput = { OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end, AddMouseButtonHandler = function() end }
        g14.TheFrontEnd = {
            GetGraphicsOptions = function()
                return {
                    GetDisplayMode = function() return 1920, 1080 end,
                    GetNumRefreshRates = function() return 1 end,
                    GetRefreshRate = function() return 60 end,
                }
            end,
            GetActiveScreen = function() return { name = "HUD" } end,
        }
        g14.ThePlayer = nil
        g14.TheWorld = { state = { time = 0.5, phase = "day", isfullmoon = false, israining = false, precipitationrate = 0.0, season = "autumn" }, HasTag = function() return false end }
        g14.PostProcessor = G.setmetatable({}, pp_mt)
        g14.PostProcessorEffects = { ZoomBlur = 0, Bloom = 1, ColourCube = 2, MoonPulseGrading = 3 }
        g14.SamplerEffects = {}
        g14.UniformVariables = { SAMPLER_PARAMS = 10 }
        g14.SamplerSizes = { Relative = 0, Static = 1 }
        g14.SamplerColourMode = { RGBA = 0, RGB = 1 }
        g14.SamplerEffectBase = { PostProcessSampler = 0, BloomSampler = 1, Shader = 2, Texture = 3, Smoke = 4 }
        g14.FILTER_MODE = { POINT = 0, LINEAR = 1, ANISOTROPIC = 2, NONE = 3 }
        g14.MIP_FILTER_MODE = { NONE = 0, POINT = 1, LINEAR = 2 }
        g14.WRAP_MODE = { WRAP = 0, CLAMP = 1, MIRROR = 2, CLAMP_TO_EDGE = 3 }
        g14.resolvefilepath = function(rel) return "../mods/dst_smooth_highfps/" .. rel end
        g14.IsPaused = function() return false end
        g14.STRINGS = { UI = { OPTIONS = { ENABLED = "ON", DISABLED = "OFF" } } }
        g14.TUNING = { CONTROLLER_DEADZONE_RADIUS = 0.3 }
        g14.KEY_ALT = 308
        g14.KEY_LALT = 308
        g14.KEY_RALT = 307
        g14.KEY_F8 = 289
        g14.KEY_F9 = 290
        g14.CONTROL_MOVE_UP = 5
        g14.CONTROL_MOVE_DOWN = 6
        g14.CONTROL_MOVE_LEFT = 7
        g14.CONTROL_MOVE_RIGHT = 8
        g14.CONTROL_CONTROLLER_ATTACK = 39
        g14.CLIENT_MOD_RPC = {}
        g14.CLIENT_MOD_RPC_HANDLERS = {}
        g14.RPC = { AttackButton = 4, LeftClick = 1, SetWriteableText = 59, ClosePopup = 62, SetSkillActivatedState = 68, PostActivateHandshake = 70 }
        g14.CLIENT_RPC = { ShowPopup = 1, RecieveChatHistory = 8, TryToTeachScrapbookData = 15, PostActivateHandshake = 14 }
        g14.HyperSmooth_Version = function() return "DST-HyperSmooth/2.8.0" end
        g14.HyperSmooth_SetFps = function() return true end
        g14.HyperSmooth_SetSkeletal = function() return true end
        g14.HyperSmooth_SetWorldInterp = function() return true end
        g14.HyperSmooth_SetVfxSmooth = function() return true end
        g14.HyperSmooth_SetLowLatency = function() return true end
        g14.HyperSmooth_SetVisual = function() return true end
        g14.HyperSmooth_SetHd2d = function() return true end
        g14.HyperSmooth_GetFrameStats = function() return 240.0, 230.0, 4.1, 0.05, 10, 30 end
        g14.HyperSmooth_GetVisualStats = function() return 1, 16, 0, false, 0.5, true, 0.0, 0 end
        g14.HyperSmooth_GetVisualPerf = function() return true, 0, 0, 0.5, 0.2, 1.0, 0.1, 300, 320, 4096 end
        g14.HyperSmooth_GetLatencyStats = function() return true, 1, 3, 0.2, 10, true, 5, 100, true, 0.0, true, 10, true end
        g14.HyperSmooth_GetHostPerf = function() return true, 10, 0.5, 1.0, 0, true, 2, 0.4, 0.3, true, 1, 20, 0.2, 0 end
        g14.HyperSmooth_GetRenderOptStats = function() return 100, 80, 50, 40, 20, 18, 0, 0 end
        g14.HyperSmooth_GetRenderBackend = function() return true, "NVIDIA RTX 4060 ES2" end
        g14.HyperSmooth_GetVbPoolStats = function() return 90, 10, 5, 2, 8, 512 end
        g14.HyperSmooth_GetNetStats = function() return true, 32, 34, 1.2, 0.4, 0, 10240, 20480, 12, 3 end
        g14.HyperSmooth_GetAsyncSaveStats = function() return true, 5, 5, 65536, 3, 1.4, 1, 0, 0, 2.2 end
        g14.HyperSmooth_SetRpcChannelMasks = function(s0, s1, s2, c0)
            captured_masks = { s0 = s0, s1 = s1, s2 = s2, c0 = c0 }
            return true
        end
        local bb_cleared = false
        local bb_exported = false
        local bb_sample_json = '{"role":"client","total":2,"count":2,"threshold_ms":25,"events":[{"epoch_ms":1795440000123,"frame_seq":101,"frame_us":42500,"cpu_busy_us":40100,"gpu_drain_us":120,"swap_us":450,"world_us":8000,"post_us":500,"lua_us":28000,"net_cpu_us":300,"gc_us":600,"gc_kb":120000,"gc_full":0,"anim_us":1200,"anim_ents":18,"tex_us":100,"unz_us":0,"shader_us":0,"host_us":0,"other_us":2050,"ping_avg_ms":32,"ping_last_ms":34,"loss_pct":0.0,"resend":0,"rx_kbps":20,"triggers":1,"culprit":"CLIENT_LUA","gl":1},{"epoch_ms":1795440001456,"frame_seq":340,"frame_us":4160,"cpu_busy_us":1100,"gpu_drain_us":0,"swap_us":100,"world_us":600,"post_us":100,"lua_us":200,"net_cpu_us":100,"gc_us":0,"gc_kb":120000,"gc_full":0,"anim_us":0,"anim_ents":0,"tex_us":0,"unz_us":0,"shader_us":0,"host_us":0,"other_us":0,"ping_avg_ms":32,"ping_last_ms":215,"loss_pct":3.5,"resend":6,"rx_kbps":18,"triggers":4,"culprit":"NET_SPIKE","gl":0}]}'
        g14.HyperSmooth_GetBlackbox = function(lim)
            if bb_cleared then return 0, 0, '{"role":"client","total":0,"count":0,"threshold_ms":25,"events":[]}' end
            return 2, 2, bb_sample_json
        end
        g14.HyperSmooth_ClearBlackbox = function() bb_cleared = true return true end
        g14.HyperSmooth_ExportBlackbox = function() bb_exported = true return true, "C:/mock/dst_hs_blackbox.json" end
        g14.print = function(...)
            local parts = {}
            for i = 1, G.select("#", ...) do parts[i] = G.tostring(G.select(i, ...)) end
            printed[#printed + 1] = G.table.concat(parts, " ")
        end
        g14.json = (type(G.json) == "table" and G.json) or { encode = function() return "{}" end, decode = function() return {} end }
        g14.ModManager = { GetMods = function() return {} end }
        g14.KnownModIndex = {}
        g14.require = function(path)
            if path == "widgets/text" then
                return function()
                    local w = { str = "", visible = true, inst = { IsValid = function() return true end } }
                    function w:SetHAnchor() end
                    function w:SetVAnchor() end
                    function w:SetRegionSize() end
                    function w:SetHAlign() end
                    function w:SetVAlign() end
                    function w:SetPosition() end
                    function w:SetScale() end
                    function w:SetString(s) self.str = s end
                    function w:SetColour() end
                    function w:SetClickable() end
                    function w:Show() self.visible = true end
                    function w:Hide() self.visible = false end
                    return w
                end
            end
            return {}
        end
        g14.NUMBERFONT = "numberfont"
        g14.ANCHOR_RIGHT = 2
        g14.ANCHOR_TOP = 1

        local env14 = {
            GLOBAL = g14, MODROOT = "../mods/dst_smooth_highfps/", env = {},
            Asset = function(t, f) return { type = t, file = f } end,
            modimport = function() return nil end,
            GetModConfigData = function() return nil end,
            AddClassPostConstruct = function(n, fn) class_hooks[n] = fn end,
            AddSimPostInit = function(fn) sim_post_inits[#sim_post_inits + 1] = fn end,
            AddPrefabPostInit = function() end,
            AddGamePostInit = function() end,
            AddPlayerActivated = function() end,
            AddComponentPostInit = function() end,
            AddModShadersInit = function() end,
            AddModShadersSortAndEnable = function() end,
            GetClientModRPC = function() return nil end,
            AddModRPCHandler = function() end,
            AddClientModRPCHandler = function() end,
        }
        env14.env = env14
        G.setfenv(fn_main, env14)()
        assert(#sim_post_inits >= 1, "Expected AddSimPostInit callback registered")
        for _, fn in G.ipairs(sim_post_inits) do fn() end

        assert(captured_masks ~= nil, "HyperSmooth_SetRpcChannelMasks was not called")
        assert(captured_masks.s0 == 0, "Combat/handshake bit leaked into srv0: " .. tostring(captured_masks.s0))
        local exp_s1 = (2 ^ 27) + (2 ^ 30)
        assert(captured_masks.s1 == exp_s1, "Expected srv1=" .. tostring(exp_s1) .. ", got " .. tostring(captured_masks.s1))
        assert(captured_masks.s2 == (2 ^ 4), "Expected srv2=16, got " .. tostring(captured_masks.s2))
        local exp_c0 = (2 ^ 1) + (2 ^ 8)
        assert(captured_masks.c0 == exp_c0, "Expected cli0=" .. tostring(exp_c0) .. ", got " .. tostring(captured_masks.c0))

        -- Non-Chinese fallback verify: Russian LanguageTranslator must default to English (ui_zh=false)
        g14.LanguageTranslator = { defaultlang = "ru" }
        assert(g14.HyperSmooth_GetDllStatus ~= nil and env14.HS_PanelApi.GetSnapshot().ui_zh == false,
               "Non-Chinese locale should default to pure English")
        g14.LanguageTranslator = nil

        local hud = {
            AddChild = function(_, w) hud_child = w; return w end,
            OnUpdate = function() end,
        }
        assert(type(class_hooks["screens/playerhud"]) == "function", "Expected screens/playerhud hook")
        class_hooks["screens/playerhud"](hud)
        hud:OnUpdate(0.30)
        assert(hud_child ~= nil, "HUD text widget missing")
        assert(hud_child.str:find("FPS 240 | Ping 32ms · Loss 1%%") ~= nil,
               "Expected Compact EN HUD: " .. tostring(hud_child.str))
        assert(hud_child.str:find("%[GL%]") == nil and hud_child.str:find("Frame") == nil,
               "Compact EN HUD should hide [GL]/Frame clutter: " .. tostring(hud_child.str))

        -- Cycle to Detailed EN HUD
        env14.HS_PanelApi.CycleHudMode()
        hud:OnUpdate(0.30)
        assert(hud_child.str:find("FPS 240 · 1%%L 230 · Frame 4%.1ms %[GL%] | Ping 32ms · Loss 1%%") ~= nil,
               "Expected Detailed EN HUD: " .. tostring(hud_child.str))

        -- Switch to ZH via c_hshud and verify Detailed ZH + Compact ZH + ZH c_hslatency()
        g14.c_hshud("zh", "detail")
        hud:OnUpdate(0.30)
        assert(hud_child.str:find("帧率 240 · 最低帧 230 · 帧时 4%.1ms %[GL%] | 延迟 32ms · 丢包 1%%") ~= nil,
               "Expected Detailed ZH HUD: " .. tostring(hud_child.str))
        g14.c_hslatency()
        local joined_zh = G.table.concat(printed, "\n")
        assert(joined_zh:find("VBPool命中=90%.0%%") ~= nil and joined_zh:find("延迟%(均=32ms 瞬=34ms%)") ~= nil,
               "ZH c_hslatency missing ZH Net/VBPool labels: " .. joined_zh)

        g14.c_hshud("simple")
        hud:OnUpdate(0.30)
        assert(hud_child.str:find("帧率 240 | 延迟 32ms · 丢包 1%%") ~= nil,
               "Expected Compact ZH HUD: " .. tostring(hud_child.str))

        -- Restore EN for English c_hslatency() check
        g14.c_hshud("auto", "detail")
        printed = {}
        g14.c_hslatency()
        local joined = G.table.concat(printed, "\n")
        assert(joined:find("OpenGL ES2") ~= nil and joined:find("VBPool=90%.0%%") ~= nil, "c_hslatency missing Render/VBPool: " .. joined)
        assert(joined:find("Ping%(avg=32ms last=34ms%)") ~= nil and joined:find("RpcQoS%(imm=12 offCh0=3%)") ~= nil, "c_hslatency missing Net/QoS: " .. joined)
        assert(joined:find("%[HyperSmooth AsyncSave%] Active=true") ~= nil, "c_hslatency missing AsyncSave: " .. joined)

        local snap14 = env14.HS_PanelApi.GetSnapshot()
        assert(snap14.render_backend == "GL" and snap14.vb_hit_pct == 90.0 and snap14.ping_avg == 32 and snap14.hud_mode == "detail", "Snapshot missing v2.8 telemetry")

        -- Verify HyperSmooth_GetRenderBackend string semantics ("d3d9" != GL, "opengl" == GL)
        g14.HyperSmooth_GetRenderBackend = function() return "d3d9", "ANGLE (NVIDIA D3D9)" end
        g14.c_hshud("auto", "detail")
        hud:OnUpdate(0.30)
        assert(hud_child.str:find("%[GL%]") == nil, "D3D9 backend must NOT show [GL] badge: " .. tostring(hud_child.str))
        assert(env14.HS_PanelApi.GetSnapshot().render_backend == "D3D9", "Expected D3D9 snapshot backend")
        printed = {}
        g14.c_hslatency()
        local joined_d3d9 = G.table.concat(printed, "\n")
        assert(joined_d3d9:find("ANGLE D3D9") ~= nil and joined_d3d9:find("OpenGL ES2") == nil,
               "Expected ANGLE D3D9 label for d3d9 string: " .. joined_d3d9)

        g14.HyperSmooth_GetRenderBackend = function() return "opengl", "NVIDIA RTX 4060 ES2" end
        hud:OnUpdate(0.30)
        assert(hud_child.str:find("%[GL%]") ~= nil and env14.HS_PanelApi.GetSnapshot().render_backend == "GL",
               "Expected GL badge/snapshot for opengl string")

        -- Verify c_hsblackbox() under ZH & EN + export + clear
        assert(type(g14.c_hsblackbox) == "function", "Expected GLOBAL.c_hsblackbox")
        g14.c_hshud("zh")
        printed = {}
        assert(g14.c_hsblackbox(5) == true, "c_hsblackbox(5) ZH failed")
        local bb_zh = G.table.concat(printed, "\n")
        assert(bb_zh:find("掉帧黑匣子") ~= nil and bb_zh:find("CLIENT_LUA") ~= nil and bb_zh:find("NET_SPIKE") ~= nil,
               "ZH c_hsblackbox missing expected culprit rows: " .. bb_zh)

        g14.c_hshud("en")
        printed = {}
        assert(g14.c_hsblackbox(5) == true, "c_hsblackbox(5) EN failed")
        local bb_en = G.table.concat(printed, "\n")
        assert(bb_en:find("Spike Blackbox") ~= nil and bb_en:find("Frame=42%.50ms") ~= nil and bb_en:find("Loss=3%.5%%") ~= nil,
               "EN c_hsblackbox missing expected metrics: " .. bb_en)

        assert(g14.c_hsblackbox("export") == true and bb_exported == true, "c_hsblackbox export failed")
        assert(g14.c_hsblackbox("clear") == true and bb_cleared == true, "c_hsblackbox clear failed")
        printed = {}
        assert(g14.c_hsblackbox(5) == true and G.table.concat(printed, "\n"):find("no frame") ~= nil,
               "Expected empty blackbox message after clear")

        print("[CLIENT_V24_TEST] 14. v2.8.0 Dynamic RPC Masks + Compact/Detailed Auto-ZH/EN HUD + c_hshud + c_hslatency + c_hsblackbox + RenderBackend PASS")
    end

    -- =========================================================================
    -- TEST 15: OptionsScreen Lazy Controls (mod-compatible) + 1% Low Transition Hold
    -- =========================================================================
    do
        local kb_build_calls, ctrl_build_calls, refresh_calls = 0, 0, 0
        local MockOptionsScreen = {}
        function MockOptionsScreen:_CollectKeyboardControlWidgets()
            kb_build_calls = kb_build_calls + 1
            self.kb_controlwidgets = {
                { tag = "vanilla_kb_1", device_type = "keyboard", control = { keyboard = 1 }, binding_btn = { SetText = function() end } },
                { tag = "vanilla_kb_2", device_type = "keyboard", control = { keyboard = 2 }, binding_btn = { SetText = function() end } },
            }
        end
        function MockOptionsScreen:_CollectControllerControlWidgets()
            ctrl_build_calls = ctrl_build_calls + 1
            self.controller_controlwidgets = {
                { tag = "vanilla_ctrl_1", device_type = "controller", control = { controller = 1 }, binding_btn = { SetText = function() end } },
            }
        end
        function MockOptionsScreen:UpdateMenu() end
        function MockOptionsScreen:RefreshControls()
            refresh_calls = refresh_calls + 1
        end
        function MockOptionsScreen:LoadDefaultControls() end
        function MockOptionsScreen:OnControlMapped() end
        function MockOptionsScreen:_RebuildControllerControls()
            self:_CollectControllerControlWidgets()
        end
        function MockOptionsScreen:OnBecomeActive() end

        local hold_log = {}
        local active_scr = { name = "HUD" }
        local paused_flag = false
        local fe15 = {
            fadedir = nil,
            GetActiveScreen = function() return active_scr end,
            Update = function() end,
            Fade = function(self, dir) self.fadedir = dir end,
            PushScreen = function(_, s) active_scr = s end,
            PopScreen = function() active_scr = { name = "HUD" } end,
        }
        local game_post_inits = {}
        local g15 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, error = G.error,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print,
            STRINGS = { UI = { OPTIONS = { INTEGRATEDBACKPACK_ENABLED = "Integrated" } } },
            IsPaused = function() return paused_flag end,
            TheFrontEnd = fe15,
            HyperSmooth_SetStatsHold = function(h) hold_log[#hold_log + 1] = h; return h end,
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
            },
            TheInput = { OnUpdate = function() end, IsKeyDown = function() return false end },
            require = function(name)
                if name == "screens/redux/optionsscreen" then return MockOptionsScreen end
                return {}
            end,
        }
        local env15 = {
            GLOBAL = g15,
            GetModConfigData = function(k) return k == "target_fps" and 240 or false end,
            AddClassPostConstruct = function() end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function() end,
            AddGamePostInit = function(fn) game_post_inits[#game_post_inits + 1] = fn end,
            AddClientModRPCHandler = function() end,
        }
        local fn_m15 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_m15, env15)
        fn_m15()
        for _, fn in ipairs(game_post_inits) do fn() end

        -- 1) Construct mock OptionsScreen landing on "settings" tab (0 vanilla rows created!)
        local function MakeMockList()
            local lst = {
                items = {},
                children = {},
                bar_front = 0,
                AddChild = function(self, w) self.children[#self.children + 1] = w; return w end,
                SetList = function(self, arr, keep) self.items = arr; self.last_keep = keep end,
            }
            lst.scroll_bar_container = { MoveToFront = function() lst.bar_front = lst.bar_front + 1 end }
            return lst
        end
        local bp_state = { enabled = nil, idx = 1, txt = nil }
        local dev_selected = 0
        local scr = G.setmetatable({
            selected_tab = "settings",
            deviceSpinner = { GetSelectedData = function() return dev_selected end },
            integratedbackpackSpinner = {
                selectedIndex = 2,
                Enable = function() bp_state.enabled = true end,
                Disable = function() bp_state.enabled = false end,
                SetSelectedIndex = function(_, idx) bp_state.idx = idx end,
                UpdateText = function(_, t) bp_state.txt = t end,
            },
            kb_controllist = MakeMockList(),
            controller_controllist = MakeMockList(),
        }, { __index = MockOptionsScreen })

        scr:_CollectKeyboardControlWidgets()
        scr:_CollectControllerControlWidgets()
        assert(kb_build_calls == 0 and ctrl_build_calls == 0, "Expected 0 control builds on initial open")
        -- Simulate 3rd-party keybind mod appending rows to kb_controllist in AddClassPostConstruct!
        local mod_hdr = { tag = "mod_header" }
        local mod_row = { tag = "mod_bind_1" }
        local items = scr.kb_controllist.items
        items[#items + 1] = scr.kb_controllist:AddChild(mod_hdr)
        items[#items + 1] = scr.kb_controllist:AddChild(mod_row)
        scr.kb_controllist:SetList(items, true)

        scr:RefreshControls()
        assert(kb_build_calls == 0 and ctrl_build_calls == 0, "RefreshControls on settings tab must stay lazy")
        assert(bp_state.enabled == true and bp_state.idx == 2, "Keyboard integratedbackpackSpinner Enable parity failed")

        -- 2) Click "controls" tab -> builds ONLY keyboard controls, prepends ahead of mod rows, brings scrollbar front!
        scr.selected_tab = "controls"
        scr:UpdateMenu()
        assert(kb_build_calls == 1 and ctrl_build_calls == 0, "Expected 1 kb build & 0 ctrl build on controls tab")
        assert(#scr.kb_controllist.items == 4, "Expected 2 vanilla + 2 mod rows, got " .. tostring(#scr.kb_controllist.items))
        assert(scr.kb_controllist.items[1].tag == "vanilla_kb_1" and scr.kb_controllist.items[2].tag == "vanilla_kb_2"
           and scr.kb_controllist.items[3].tag == "mod_header"   and scr.kb_controllist.items[4].tag == "mod_bind_1",
               "Vanilla-first / mod-last row ordering broken!")
        assert(scr.kb_controllist.bar_front == 1 and scr.kb_controllist.last_keep == true, "Scrollbar/keepitems guard failed")

        -- 3) Switch device to controller -> builds controller controls on demand!
        dev_selected = 1
        scr:UpdateMenu()
        assert(ctrl_build_calls == 1 and #scr.controller_controllist.items == 1, "Lazy controller build on device switch failed")
        assert(scr.controller_controllist.bar_front == 1, "Controller scrollbar MoveToFront failed")

        -- 4) Verify Honest 1% Low Transition Hold (HUD live -> false; Pause/Fade/Options -> true; back to HUD -> false; 0 dup calls)
        fe15:Update(0.004)
        fe15:Update(0.004)
        assert(#hold_log == 1 and hold_log[1] == false, "Expected single hold=false in live HUD gameplay")
        fe15:PushScreen({ name = "PauseScreen" })
        assert(#hold_log == 2 and hold_log[2] == true, "Expected hold=true on PauseScreen")
        fe15:Fade("out")
        fe15:Update(0.004)
        assert(#hold_log == 2, "Expected 0 redundant C++ calls while still held")
        fe15:PopScreen()
        fe15.fadedir = nil
        fe15:Update(0.004)
        assert(#hold_log == 3 and hold_log[3] == false, "Expected hold=false on return to live HUD")
        paused_flag = true
        fe15:Update(0.004)
        assert(#hold_log == 4 and hold_log[4] == true, "Expected hold=true when IsPaused() == true")
        paused_flag = false
        fe15:Update(0.004)
        assert(#hold_log == 5 and hold_log[5] == false, "Expected hold=false when unpaused")

        print("[CLIENT_V24_TEST] 15. OptionsScreen Lazy Controls (mod-order + scrollbar + gamepad) & Honest 1% Low Transition Hold PASS")
    end

    -- ------------------------------------------------------------------------
    -- TEST 16: Full 74-Knob Zero-Hardcode Visual Studio (All 3 Floors:
    --          PostFX+CDL+GlowPyramid, Native 3D Relief + Ocean Caustics +
    --          Projected Shadows + Night Campfire + Bloom-OFF Shadow Safety,
    --          Independent Reversible Filters, 3 Custom Slots & Coexistence)
    -- ------------------------------------------------------------------------
    do
        local cc_data_log, cc_lerp_log, dist_log, snow_log = {}, {}, {}, {}
        local pp_u16, pp_en16, pp_bound16, pp_samp16 = {}, {}, {}, {}
        local native_mip16, native_hd2d16, native_ws16 = 0.0, nil, nil
        local engine_bloom_rt_log = {}
        local mock_bloom_enabled = true
        local persist_store16 = {}
        local mock_clip16 = ""
        local clip_writes16 = 0
        local pp_idx16 = {
            SetBloomSamplerParams = function() end,
            AddUniformVariable = function(_, n) return n end,
            AddSamplerEffect = function(_, p) return p end,
            AddSampler = function(_, eff, base, src)
                pp_samp16[eff] = pp_samp16[eff] or {}
                pp_samp16[eff][#pp_samp16[eff] + 1] = { base, src }
            end,
            SetEffectUniformVariables = function(_, eff, ...) pp_bound16[eff] = { ... } end,
            SetSamplerEffectState = function() end,
            SetSamplerEffectFilter = function() end,
            AddPostProcessEffect = function(_, p) return p end,
            SetPostProcessEffectAfter = function() end,
            SetUniformVariable = function(_, id, a, b, c, d) pp_u16[id] = { a, b, c, d } end,
            EnablePostProcessEffect = function(_, id, en) pp_en16[id] = en end,
            SetBloomEnabled = function(_, en) engine_bloom_rt_log[#engine_bloom_rt_log + 1] = en end,
            IsBloomEnabled = function() return mock_bloom_enabled end,
            SetColourCubeData = function(_, idx, src, dst) cc_data_log[#cc_data_log + 1] = { idx, src, dst } end,
            SetColourCubeLerp = function(_, idx, l) cc_lerp_log[#cc_lerp_log + 1] = { idx, l } end,
            SetDistortionFactor = function(_, f) dist_log[#dist_log + 1] = f end,
        }
        local pp16 = setmetatable({}, { __index = pp_idx16 })
        local map_idx16 = {
            SetOverlayLerp = function(_, lvl) snow_log[#snow_log + 1] = lvl end,
        }
        local map16 = setmetatable({}, { __index = map_idx16 })
        local world16 = { Map = map16, state = { phase = "day", timeinphase = 0.5, time = 0.5, israining = true, precipitationrate = 0.8 }, HasTag = function() return false end }
        local campfire16 = {
            Light = { IsEnabled = function() return true end, GetCalculatedRadius = function() return 6.0 end },
            Transform = { GetWorldPosition = function() return 8.0, 0.0, 0.0 end },
        }
        local player16 = {
            Transform = { GetWorldPosition = function() return 0.0, 0.0, 0.0 end },
        }
        local sh_init16, sh_sort16, pref_hooks16, cls_hooks16 = {}, {}, {}, {}

        local g16 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = function() end,
            json = G.json,
            HyperSmooth_SetVisual = function(_, _, mip) native_mip16 = mip or 0.0; return true end,
            HyperSmooth_SetHd2d = function(...) native_hd2d16 = { ... }; return true end,
            HyperSmooth_SetWorldStudio = function(...) native_ws16 = { ... }; return true end,
            HyperSmooth_SetClipboard = function(s) mock_clip16 = s or ""; clip_writes16 = clip_writes16 + 1; return true end,
            ThePlayer = player16,
            TheCamera = {
                GetRightVec = function() return { x = 1, y = 0, z = 0 } end,
                GetDownVec  = function() return { x = 0, y = 0, z = 1 } end,
            },
            TheSim = {
                GetPersistentString = function(_, fname, cb)
                    local v = persist_store16[fname]
                    cb(v ~= nil, v or "")
                end,
                SetPersistentString = function(_, fname, payload) persist_store16[fname] = payload or "" end,
                GetClipboardData = function() return mock_clip16 end,
                FindEntities = function() return { player16, campfire16 } end,
            },
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyDownHandler = function() end, OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            TheWorld = world16,
            PostProcessor = pp16,
            PostProcessorEffects = { Bloom = 1, ColourCube = 2 },
            SamplerEffects = {},
            UniformVariables = { SAMPLER_PARAMS = "SAMPLER_PARAMS" },
            SamplerSizes = { Relative = 1 },
            SamplerColourMode = { RGB = 1 },
            SamplerEffectBase = { BloomSampler = 1, Shader = 2 },
            FILTER_MODE = { LINEAR = 1 },
            MIP_FILTER_MODE = { NONE = 1 },
            WRAP_MODE = { CLAMP_TO_EDGE = 1 },
            KnownModIndex = { GetModInfo = function() return {} end },
            require = function() return {} end,
        }

        local env16 = {
            GLOBAL = g16,
            MODROOT = "../mods/client_v24_lua_test/",
            Asset = function() return {} end,
            GetModConfigData = function() return nil end,
            AddModShadersInit = function(fn) sh_init16[#sh_init16 + 1] = fn end,
            AddModShadersSortAndEnable = function(fn) sh_sort16[#sh_sort16 + 1] = fn end,
            AddPrefabPostInit = function(pf, fn) pref_hooks16[pf] = fn end,
            AddClassPostConstruct = function(cls, fn) cls_hooks16[cls] = fn end,
            AddComponentPostInit = function() end,
            AddSimPostInit = function() end,
            AddGamePostInit = function() end,
            AddClientModRPCHandler = function() end,
            modimport = function() end,
        }

        local fn16, err16 = G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua")
        assert(fn16 ~= nil, "modmain load failed in Test 16: " .. tostring(err16))
        G.setfenv(fn16, env16)
        fn16()
        for _, fn in ipairs(sh_init16) do fn() end
        for _, fn in ipairs(sh_sort16) do fn() end
        if pref_hooks16["world"] then pref_hooks16["world"](world16) end

        -- Verify ALL 15 uniforms & samplers bound on HS_Bloom (4 uniforms, 3 added samplers) + HS_VisualFinal (11 uniforms, 1 added BloomSampler)
        local bound_bloom = pp_bound16[g16.PostProcessorEffects.HS_Bloom]
        assert(type(bound_bloom) == "table" and #bound_bloom == 4
            and bound_bloom[1] == "HS_BLOOM_PARAMS" and bound_bloom[2] == "HS_BLOOM_EX"
            and bound_bloom[3] == "HS_BLOOM_FX" and bound_bloom[4] == "HS_BLOOM_SUN",
            "HS_Bloom uniform binding mismatch!")
        assert(#(pp_samp16[g16.PostProcessorEffects.HS_Bloom] or {}) == 3, "HS_Bloom must bind 3 pyramid samplers!")
        local bound_final = pp_bound16[g16.PostProcessorEffects.HS_VisualFinal]
        assert(type(bound_final) == "table" and #bound_final == 11
            and bound_final[1] == "HS_VISUAL_PARAMS" and bound_final[3] == "HS_SHARP_PRO"
            and bound_final[4] == "HS_AURA_EX" and bound_final[7] == "HS_GRADE_C"
            and bound_final[8] == "HS_CDL_S" and bound_final[11] == "HS_SHADOW_CFG",
            "HS_VisualFinal 11-uniform binding mismatch!")
        assert(#(pp_samp16[g16.PostProcessorEffects.HS_VisualFinal] or {}) == 1
            and pp_samp16[g16.PostProcessorEffects.HS_VisualFinal][1][1] == 1,
            "HS_VisualFinal must attach SEB.BloomSampler for shadow composite!")

        -- Attach mock widgets/sandover to test reversible sandstorm dust hide/show
        local sand_hidden, dust_hidden = false, false
        local sand_w = {
            shown = true,
            inst = { entity = { Hide = function() sand_hidden = true end, Show = function() sand_hidden = false end } },
            dust = {
                shown = true,
                inst = { entity = { Hide = function() dust_hidden = true end, Show = function() dust_hidden = false end } },
            },
            OnUpdate = function() end,
        }
        assert(type(cls_hooks16["widgets/sandover"]) == "function", "widgets/sandover hook missing!")
        cls_hooks16["widgets/sandover"](sand_w)

        -- Simulate vanilla ColourCube / Sanity / Distortion / Snow calls first
        pp16:SetColourCubeData(0, "summer_cc.tex", "autumn_cc.tex")
        pp16:SetColourCubeLerp(0, 1.0)
        pp16:SetColourCubeLerp(1, 0.75)
        pp16:SetDistortionFactor(0.40)
        map16:SetOverlayLerp(1.0)

        -- 1) Preset 2 (Cinematic Studio): activates BCAS + Warm ACES + Native 3D Surface Light + Ground Shadows + Ocean Caustics!
        env16.HS_PanelApi.SetPreset(2)
        assert(pp_en16[g16.PostProcessorEffects.HS_VisualFinal] == true, "Preset 2 must enable HS_VisualFinal")
        assert(math.abs(pp_u16["HS_VISUAL_PARAMS"][1] - 5.20) < 1e-2, "Preset 2 BCAS mismatch")
        assert(math.abs(pp_u16["HS_GRADE_A"][1] - 0.05) < 1e-3 and math.abs(pp_u16["HS_GRADE_C"][3] - 0.25) < 1e-3, "Preset 2 grading/sun_fill mismatch")
        assert(native_ws16 ~= nil and math.abs(native_ws16[1] - 0.75) < 1e-3 and math.abs(native_ws16[9] - 0.45) < 1e-3 and native_ws16[16] == 1.0,
            "Preset 2 must push 3D SurfaceLight=0.75, OceanGlint=0.45, ShadowProjOn=1.0 to native WorldStudio!")
        assert(math.abs(pp_u16["HS_SHADOW_CFG"][4] - 0.45) < 1e-3 and math.abs(pp_u16["HS_SHADOW_CFG"][3] - 0.38) < 1e-3,
            "Preset 2 HS_SHADOW_CFG density/tint mismatch!")
        local last_cc = cc_data_log[#cc_data_log]
        assert(last_cc[2] == "images/colour_cubes/identity_colourcube.tex" and last_cc[3] == "autumn_cc.tex", "vanilla_cc<1 must swap src to identity LUT!")

        -- 2) Zero-Hardcode Verification across ALL 3 Floors (Sharpen Pro, Cinema+CDL+Grain, Glow Pyramid, Native 3D/Shadow/Ocean)
        g16.c_hstune("exposure", 0.00)
        assert(math.abs(pp_u16["HS_GRADE_A"][1]) < 1e-4, "Zero-hardcode violated: exposure=0.00 must stay 0.00!")
        g16.c_hstune("deconv_gate", 0.045)
        g16.c_hstune("deconv_penetr", 0.50)
        g16.c_hstune("range_sigma", 0.40)
        g16.c_hstune("spatial_sigma", 1.60)
        g16.c_hstune("noise_floor", 0.025)
        g16.c_hstune("ar_l_over", 0.020)
        g16.c_hstune("ar_d_over", 0.015)
        assert(math.abs(pp_u16["HS_SHARP_PRO"][1] - 0.045) < 1e-4 and math.abs(pp_u16["HS_SHARP_PRO"][2] - 0.50) < 1e-3
            and math.abs(pp_u16["HS_SHARP_PRO"][3] - 0.40) < 1e-3 and math.abs(pp_u16["HS_SHARP_PRO"][4] - 0.025) < 1e-4, "HS_SHARP_PRO mismatch!")
        assert(math.abs(pp_u16["HS_AURA_EX"][2] - 0.020) < 1e-4 and math.abs(pp_u16["HS_AURA_EX"][3] - 0.015) < 1e-4
            and math.abs(pp_u16["HS_AURA_EX"][4] - 1.60) < 1e-3, "HS_AURA_EX mismatch!")
        g16.c_hstune("lightness", 0.10)
        g16.c_hstune("gamma", 0.88)
        g16.c_hstune("sun_fill", 0.60)
        g16.c_hstune("original_mix", 0.20)
        g16.c_hstune("slope_r", 1.15)
        g16.c_hstune("offset_b", 0.05)
        g16.c_hstune("power_g", 0.92)
        g16.c_hstune("grain", 0.15)
        assert(math.abs(pp_u16["HS_GRADE_C"][1] - 0.10) < 1e-3 and math.abs(pp_u16["HS_GRADE_C"][2] - 0.88) < 1e-3
            and math.abs(pp_u16["HS_GRADE_C"][3] - 0.60) < 1e-3 and math.abs(pp_u16["HS_GRADE_C"][4] - 0.20) < 1e-3, "HS_GRADE_C mismatch!")
        assert(math.abs(pp_u16["HS_CDL_S"][1] - 1.15) < 1e-3 and math.abs(pp_u16["HS_CDL_O"][3] - 0.05) < 1e-3
            and math.abs(pp_u16["HS_CDL_P"][2] - 0.92) < 1e-3 and math.abs(pp_u16["HS_CDL_P"][4] - 0.15) < 1e-3, "HS_CDL/grain mismatch!")
        g16.c_hstune("bloom_thresh", 0.20)
        g16.c_hstune("bloom_warmth", -0.50)
        g16.c_hstune("bloom_spread", 0.85)
        g16.c_hstune("glow_tail", 0.70)
        g16.c_hstune("light_wrap", 0.40)
        g16.c_hstune("glow_rim", 0.30)
        g16.c_hstune("god_rays", 1.20)
        g16.c_hstune("bloom_compress", 0.35)
        assert(math.abs(pp_u16["HS_BLOOM_EX"][1] - 0.20) < 1e-3 and math.abs(pp_u16["HS_BLOOM_EX"][2] + 0.50) < 1e-3
            and math.abs(pp_u16["HS_BLOOM_EX"][3] - 0.85) < 1e-3 and math.abs(pp_u16["HS_BLOOM_EX"][4] - 0.70) < 1e-3, "HS_BLOOM_EX mismatch!")
        assert(math.abs(pp_u16["HS_BLOOM_FX"][1] - 0.40) < 1e-3 and math.abs(pp_u16["HS_BLOOM_FX"][2] - 0.30) < 1e-3
            and math.abs(pp_u16["HS_BLOOM_FX"][3] - 1.20) < 1e-3 and math.abs(pp_u16["HS_BLOOM_FX"][4] - 0.35) < 1e-3, "HS_BLOOM_FX mismatch!")
        g16.c_hstune("surface_light", 1.10)
        g16.c_hstune("shade_cool", 0.95)
        g16.c_hstune("shade_dark", 0.20)
        g16.c_hstune("glint_str", 0.75)
        g16.c_hstune("glint_grain", 1.30)
        g16.c_hstune("shadow_density", 0.60)
        g16.c_hstune("shadow_soft", 1.50)
        assert(math.abs(native_ws16[1] - 1.10) < 1e-3 and math.abs(native_ws16[4] - 0.95) < 1e-3 and math.abs(native_ws16[5] - 0.20) < 1e-3, "Native 3D Relief mismatch!")
        assert(math.abs(native_ws16[9] - 0.75) < 1e-3 and math.abs(native_ws16[11] - 1.30) < 1e-3, "Native Ocean Caustics mismatch!")
        assert(math.abs(pp_u16["HS_CDL_O"][4] - 1.50) < 1e-3 and math.abs(pp_u16["HS_SHADOW_CFG"][4] - 0.60) < 1e-3, "Shadow composite params mismatch!")

        -- 3) Night Campfire Shadow Direction + Bloom-OFF Shadow Pass Safety!
        world16.state.phase = "night"
        g16.c_hstune("shadow_density", 0.60)
        assert(native_ws16[13] < -0.95 and native_ws16[16] == 1.0,
            "Night campfire at +X must project shadows toward -X (got shad_wx=" .. tostring(native_ws16[13]) .. ")!")
        world16.state.phase = "day"
        -- Simulate user turning Game Bloom OFF while keeping Projected Shadows ON:
        pp16:SetBloomEnabled(false)
        assert(engine_bloom_rt_log[#engine_bloom_rt_log] == true, "Bloom-OFF with ShadowProj ON must silently enable engine BloomSampler RT!")
        assert(pp_en16[1] == false and pp_en16[g16.PostProcessorEffects.HS_Bloom] == false, "Bloom-OFF must NEVER leak bloom glow even when ShadowProj ON!")
        assert(pp_en16[g16.PostProcessorEffects.HS_VisualFinal] == true, "HS_VisualFinal must remain ON to composite shadows!")
        -- Restore Vanilla Pure -> MUST turn engine Bloom RT back OFF cleanly!
        env16.HS_PanelApi.SetPreset(0)
        assert(engine_bloom_rt_log[#engine_bloom_rt_log] == false, "Preset 0 must restore engine Bloom RT back to player's OFF preference!")
        assert(native_ws16[1] == 0.0 and native_ws16[9] == 0.0 and native_ws16[16] == 0.0, "Preset 0 must zero all Native World Studio effects!")
        pp16:SetBloomEnabled(true)

        -- 4) Independent Reversible Vanilla Filters (NOT bundled!)
        env16.HS_PanelApi.ToggleTuneBool("sanity_color")
        assert(cc_lerp_log[#cc_lerp_log][1] == 1 and cc_lerp_log[#cc_lerp_log][2] == 0, "sanity_color=1 must zero sanity LUT lerp")
        assert(math.abs(dist_log[#dist_log] - 0.40) < 1e-4 and math.abs(snow_log[#snow_log] - 1.00) < 1e-4, "sanity_color alone must NOT touch distortion/snow!")
        g16.c_hstune("snow_cap", 0.40)
        assert(math.abs(snow_log[#snow_log] - 0.40) < 1e-4, "snow_cap=0.40 must cap snow overlay!")
        env16.HS_PanelApi.ToggleTuneBool("sand_filter")
        assert(sand_hidden == true and dust_hidden == true, "sand_filter=1 must hide sandover & dust!")
        env16.HS_PanelApi.ToggleTuneBool("sand_filter")
        assert(sand_hidden == false and dust_hidden == false, "sand_filter=0 must restore sandover & dust!")

        -- 5) Player Self-Save Custom Slot (Save -> Reset -> Load + Persistence Roundtrip across VM restart!)
        env16.HS_PanelApi.SetPreset(2)
        g16.c_hstune("tint", 0.25)
        g16.c_hstune("slope_r", 1.22)
        g16.c_hstune("surface_light", 1.15)
        g16.c_hstune("glint_str", 0.80)
        g16.c_hstune("snow_cap", 0.40)
        assert(env16.HS_PanelApi.SaveCustomSlot(1) == true, "SaveCustomSlot(1) failed!")
        assert(env16.HS_PanelApi.GetSnapshot().slot_saved[1] == true, "Snapshot must mark slot 1 saved!")
        env16.HS_PanelApi.SetPreset(0)
        assert(env16.HS_PanelApi.LoadCustomSlot(1) == true, "LoadCustomSlot(1) failed!")
        local snap_loaded = env16.HS_PanelApi.GetSnapshot()
        assert(snap_loaded.preset == 3 and math.abs(snap_loaded.tune.slope_r - 1.22) < 1e-3
            and math.abs(snap_loaded.tune.surface_light - 1.15) < 1e-3 and math.abs(snap_loaded.tune.glint_str - 0.80) < 1e-3,
            "LoadCustomSlot(1) failed to restore exact custom recipe!")

        -- Reload fresh modmain.lua from persist_store16 and verify BOTH main config AND custom slot 1 survive restart!
        local g16b = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = function() end,
            TheSim = {
                GetPersistentString = function(_, fname, cb)
                    local v = persist_store16[fname]
                    cb(v ~= nil, v or "")
                end,
                SetPersistentString = function() end,
            },
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyDownHandler = function() end, OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            KnownModIndex = { GetModInfo = function() return {} end },
            require = function() return {} end,
        }
        local env16b = {
            GLOBAL = g16b, MODROOT = "../mods/client_v24_lua_test/", Asset = function() return {} end,
            GetModConfigData = function() return nil end,
            AddModShadersInit = function() end, AddModShadersSortAndEnable = function() end,
            AddPrefabPostInit = function() end, AddClassPostConstruct = function() end,
            AddComponentPostInit = function() end, AddSimPostInit = function() end,
            AddGamePostInit = function() end, AddClientModRPCHandler = function() end, modimport = function() end,
        }
        local fn16b = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setfenv(fn16b, env16b)
        fn16b()
        local snap16b = env16b.HS_PanelApi.GetSnapshot()
        assert(snap16b.preset == 3 and snap16b.slot_saved[1] == true
            and math.abs(snap16b.tune.tint - 0.25) < 1e-3 and math.abs(snap16b.tune.slope_r - 1.22) < 1e-3
            and math.abs(snap16b.tune.surface_light - 1.15) < 1e-3 and math.abs(snap16b.tune.glint_str - 0.80) < 1e-3
            and math.abs(snap16b.tune.snow_cap - 0.40) < 1e-3,
            "Restart persistence roundtrip failed for 74-knob state or custom slot 1!")

        -- 6) Standalone BCAS Studio Coexistence Auto-Yield (PostFX + Native World Studio!)
        env16.HS_PanelApi.SetPreset(2)
        assert(pp_en16[g16.PostProcessorEffects.HS_VisualFinal] == true and native_ws16[16] == 1.0, "Expected active before BCAS detect")
        g16.BCAS_State = { active = true }
        env16.HS_PanelApi.SetPreset(2)
        assert(pp_en16[g16.PostProcessorEffects.HS_VisualFinal] == false and native_ws16[1] == 0.0 and native_ws16[9] == 0.0 and native_ws16[16] == 0.0,
            "Must auto-yield BOTH PostFX and Native World Studio when BCAS_State is present!")
        g16.BCAS_State = nil

        -- 7) Transactional HS1 Share Code (Validate -> Atomic Apply -> Persist + Undo) & Human-Readable JSON Preset Library!
        env16.HS_PanelApi.SetPreset(2)
        g16.c_hstune("tint", 0.28)
        g16.c_hstune("slope_r", 1.18)
        g16.c_hstune("surface_light", 1.12)
        g16.c_hstune("glint_str", 0.78)
        g16.c_hstune("snow_cap", 0.45)

        -- PreviewShareCode MUST NOT overwrite clipboard!
        mock_clip16 = "FRIEND_CLIPBOARD_TOKEN"
        clip_writes16 = 0
        local preview_code = env16.HS_PanelApi.PreviewShareCode("")
        assert(type(preview_code) == "string" and preview_code:sub(1, 4) == "HS1-", "PreviewShareCode must emit HS1- prefix!")
        assert(clip_writes16 == 0 and mock_clip16 == "FRIEND_CLIPBOARD_TOKEN", "PreviewShareCode must NEVER overwrite clipboard!")

        -- ExportShareCode WITH UTF-8 title ("暗黑童话") vs WITHOUT title (shorter!)
        local code_named, copied_n, diffs_n = env16.HS_PanelApi.ExportShareCode("暗黑童话")
        assert(code_named:sub(1, 4) == "HS1-" and copied_n == true and mock_clip16 == code_named and (diffs_n or 0) >= 5,
            "ExportShareCode with title failed!")
        local code_short = env16.HS_PanelApi.PreviewShareCode("")
        assert(#code_short < #code_named, "Untitled HS1 code must be strictly shorter than titled code!")

        -- Reset to Preset 0 (known baseline) before testing transactional rejection & valid import
        env16.HS_PanelApi.SetPreset(0)
        local snap_before_bad = env16.HS_PanelApi.GetSnapshot()
        local saved_cfg_before = persist_store16["hs_fps_v25.cfg"]

        -- Corrupt 1 character in HS1 payload -> CRC16 MUST reject with ZERO mutation!
        local last_c = code_named:sub(-1)
        local bad_c = (last_c == "A") and "B" or "A"
        local bad_crc_code = code_named:sub(1, -2) .. bad_c
        local ok_bad1, err_bad1 = env16.HS_PanelApi.ImportShareCode(bad_crc_code)
        assert(not ok_bad1 and type(err_bad1) == "string", "Corrupt CRC16 share code MUST be rejected!")

        -- Truncated, unknown version, malformed JSON, out-of-range JSON -> ALL MUST fail with ZERO mutation!
        local bad_cases = {
            code_named:sub(1, #code_named - 4),
            "HS9-AAAAAAAAAA",
            "{not valid json",
            '{"tune":{"sharp":999}}',
            '{"tune":{"unknown_knob_xyz":1}}',
            '{"flags":{"af16":"not_a_bool"}}',
        }
        for bi = 1, #bad_cases do
            local ok_b = env16.HS_PanelApi.ImportShareCode(bad_cases[bi])
            assert(not ok_b, "Malformed/out-of-range case #" .. bi .. " MUST be rejected!")
        end
        local snap_after_bad = env16.HS_PanelApi.GetSnapshot()
        assert(snap_after_bad.preset == snap_before_bad.preset
            and math.abs(snap_after_bad.tune.slope_r - snap_before_bad.tune.slope_r) < 1e-6
            and math.abs(snap_after_bad.tune.surface_light - snap_before_bad.tune.surface_light) < 1e-6
            and persist_store16["hs_fps_v25.cfg"] == saved_cfg_before,
            "Corrupt share codes MUST NOT overwrite live config or disk!")

        -- Phase 1 Dry-Run Validate (read-only!) MUST succeed AND keep Preset 0 untouched!
        local ok_val, _, st_val = env16.HS_PanelApi.ValidateShareCode(code_named)
        assert(ok_val and st_val and st_val.name == "暗黑童话" and (st_val.diff_count or 0) >= 5, "ValidateShareCode dry-run failed!")
        assert(env16.HS_PanelApi.GetSnapshot().preset == 0, "ValidateShareCode must be strictly read-only!")

        -- Phase 2 + 3 Transactional Import -> applies exact 74-knob state, sets scheme_name, persists JSON library!
        local ok_imp, err_imp, st_imp = env16.HS_PanelApi.ImportShareCode(code_named)
        assert(ok_imp and st_imp and st_imp.name == "暗黑童话", "Valid HS1 import failed: " .. tostring(err_imp))
        local snap_imp = env16.HS_PanelApi.GetSnapshot()
        assert(snap_imp.preset == 3 and snap_imp.scheme_name == "暗黑童话" and snap_imp.has_undo == true
            and math.abs(snap_imp.tune.tint - 0.28) < 1e-3
            and math.abs(snap_imp.tune.slope_r - 1.18) < 1e-3
            and math.abs(snap_imp.tune.surface_light - 1.12) < 1e-3
            and math.abs(snap_imp.tune.glint_str - 0.78) < 1e-3
            and math.abs(snap_imp.tune.snow_cap - 0.45) < 1e-3,
            "Imported HS1 share code did not restore exact 74-knob values!")
        assert(type(persist_store16["hypersmooth_presets.json"]) == "string"
            and persist_store16["hypersmooth_presets.json"]:find("hypersmooth_visual_library_v1", 1, true) ~= nil,
            "Transactional import must persist hypersmooth_presets.json!")

        -- 1-Click Undo -> restores Preset 0 cleanly!
        assert(env16.HS_PanelApi.UndoLastImport() == true, "UndoLastImport failed!")
        assert(env16.HS_PanelApi.GetSnapshot().preset == 0, "UndoLastImport did not restore Preset 0!")

        -- Human-Readable JSON Preset Library save -> reload -> load + corrupt JSON guard!
        env16.HS_PanelApi.SetPreset(1)
        g16.c_hstune("surface_light", 0.92)
        g16.c_hsjson("save", "洞穴护眼")
        assert(persist_store16["hypersmooth_presets.json"]:find("洞穴护眼", 1, true) ~= nil, "c_hsjson save did not write named entry!")
        env16.HS_PanelApi.SetPreset(0)
        local rel_ok, rel_cnt = false, 0
        env16.HS_PanelApi.ReloadJsonLibrary(function(ok_r, cnt_r) rel_ok, rel_cnt = ok_r, cnt_r end)
        assert(rel_ok and rel_cnt >= 1, "ReloadJsonLibrary failed to parse hypersmooth_presets.json!")
        g16.c_hsjson("load", "洞穴护眼")
        local snap_jload = env16.HS_PanelApi.GetSnapshot()
        assert(snap_jload.scheme_name == "洞穴护眼" and math.abs(snap_jload.tune.surface_light - 0.92) < 1e-3,
            "c_hsjson load failed to restore named JSON preset!")
        -- Corrupt JSON on disk -> ReloadJsonLibrary MUST fail safely without clobbering live config!
        persist_store16["hypersmooth_presets.json"] = "{corrupt_json!!!"
        local bad_rel_ok = true
        env16.HS_PanelApi.ReloadJsonLibrary(function(ok_r) bad_rel_ok = ok_r end)
        assert(bad_rel_ok == false and math.abs(env16.HS_PanelApi.GetSnapshot().tune.surface_light - 0.92) < 1e-3,
            "Corrupt hypersmooth_presets.json must fail safely without mutating live state!")

        print("[CLIENT_V24_TEST] 16. Full 74-Knob 3-Floor Visual Studio + Transactional HS1 ShareCode & JSON Preset Library PASS")
    end

    -- ------------------------------------------------------------------------
    -- TEST 17: HS_EXT_V1 Standard Shader Extension Runtime + Real Reference Mod
    --          (Load-Order Independence, Strict Validation, 6-Stage DAG Sort,
    --           Multi-Pass Subpasses, Exclusive Groups, Shared Context Bus,
    --           Fault-Isolation Circuit Breaker, Master Bypass, Native GPU
    --           Telemetry Bridge, & hypersmooth_presets.json "plugins" Sync)
    -- ------------------------------------------------------------------------
    do
        local pp_u17, pp_en17, pp_after17, pp_before17, pp_sub17 = {}, {}, {}, {}, {}
        local probe_calls17 = {}
        local persist_store17 = {}
        local gpu_mock17 = {
            ["__core.bloom"]        = { 1, 0.11, 0.15, 200, true },
            ["__core.visual_final"] = { 1, 0.22, 0.30, 200, true },
            ["demo.vintage_crt"]    = { 1, 0.14, 0.19, 120, true },
            ["demo.crt_alt"]        = { 1, 0.18, 0.25, 180, true },
        }
        local pp_idx17 = {
            SetBloomSamplerParams = function() end,
            AddUniformVariable = function(_, n) return n end,
            AddSamplerEffect = function(_, p) pp_sub17[p] = true; return p end,
            AddTextureSampler = function(_, p) return "TEX:" .. tostring(p) end,
            SetTextureSamplerFilter = function() end,
            SetTextureSamplerState = function() end,
            AddSampler = function() end,
            SetEffectUniformVariables = function() end,
            SetSamplerEffectState = function() end,
            SetSamplerEffectFilter = function() end,
            AddPostProcessEffect = function(_, p) return p end,
            SetPostProcessEffectAfter = function(_, src, dst) pp_after17[src] = dst end,
            SetPostProcessEffectBefore = function(_, src, dst) pp_before17[src] = dst end,
            SetUniformVariable = function(_, id, a, b, c, d) pp_u17[id] = { a, b, c, d } end,
            EnablePostProcessEffect = function(_, id, en) pp_en17[id] = en end,
            SetBloomEnabled = function() end,
            IsBloomEnabled = function() return true end,
            SetColourCubeData = function() end,
            SetColourCubeLerp = function() end,
            SetDistortionFactor = function() end,
        }
        local pp17 = setmetatable({}, { __index = pp_idx17 })
        local sh_init17, sh_sort17 = {}, {}

        local g17 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table, unpack = G.unpack,
            tonumber = G.tonumber, tostring = G.tostring, error = G.error, print = function() end,
            json = G.json,
            HyperSmooth_ExtRegisterProbe = function(id, uni)
                probe_calls17[id] = uni or ""
                return 1
            end,
            HyperSmooth_ExtGetGpuStats = function(id)
                local m = gpu_mock17[id]
                if m then return m[1], m[2], m[3], m[4], m[5] end
                return 0, 0, 0, 0, false
            end,
            TheWorld = { state = { phase = "day", time = 0.42, israining = true, precipitationrate = 0.65, snowlevel = 0.25 }, HasTag = function() return false end },
            ThePlayer = { replica = { sanity = { GetPercent = function() return 0.72 end } } },
            TheCamera = { distance = 36, heading = 45, GetRightVec = function() return { x = 1, y = 0, z = 0 } end, GetDownVec = function() return { x = 0, y = 0, z = 1 } end },
            TheSim = {
                GetPersistentString = function(_, fname, cb) local v = persist_store17[fname]; cb(v ~= nil, v or "") end,
                SetPersistentString = function(_, fname, payload) persist_store17[fname] = payload or "" end,
                GetScreenSize = function() return 1920, 1080 end,
                FindEntities = function() return {} end,
            },
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyDownHandler = function() end, OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            PostProcessor = pp17,
            PostProcessorEffects = { Bloom = "VAN_BLOOM", ColourCube = "VAN_CC" },
            SamplerEffects = {},
            UniformVariables = { SAMPLER_PARAMS = "SAMPLER_PARAMS" },
            SamplerSizes = { Relative = 1, Static = 2 },
            SamplerColourMode = { RGB = 1 },
            SamplerEffectBase = { BloomSampler = 1, Shader = 2, Texture = 3, Smoke = 4 },
            FILTER_MODE = { LINEAR = 1, POINT = 2 },
            MIP_FILTER_MODE = { NONE = 1 },
            WRAP_MODE = { CLAMP_TO_EDGE = 1, REPEAT = 2 },
            KnownModIndex = { GetModInfo = function() return {} end },
            require = function() return {} end,
            debug = G.debug, Settings = {}, TheFrontEnd = {},
            GetTick = function() return 0 end, GetTimeRealSeconds = function() return 0 end,
            IsPaused = function() return false end, TUNING = {}, scheduler = {},
            ANCHOR_RIGHT = 2, ANCHOR_TOP = 1, NUMBERFONT = "stint-ucr", CLIENT_MOD_RPC = {},
            LanguageTranslator = { defaultlang = "zh" },
            STRINGS = { UI = { OPTIONS = { ENABLED = "启用", DISABLED = "禁用" } } },
        }
        -- Attach real Klei strict.lua-equivalent metatable onto g17 so ANY undeclared global
        -- read/write (like `G.HS_EXT_QUEUE` without rawget/rawset) immediately fails the test!
        local strict_mt17 = {
            __index = function(_, k)
                G.error("variable '" .. tostring(k) .. "' is not declared", 2)
            end,
            __newindex = function(_, k)
                G.error("assign to undeclared variable '" .. tostring(k) .. "'", 2)
            end,
        }
        G.setmetatable(g17, strict_mt17)

        -- 1) Load REAL third-party reference mod BEFORE HyperSmooth under strict.lua -> enters GLOBAL.HS_EXT_QUEUE!
        local env_demo = { GLOBAL = g17, MODROOT = "../mods/example_hs_crt_vintage/", Asset = function() return {} end }
        local fn_demo, err_demo = G.kleiloadlua("../mods/client_v24_lua_test/target_example_crt_modmain.lua")
        assert(fn_demo ~= nil, "Failed to load reference plugin modmain: " .. tostring(err_demo))
        G.setfenv(fn_demo, env_demo)
        fn_demo()
        assert(type(G.rawget(g17, "HS_EXT_QUEUE")) == "table" and #G.rawget(g17, "HS_EXT_QUEUE") == 1, "Reference mod must queue into GLOBAL.HS_EXT_QUEUE!")

        -- 2) Load HyperSmooth modmain WITH real hs_ext.lua via modimport!
        local env17 = {
            GLOBAL = g17,
            MODROOT = "../mods/client_v24_lua_test/",
            Asset = function() return {} end,
            GetModConfigData = function() return nil end,
            AddModShadersInit = function(fn) sh_init17[#sh_init17 + 1] = fn end,
            AddModShadersSortAndEnable = function(fn) sh_sort17[#sh_sort17 + 1] = fn end,
            AddPrefabPostInit = function() end,
            AddClassPostConstruct = function() end,
            AddComponentPostInit = function() end,
            AddSimPostInit = function() end,
            AddGamePostInit = function() end,
            AddClientModRPCHandler = function() end,
        }
        env17.modimport = function(modfile)
            if modfile == "hs_ext" then
                local fext, eext = G.kleiloadlua("../mods/client_v24_lua_test/target_hs_ext.lua")
                assert(fext ~= nil, "hs_ext.lua load failed: " .. tostring(eext))
                G.setfenv(fext, env17)
                fext()
            end
        end
        local fn17, err17 = G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua")
        assert(fn17 ~= nil, "modmain load failed in Test 17: " .. tostring(err17))
        G.setfenv(fn17, env17)
        fn17()

        assert(type(g17.HyperSmooth) == "table" and g17.HyperSmooth.ABI_VERSION == 1, "GLOBAL.HyperSmooth ABI v1 missing!")
        assert(g17.HyperSmooth.GetPlugin("demo.vintage_crt") ~= nil, "Failed to drain pre-loaded plugin from HS_EXT_QUEUE!")

        -- Idempotent duplicate registration + strict rejection of malformed spec
        local ok_dup = g17.HyperSmooth.RegisterPlugin(g17.HS_EXT_QUEUE[1])
        assert(ok_dup == true and #g17.HyperSmooth.GetPlugins() == 1, "Duplicate registration must be idempotent!")
        local ok_bad_spec, err_bad_spec = g17.HyperSmooth.RegisterPlugin({ abi = 99, id = "bad.abi", stage = "FINAL_FX", shader = "x.ksh" })
        assert(ok_bad_spec == false and type(err_bad_spec) == "string", "Invalid ABI spec must be rejected!")

        -- 3) Register 2nd plugin AFTER HyperSmooth (POST_SHARPEN, same exclusive_group, multi-pass subpass + SUN/ENV ctx!)
        local ok_p2, err_p2 = g17.HyperSmooth.RegisterPlugin({
            abi = 1, id = "demo.crt_alt", version = "2.0.0", author = "Test",
            name = { zh = "备用锐化扩展", en = "Alt Sharpen Ext" },
            stage = "POST_SHARPEN", order = 50, exclusive_group = "stylized_retro",
            shader = "shaders/alt_sharpen.ksh",
            context = { "SUN", "ENV" },
            subpasses = {
                { name = "down", shader = "shaders/alt_down.ksh", size_mode = "RELATIVE", w = 0.5, h = 0.5, base = "BLOOM_SAMPLER" },
            },
            samplers = {
                { base = "SUBPASS", name = "down" },
            },
            params = {
                { key = "gain", type = "float", min = 0.0, max = 2.0, step = 0.1, default = 0.8 },
            },
        })
        assert(ok_p2 == true, "RegisterPlugin #2 failed: " .. tostring(err_p2))

        -- Register 3rd plugin whose on_frame intentionally throws -> tests Fault-Isolation Circuit Breaker!
        local ok_p3 = g17.HyperSmooth.RegisterPlugin({
            abi = 1, id = "demo.faulty_frame", version = "0.1.0",
            name = "Faulty Plugin", stage = "COLOR_GRADE", order = 10,
            shader = "shaders/faulty.ksh",
            on_frame = function() error("simulated third-party math crash!") end,
        })
        assert(ok_p3 == true, "RegisterPlugin #3 failed!")

        -- 4) Run Klei BuildModShaders & SortAndEnable lifecycle!
        for _, fn in ipairs(sh_init17) do fn() end
        for _, fn in ipairs(sh_sort17) do fn() end

        assert(probe_calls17["__core.bloom"] == "HS_BLOOM_PARAMS" and probe_calls17["__core.visual_final"] == "HS_VISUAL_PARAMS", "Core GPU profiler probes missing!")
        assert(probe_calls17["demo.vintage_crt"] == "HSEXT_DEMO_VINTAGE_CRT_U0", "Plugin #1 GPU probe not registered!")
        assert(probe_calls17["demo.crt_alt"] == "HSEXT_DEMO_CRT_ALT_U0", "Plugin #2 GPU probe not registered!")
        assert(pp_sub17["shaders/alt_down.ksh"] == true, "Plugin #2 offscreen subpass was not allocated!")
        -- Verify deterministic stage DAG: COLOR_GRADE before HS_VisualFinal -> POST_SHARPEN after HS_VisualFinal -> FINAL_FX after POST_SHARPEN!
        local hs_final_id = g17.PostProcessorEffects.HS_VisualFinal
        assert(pp_before17["shaders/faulty.ksh"] == hs_final_id, "COLOR_GRADE must anchor Before HS_VisualFinal!")
        assert(pp_after17["shaders/alt_sharpen.ksh"] == hs_final_id, "POST_SHARPEN must anchor After HS_VisualFinal!")
        assert(pp_after17["../mods/example_hs_crt_vintage/shaders/hs_ext_vintage_crt.ksh"] == "shaders/alt_sharpen.ksh",
            "FINAL_FX must chain After POST_SHARPEN!")

        -- 5) Enable demo.vintage_crt -> packs U0/U1 uniforms; tune scanline & flicker!
        assert(g17.HyperSmooth.SetPluginEnabled("demo.vintage_crt", true) == true)
        assert(pp_en17["../mods/example_hs_crt_vintage/shaders/hs_ext_vintage_crt.ksh"] == true)
        assert(math.abs(pp_u17["HSEXT_DEMO_VINTAGE_CRT_U0"][1] - 0.30) < 1e-4 and math.abs(pp_u17["HSEXT_DEMO_VINTAGE_CRT_U0"][4] - 0.35) < 1e-4,
            "Initial U0 param packing mismatch!")
        assert(g17.HyperSmooth.SetPluginParam("demo.vintage_crt", "scanline", 0.75) == true)
        assert(g17.HyperSmooth.SetPluginParam("demo.vintage_crt", "flicker", true) == true)
        assert(math.abs(pp_u17["HSEXT_DEMO_VINTAGE_CRT_U0"][1] - 0.75) < 1e-4 and pp_u17["HSEXT_DEMO_VINTAGE_CRT_U1"][1] == 1.0,
            "Live param update U0/U1 mismatch!")

        -- 6) Exclusive Group Arbitration: enabling demo.crt_alt MUST auto-disable demo.vintage_crt!
        assert(g17.HyperSmooth.SetPluginEnabled("demo.crt_alt", true) == true)
        assert(g17.HyperSmooth.GetPlugin("demo.vintage_crt").enabled == false, "Exclusive group failed to disable competing plugin!")
        assert(pp_en17["../mods/example_hs_crt_vintage/shaders/hs_ext_vintage_crt.ksh"] == false)
        assert(pp_en17["shaders/alt_sharpen.ksh"] == true)

        -- 7) Master Bypass (1-click panic switch)
        g17.HyperSmooth.SetMasterBypass(true)
        assert(pp_en17["shaders/alt_sharpen.ksh"] == false, "MasterBypass=true must disable active plugin pass!")
        g17.HyperSmooth.SetMasterBypass(false)
        assert(pp_en17["shaders/alt_sharpen.ksh"] == true, "MasterBypass=false must restore active plugin pass!")

        -- 8) Shared Context Bus + Fault-Isolation Circuit Breaker on on_frame crash!
        assert(g17.HyperSmooth.SetPluginEnabled("demo.faulty_frame", true) == true)
        env17.HS_ExtRuntime.Tick(0.05, { sun_sx = 0.40, sun_sy = 0.25, sun_elev = 0.80, day_factor = 0.90 })
        local p_fault = g17.HyperSmooth.GetPlugin("demo.faulty_frame")
        assert(p_fault.faulted == true and p_fault.enabled == false and pp_en17["shaders/faulty.ksh"] == false,
            "Faulty plugin must be isolated by circuit breaker!")
        assert(g17.HyperSmooth.GetPlugin("demo.crt_alt").enabled == true, "Circuit breaker must NOT affect healthy plugins!")
        assert(pp_u17["HS_CTX_SUN"] ~= nil and math.abs(pp_u17["HS_CTX_SUN"][1] - 0.40) < 1e-4, "HS_CTX_SUN context bus not fed!")
        assert(pp_u17["HS_CTX_ENV"] ~= nil and math.abs(pp_u17["HS_CTX_ENV"][1] - 0.65) < 1e-4 and math.abs(pp_u17["HS_CTX_ENV"][4] - 0.72) < 1e-4,
            "HS_CTX_ENV context bus not fed!")

        -- 9) Native Hardware GPU Profiler Telemetry Snapshot
        local esnap = env17.HS_PanelApi.GetExtSnapshot()
        assert(esnap.total_count == 3 and esnap.active_count == 1 and esnap.fault_count == 1, "Ext snapshot counts wrong!")
        assert(math.abs(esnap.total_gpu_ms - 0.18) < 1e-3 and esnap.core_final.is_hw == true, "Native GPU telemetry bridge mismatch!")

        -- 10) hypersmooth_presets.json "plugins" Persistence + Unloaded Plugin Preservation!
        local saved_json17 = persist_store17["hypersmooth_presets.json"]
        assert(type(saved_json17) == "string" and saved_json17:find('"plugins"', 1, true) ~= nil
            and saved_json17:find('"demo.vintage_crt"', 1, true) ~= nil and saved_json17:find("0.7500", 1, true) ~= nil,
            "Plugin params not persisted to hypersmooth_presets.json!")

        -- Fresh reload where ONLY demo.vintage_crt is installed -> restores scanline=0.75 AND preserves unloaded demo.crt_alt!
        local g17b = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table, unpack = G.unpack,
            tonumber = G.tonumber, tostring = G.tostring, error = G.error, print = function() end,
            json = G.json,
            TheSim = {
                GetPersistentString = function(_, fname, cb) local v = persist_store17[fname]; cb(v ~= nil, v or "") end,
                SetPersistentString = function(_, fname, payload) persist_store17[fname] = payload or "" end,
            },
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyDownHandler = function() end, OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            KnownModIndex = { GetModInfo = function() return {} end },
            require = function() return {} end,
            debug = G.debug, Settings = {}, TheFrontEnd = {},
            GetTick = function() return 0 end, GetTimeRealSeconds = function() return 0 end,
            IsPaused = function() return false end, TUNING = {}, scheduler = {},
            ANCHOR_RIGHT = 2, ANCHOR_TOP = 1, NUMBERFONT = "stint-ucr", CLIENT_MOD_RPC = {},
            LanguageTranslator = { defaultlang = nil },
            STRINGS = { UI = { OPTIONS = { ENABLED = "Enabled", DISABLED = "Disabled" } } },
        }
        G.setmetatable(g17b, strict_mt17)
        local env_demo_b = { GLOBAL = g17b, MODROOT = "../mods/example_hs_crt_vintage/", Asset = function() return {} end }
        G.setfenv(fn_demo, env_demo_b); fn_demo()
        local env17b = {
            GLOBAL = g17b, MODROOT = "../mods/client_v24_lua_test/", Asset = function() return {} end,
            GetModConfigData = function() return nil end,
            AddModShadersInit = function() end, AddModShadersSortAndEnable = function() end,
            AddPrefabPostInit = function() end, AddClassPostConstruct = function() end,
            AddComponentPostInit = function() end, AddSimPostInit = function() end,
            AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
        }
        env17b.modimport = function(modfile)
            if modfile == "hs_ext" then
                local fext = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_hs_ext.lua"))
                G.setfenv(fext, env17b); fext()
            end
        end
        local fn17b = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setfenv(fn17b, env17b); fn17b()
        -- Verify automatic official locale detection (zh -> true; en/nil -> false) under strict.lua!
        assert(env17.HS_IsUiZh() == true, "HS_IsUiZh must return true when defaultlang='zh'!")
        assert(env17b.HS_IsUiZh() == false, "HS_IsUiZh must return false when defaultlang=nil & ENABLED='Enabled'!")
        g17b.LanguageTranslator.defaultlang = "zht"
        assert(env17b.HS_IsUiZh() == true, "HS_IsUiZh must return true when defaultlang='zht'!")
        g17b.LanguageTranslator.defaultlang = "en"
        assert(env17b.HS_IsUiZh() == false, "HS_IsUiZh must return false when defaultlang='en'!")
        local p_restored = g17b.HyperSmooth.GetPlugin("demo.vintage_crt")
        assert(p_restored and math.abs(p_restored.values.scanline - 0.75) < 1e-3 and p_restored.values.flicker == 1.0,
            "Restart reload failed to restore plugin params from hypersmooth_presets.json!")
        local re_json = env17b.HS_PanelApi.ExportJsonLibrary()
        assert(re_json:find('"demo.crt_alt"', 1, true) ~= nil,
            "Re-exporting JSON must passively preserve unloaded plugins' saved configs!")

        print("[CLIENT_V24_TEST] 17. HS_EXT_V1 + strict.lua Guard + Bilingual Auto-Locale + GPU Profiler + JSON Plugins PASS")
    end

    -- =========================================================================
    -- TEST 18: Fluid Shadow & Lighting Engine v2 (240Hz dt continuous arc,
    --   zero dusk freeze, multi-light night damping, Q/E shortest-arc damping,
    --   wind clock pause safety, 1.0x HD BloomSampler, & Shadow-Creature Exclusion)
    -- =========================================================================
    do
        local ws_calls, sd_calls, pp_unis = {}, {}, {}
        local paused_flag = false
        local hud_onupdate = nil
        local prefab_hooks = {}
        local prefab_any_hooks = {}
        local sh_init_18, sh_sort_18 = nil, nil
        local bloom_params_18 = nil
        local cam_angle = 0.0
        local mock_lights = {}

        local g18 = {
            print = function() end,
            pcall = G.pcall, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            type = G.type, tonumber = G.tonumber, tostring = G.tostring,
            math = G.math, string = G.string, table = G.table,
            rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            json = G.json,
            resolvefilepath = function(p) return "resolved/" .. tostring(p) end,
            IsPaused = function() return paused_flag end,
            GetTick = function() return 0 end, GetTimeRealSeconds = function() return 0 end,
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyDownHandler = function() end, OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            KnownModIndex = { GetModInfo = function() return {} end },
            TheFrontEnd = {}, Settings = {}, TUNING = {}, scheduler = {},
            ANCHOR_RIGHT = 2, ANCHOR_TOP = 1, NUMBERFONT = "stint-ucr", CLIENT_MOD_RPC = {},
            LanguageTranslator = { defaultlang = "zh" },
            STRINGS = { UI = { OPTIONS = { ENABLED = "Enabled", DISABLED = "Disabled" } } },
            TheWorld = {
                state = { phase = "day", timeinphase = 0.50, time = 0.25, isfullmoon = false },
                HasTag = function() return false end,
            },
            ThePlayer = {
                Transform = { GetWorldPosition = function() return 0.0, 0.0, 0.0 end },
            },
            TheCamera = {
                GetRightVec = function() return { x = math.cos(cam_angle), y = 0, z = math.sin(cam_angle) } end,
                GetDownVec  = function() return { x = -math.sin(cam_angle), y = 0, z = math.cos(cam_angle) } end,
            },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                FindEntities = function() return mock_lights end,
            },
            require = function(mod)
                if mod == "widgets/text" then
                    return function()
                        return {
                            inst = { IsValid = function() return true end },
                            SetHAnchor = function() end, SetVAnchor = function() end,
                            SetRegionSize = function() end, SetHAlign = function() end,
                            SetPosition = function() end, SetColour = function() end,
                            SetClickable = function() end, Hide = function() end,
                            Show = function() end, SetString = function() end,
                        }
                    end
                end
                return {}
            end,
            SamplerSizes = { Relative = 1 },
            SamplerColourMode = { RGB = 1 },
            SamplerEffectBase = { BloomSampler = 1, Shader = 2 },
            FILTER_MODE = { LINEAR = 1 }, MIP_FILTER_MODE = { NONE = 0 }, WRAP_MODE = { CLAMP_TO_EDGE = 1 },
            PostProcessorEffects = { Bloom = 1, ColourCube = 2 },
            SamplerEffects = {},
            UniformVariables = {},
            PostProcessor = {
                SetBloomSamplerParams = function(_, sm, w, h, cm) bloom_params_18 = { sm, w, h, cm } end,
                AddUniformVariable = function(_, n) return "U_" .. n end,
                AddSamplerEffect = function(_, p) return "SE_" .. p end,
                AddPostProcessEffect = function(_, p) return "PPE_" .. p end,
                AddSampler = function() end, SetEffectUniformVariables = function() end,
                SetSamplerEffectState = function() end, SetSamplerEffectFilter = function() end,
                SetPostProcessEffectAfter = function() end, EnablePostProcessEffect = function() end,
                SetUniformVariable = function(_, uid, a, b, c, d) pp_unis[uid] = { a, b, c, d } end,
                IsBloomEnabled = function() return false end,
                SetBloomEnabled = function() end,
            },
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_Version = function() return "2.8.0" end,
            HyperSmooth_SetWorldStudio = function(...) ws_calls[#ws_calls + 1] = { ... }; return true end,
            HyperSmooth_SetShadowDyn   = function(...) sd_calls[#sd_calls + 1] = { ... }; return true end,
        }
        local env18 = {
            GLOBAL = g18, MODROOT = "../mods/client_v24_lua_test/", Asset = function(t, p) return { t, p } end,
            GetModConfigData = function(k) if k == "visual_preset" then return 2 end return nil end,
            AddModShadersInit = function(fn) sh_init_18 = fn end,
            AddModShadersSortAndEnable = function(fn) sh_sort_18 = fn end,
            AddPrefabPostInit = function(name, fn)
                prefab_hooks[name] = prefab_hooks[name] or {}
                prefab_hooks[name][#prefab_hooks[name] + 1] = fn
            end,
            AddPrefabPostInitAny = function(fn) prefab_any_hooks[#prefab_any_hooks + 1] = fn end,
            AddClassPostConstruct = function(cls, fn)
                if cls == "screens/playerhud" then
                    local mock_hud = { AddChild = function(_, w) return w end }
                    fn(mock_hud)
                    hud_onupdate = mock_hud.OnUpdate
                end
            end,
            AddComponentPostInit = function() end, AddSimPostInit = function() end,
            AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
            modimport = function() end,
        }
        local fn18 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setfenv(fn18, env18); fn18()
        assert(sh_init_18 and sh_sort_18 and hud_onupdate, "Test 18 hooks missing")
        sh_init_18(); sh_sort_18()
        assert(bloom_params_18 and bloom_params_18[2] == 1.0 and bloom_params_18[3] == 1.0,
            "Test 18: BloomSampler must initialize at 1.0x HD!")

        -- 1) Verify ZERO 10Hz throttle: every single 240Hz frame (dt=1/240) updates SetWorldStudio & SetShadowDyn!
        ws_calls, sd_calls = {}, {}
        for f = 1, 10 do
            hud_onupdate({}, 1.0 / 240.0)
        end
        assert(#ws_calls == 10 and #sd_calls == 10,
            "Test 18: Expected 10/10 240Hz per-frame updates (zero 10Hz throttle), got ws=" .. #ws_calls .. " sd=" .. #sd_calls)
        -- Wind clock advances monotonically on every 240Hz frame and stops when paused!
        assert(sd_calls[10][1] > sd_calls[1][1] + 0.03, "Test 18: windTimeSec must advance continuously with dt!")
        local t_before_pause = sd_calls[10][1]
        paused_flag = true
        hud_onupdate({}, 1.0 / 60.0)
        assert(math.abs(sd_calls[#sd_calls][1] - t_before_pause) < 1e-6, "Test 18: windTimeSec must freeze when game paused!")
        paused_flag = false

        -- 2) Verify ZERO dusk freeze: direction & stretch BOTH continue evolving smoothly across dusk!
        g18.TheWorld.state.phase = "dusk"
        g18.TheWorld.state.timeinphase = 0.05
        hud_onupdate({}, 0.05)
        local d0 = ws_calls[#ws_calls]
        for step = 1, 20 do
            g18.TheWorld.state.timeinphase = 0.05 + step * 0.04
            hud_onupdate({}, 0.05)
        end
        local d1 = ws_calls[#ws_calls]
        assert(d1[15] > d0[15] + 0.15, "Test 18: Dusk shadow stretch must continue lengthening (was frozen before!), got " .. d0[15] .. " -> " .. d1[15])
        assert(math.abs(d1[13] - d0[13]) > 0.01 or math.abs(d1[14] - d0[14]) > 0.01, "Test 18: Dusk shadow direction must glide continuously!")

        -- 3) Verify Multi-Light Night Damping (NO 180-deg instant snap when walking between two campfires!)
        g18.TheWorld.state.phase = "night"
        g18.TheWorld.state.timeinphase = 0.3
        local fireA = {
            Light = { IsEnabled = function() return true end, GetCalculatedRadius = function() return 6.0 end },
            Transform = { GetWorldPosition = function() return 5.0, 0, 0.0 end },
        }
        local fireB = {
            Light = { IsEnabled = function() return true end, GetCalculatedRadius = function() return 6.0 end },
            Transform = { GetWorldPosition = function() return -5.0, 0, 0.0 end },
        }
        mock_lights = { fireA }
        for _ = 1, 40 do hud_onupdate({}, 0.05) end
        local before_flip = ws_calls[#ws_calls]
        -- Instantly switch dominant light to opposite side (-5, 0) -> single 4ms frame MUST smoothly damp, NEVER snap 180 deg!
        mock_lights = { fireB }
        hud_onupdate({}, 0.06) -- trigger 20Hz light scan + 1 damped step
        local after_1step = ws_calls[#ws_calls]
        local delta_x = math.abs(after_1step[13] - before_flip[13])
        assert(delta_x > 0.02 and delta_x < 0.85,
            "Test 18: Night firelight switch must smoothly damp rather than teleport 180 deg! delta_x=" .. tostring(delta_x))

        -- 4) Verify Shadow-Creature Ethereal Exclusion (crawlinghorror + modded shadowcreature tag + reversible Preset 0!)
        local function make_shadow_mock(tags)
            local obj = {
                bloom_handle = nil,
                cleared = 0,
                IsValid = function() return true end,
                HasTag = function(_, t) return tags[t] == true end,
            }
            obj.AnimState = {
                SetBloomEffectHandle = function(_, h) obj.bloom_handle = h end,
                ClearBloomEffectHandle = function() obj.bloom_handle = nil; obj.cleared = obj.cleared + 1 end,
            }
            return obj
        end
        local ch = make_shadow_mock({ shadowcreature = true })
        assert(prefab_hooks["crawlinghorror"] and #prefab_hooks["crawlinghorror"] >= 1, "Missing crawlinghorror hook")
        prefab_hooks["crawlinghorror"][1](ch)
        assert(ch.bloom_handle == "shaders/anim_bloom_ghost.ksh",
            "Test 18: crawlinghorror must receive preloaded shaders/anim_bloom_ghost.ksh! got=" .. tostring(ch.bloom_handle))

        local modded_sc = make_shadow_mock({ shadowcreature = true })
        for i = 1, #prefab_any_hooks do prefab_any_hooks[i](modded_sc) end
        assert(modded_sc.bloom_handle == "shaders/anim_bloom_ghost.ksh",
            "Test 18: Modded shadowcreature-tagged entity must receive preloaded shaders/anim_bloom_ghost.ksh!")

        -- Toggle exclusion OFF via c_hstune -> MUST cleanly restore ClearBloomEffectHandle()!
        g18.c_hstune("shadow_skip_nightmare", 0)
        assert(ch.bloom_handle == nil and ch.cleared >= 1 and modded_sc.bloom_handle == nil,
            "Test 18: Toggling shadow_skip_nightmare=0 must cleanly clear bloom handle on all shadow creatures!")
        -- Toggle back ON -> re-applies live!
        g18.c_hstune("shadow_skip_nightmare", 1)
        assert(ch.bloom_handle == "shaders/anim_bloom_ghost.ksh" and modded_sc.bloom_handle == "shaders/anim_bloom_ghost.ksh",
            "Test 18: Re-enabling shadow_skip_nightmare=1 must re-apply preloaded bloom shader live!")

        -- 5) Verify new shadow knobs (shadow_length, shadow_sway, shadow_fade) flow into SetWorldStudio & SetShadowDyn!
        g18.c_hstune("shadow_sway", 0.85)
        g18.c_hstune("shadow_fade", 0.72)
        g18.c_hstune("shadow_length", 1.50)
        hud_onupdate({}, 1.0 / 240.0)
        local last_sd = sd_calls[#sd_calls]
        assert(math.abs(last_sd[2] - 0.85) < 1e-3 and math.abs(last_sd[3] - 0.72) < 1e-3,
            "Test 18: shadow_sway/shadow_fade did not propagate to HyperSmooth_SetShadowDyn!")

        print("[CLIENT_V24_TEST] 18. Fluid Shadow Engine v2 (240Hz dt arc + zero dusk freeze + multi-light damping + shadow-creature exclusion) PASS")
    end

    print("[CLIENT_V24_TEST] ALL_PASS")
    G.c_shutdown(false)
end)
"""

def main():
    assert os.path.isdir(BASE_BACKUP), f"Missing {BASE_BACKUP}"
    if os.path.exists(CLUSTER_DIR):
        shutil.rmtree(CLUSTER_DIR)
    shutil.copytree(BASE_BACKUP, CLUSTER_DIR)

    os.makedirs(HARNESS_DIR, exist_ok=True)
    with open(os.path.join(HARNESS_DIR, "modinfo.lua"), "w", encoding="utf-8") as f:
        f.write(HARNESS_MODINFO)
    with open(os.path.join(HARNESS_DIR, "modmain.lua"), "w", encoding="utf-8") as f:
        f.write(HARNESS_MODMAIN)
    shutil.copy2(os.path.join(CLIENT_MOD, "modinfo.lua"), os.path.join(HARNESS_DIR, "target_modinfo.lua"))
    shutil.copy2(os.path.join(CLIENT_MOD, "modmain.lua"), os.path.join(HARNESS_DIR, "target_modmain.lua"))
    shutil.copy2(os.path.join(CLIENT_MOD, "keybind.lua"), os.path.join(HARNESS_DIR, "target_keybind.lua"))
    shutil.copy2(os.path.join(CLIENT_MOD, "hs_ext.lua"), os.path.join(HARNESS_DIR, "target_hs_ext.lua"))
    shutil.copy2(os.path.join(CLIENT_MOD, "hs_panel.lua"), os.path.join(HARNESS_DIR, "target_hs_panel.lua"))
    shutil.copy2(
        os.path.join(os.path.dirname(os.path.dirname(CLIENT_MOD)), "sdk", "example_hs_crt_vintage", "modmain.lua"),
        os.path.join(HARNESS_DIR, "target_example_crt_modmain.lua"),
    )

    # Temporarily stash iphlpapi.dll (restored in finally!) so pure vanilla Klei Lua 5.1 runs without polluting server tests
    dll_path = os.path.join(BIN64, "iphlpapi.dll")
    dll_stash = os.path.join(BIN64, "iphlpapi.dll.client_test_stash")
    stashed = False
    if os.path.exists(dll_path):
        shutil.move(dll_path, dll_stash)
        stashed = True

    try:
        with open(os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua"), "w", encoding="utf-8") as f:
            f.write(f'return {{ ["{HARNESS_NAME}"] = {{ enabled = true, configuration_options = {{}} }} }}\n')

        exe = os.path.join(BIN64, "dontstarve_dedicated_server_nullrenderer_x64.exe")
        proc = subprocess.run(
            [exe, "-persistent_storage_root", STORAGE, "-conf_dir", "DoNotStarveTogether",
             "-cluster", CLUSTER_NAME, "-shard", "Master", "-offline", "-port", "11016",
             "-steam_master_server_port", "27029", "-steam_authentication_port", "8779"],
            cwd=BIN64, capture_output=True, text=True, timeout=60
        )
        out = (proc.stdout or "") + "\n" + (proc.stderr or "")
        out_lines = out.splitlines()
        for idx, line in enumerate(out_lines):
            if "[CLIENT_V24_TEST]" in line or "LUA ERROR" in line or "MOD ERROR" in line or "ERROR:" in line or "assertion failed" in line.lower():
                if "LUA ERROR" in line and idx > 0:
                    print("   [ERR_CTX]", out_lines[idx - 1])
                print(line)
                if "LUA ERROR" in line:
                    for extra in out_lines[idx+1:idx+12]:
                        print("  ", extra)
        assert proc.returncode == 0 and "[CLIENT_V24_TEST] ALL_PASS" in out, f"Lua test failed (rc={proc.returncode})"
    finally:
        if stashed and os.path.exists(dll_stash):
            shutil.move(dll_stash, dll_path)
        shutil.rmtree(HARNESS_DIR, ignore_errors=True)
        shutil.rmtree(CLUSTER_DIR, ignore_errors=True)

if __name__ == "__main__":
    main()
