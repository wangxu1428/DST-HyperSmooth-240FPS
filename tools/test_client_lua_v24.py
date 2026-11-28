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
    assert(bare_env.version == "2.9.7-beta1", "Expected modinfo version 2.9.7-beta1, got " .. tostring(bare_env.version))
    assert(bare_env.dnh_github == "wangxu1428/DST-HyperSmooth-240FPS" and bare_env.dnh_github_asset == "dinput8.dll",
           "Missing DNH bootstrap declaration in modinfo.lua")
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
    assert(bare_env_en.version == "2.9.7-beta1", "Expected EN modinfo version 2.9.7-beta1, got " .. tostring(bare_env_en.version))
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
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
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

        -- Verify Player-First Default: 100% Vanilla Pure (0 forced post-process, 0 world shaders, exact 0.25x BloomSampler!)
        assert(type(env.Assets) == "table" and #env.Assets == 16, "Expected 16 Assets (4 custom shaders + 2 VFX shaders + 6 FX images + lightrays + 2 shade tex + ds_fog1), got " .. tostring(env.Assets and #env.Assets))
        assert(#hooks.sh_init == 1 and #hooks.sh_sort == 1, "Expected ModShadersInit & Sort hooks")
        hooks.sh_init[1]()
        hooks.sh_sort[1]()
        assert(pp_bloom_params == nil,
            "Vanilla Pure (Preset 0) MUST keep Klei's exact stock 0.25x BloomSampler untouched (prevents Magiluminescence bloom distortion)!")
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
            assert(st == "ok" and ver == "2.9.7" and exp == "2.9.7",
                   "Expected ok/2.9.7/2.9.7, got " .. tostring(st) .. "/" .. tostring(ver))
            local snap_ok = env.HS_PanelApi.GetSnapshot()
            assert(snap_ok.dll_status == "ok" and snap_ok.dll_ver == "2.9.7", "Panel snapshot dll_status ok mismatch")

            local saved_ver = fakeG.HyperSmooth_Version
            local saved_fps = fakeG.HyperSmooth_SetFps
            fakeG.HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.6" end
            local st_prev, ver_prev = fakeG.HyperSmooth_GetDllStatus()
            assert(st_prev == "outdated" and ver_prev == "2.9.6", "Older 2.9.6 DLL must report outdated, got " .. tostring(st_prev))
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
            -- Lag Comp ON (locomotor ~= nil) without native DLL -> pure-Lua fallback bypasses FollowCamera!
            mock_player.components.locomotor = {}
            raw_px = 12.20
            cam:Update(1/240)
            assert(math.abs(cam.targetpos.x - 12.20) < 1e-4, "Lag Comp ON must bypass FollowCamera 240Hz smoother in pure-Lua mode!")
            -- With native HyperSmooth_SmoothCameraTarget present, FollowCamera MUST lockstep even when locomotor ~= nil!
            local native_cam_calls = 0
            fakeG.HyperSmooth_SmoothCameraTarget = function(rx, ry, rz, resetFlag)
                native_cam_calls = native_cam_calls + 1
                if resetFlag ~= 0 then
                    return rx, ry, rz, false
                end
                return rx - 0.10, ry, rz, true
            end
            cam:Update(1/240) -- Frame 0: initializes native tracker (need_reset -> fnCamSmooth reset=1,0)
            cam:Update(1/240) -- Frame 1: evaluates native smoother (need_reset=false -> sx = rx - 0.10)
            assert(native_cam_calls >= 2, "FollowCamera must invoke native HyperSmooth_SmoothCameraTarget even with locomotor ~= nil!")
            assert(math.abs(cam.targetpos.x - 12.10) < 1e-4, "Native FollowCamera lockstep failed with locomotor ~= nil, got " .. tostring(cam.targetpos.x))
            fakeG.HyperSmooth_SmoothCameraTarget = nil
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

        -- Round 2 regression (P2 #6): CraftingMenuHUD:Open error-retry!
        -- If ApplyFilters throws on 1st Open(), orig_Open MUST still run AND deferred flags MUST NOT be consumed!
        local open_runs_r2, apply_attempts_r2, apply_ok_r2 = 0, 0, 0
        local cm_r2 = {
            crafting_hud = { IsCraftingOpen = function() return false end },
            UpdateFilterButtons = function() return true end,
            ApplyFilters = function()
                apply_attempts_r2 = apply_attempts_r2 + 1
                if apply_attempts_r2 == 1 then
                    G.error("simulated third-party mod filter error")
                end
                apply_ok_r2 = apply_ok_r2 + 1
            end,
            details_root = { Refresh = function() end },
            Refresh = function() end,
        }
        local hud_r2 = {
            owner = real_owner,
            craftingmenu = cm_r2,
            RebuildRecipes = function() end,
            Open = function(self) open_runs_r2 = open_runs_r2 + 1 return true end,
        }
        class_hooks["widgets/redux/craftingmenu_widget"](cm_r2)
        class_hooks["widgets/redux/craftingmenu_hud"](hud_r2)
        cm_r2:Refresh(true)
        assert(cm_r2._hs_deferred_refresh == true and cm_r2._hs_deferred_rebuild == true, "Deferred flags not set")
        hud_r2:Open() -- 1st Open: ApplyFilters throws, orig_Open still executes, flags MUST remain true!
        assert(open_runs_r2 == 1 and apply_attempts_r2 == 1 and apply_ok_r2 == 0,
            "1st Open failed to preserve orig_Open or attempt ApplyFilters")
        assert(cm_r2._hs_deferred_refresh == true and cm_r2._hs_deferred_rebuild == true,
            "Round 2 P2 #6 REGRESSION: failed ApplyFilters prematurely cleared deferred flags!")
        hud_r2:Open() -- 2nd Open: retries ApplyFilters, succeeds, THEN clears flags!
        assert(open_runs_r2 == 2 and apply_attempts_r2 == 2 and apply_ok_r2 == 1,
            "2nd Open did not retry ApplyFilters after 1st failure!")
        assert(cm_r2._hs_deferred_refresh == false and cm_r2._hs_deferred_rebuild == false,
            "2nd Open did not clear deferred flags after success!")

        print(string.format("[CLIENT_V24_TEST] 10. CraftingMenu Zero-Stall PASS (600/600 vanilla O(slots) + early-exit mod memo exact parity, Has %d->%d, hidden UI deferred 3->0 & Open error-retry verified)",
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

        -- 11b) FollowText 240Hz smooth interpolation + >5u teleport snap + LagComp/No-DLL zero-jitter bypass!
        assert(type(class_hooks["widgets/followtext"]) == "function", "followtext hook missing!")
        fakeG11.HyperSmooth_SetFps = function() end
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
        ft:OnUpdate(1/240) -- right on step edge -> C0 smooth advance (no alpha=0 freeze!)
        local x_edge = last_ft_x
        ft:OnUpdate(1/240) -- mid-step -> MUST smoothly interpolate between 0 and 10!
        ft:OnUpdate(1/240)
        local x_mid = last_ft_x
        assert(x_edge > 0.1 and x_mid > x_edge and x_mid < 9.5, "FollowText did not smoothly interpolate at 240Hz: edge=" .. x_edge .. " mid=" .. x_mid)
        -- Teleport >5u -> MUST hard-snap immediately!
        ent_x = 20.0
        ft:OnUpdate(1/240)
        assert(math.abs(last_ft_x - 200.0) < 0.01, "FollowText did not snap on >5u teleport: got " .. last_ft_x)
        -- Round 2 regression (P2 #7): 115Hz & 120Hz continuous per-frame target motion MUST NOT lag!
        for _, hz in ipairs({ 115.0, 120.0, 240.0 }) do
            local dt_hz = 1.0 / hz
            local ft_hz_x = 0.0
            local target_hz_x = 0.0
            local ft_hz = {
                hashud = true,
                target = {
                    IsValid = function() return true end,
                    AnimState = { GetSymbolPosition = function() return target_hz_x, 0.0, 0.0 end },
                },
                symbol = "head",
                offset = { x = 0, y = 0, z = 0 },
                screen_offset = { x = 0, y = 0 },
                SetPosition = function(_, x, y) ft_hz_x = x end,
                OnUpdate = function(self)
                    local x, y = fakeG11.TheSim:GetScreenPos(target_hz_x, 0, 0)
                    self:SetPosition(x, y)
                end,
            }
            class_hooks["widgets/followtext"](ft_hz)
            ft_hz:OnUpdate(dt_hz) -- init at 0.0
            for step = 1, 30 do
                target_hz_x = step * 0.1
                ft_hz:OnUpdate(dt_hz)
            end
            assert(math.abs(ft_hz_x - 30.0) < 1e-4,
                string.format("Round 2 P2 #7 REGRESSION: FollowText lagged at %.0fHz! got=%.4f expected=30.0", hz, ft_hz_x))
        end
        -- Lag Comp ON / Local Host (locomotor present) -> MUST bypass 30Hz lag-lerp with 0 jitter at 60/115Hz!
        ft.target.components = { locomotor = {} }
        ent_x = 20.15
        ft:OnUpdate(1/60)
        assert(math.abs(last_ft_x - 201.5) < 0.01, "FollowText with locomotor must stay locked to raw pos! got=" .. last_ft_x)
        -- Pure Lua Workshop mod without dinput8.dll -> MUST bypass cleanly!
        ft.target.components = nil
        fakeG11.HyperSmooth_SetFps = nil
        ent_x = 20.45
        ft:OnUpdate(1/60)
        assert(math.abs(last_ft_x - 204.5) < 0.01, "FollowText without native DLL must stay locked to raw pos! got=" .. last_ft_x)

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
        assert(fakeG11.DataDumper({ a = 1 }, "t", nil, nil) == "orig_slow" and orig_dump_calls == 1 and fastdump_calls == 1 and last_orig_fastmode == nil,
               "Local Host FastDataDumper must delegate fastmode=nil to orig_dumper!")
        assert(fakeG11.DataDumper({ a = 1 }, "t", false, nil) == "orig_slow" and orig_dump_calls == 2,
               "Local Host FastDataDumper did not fall back on fastmode=false!")
        assert(fakeG11.DataDumper({ a = 1 }, "t", true, 2) == "orig_fast" and orig_dump_calls == 3,
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

        -- 12c) TriggerDeployHelpers same-tick dedup + move/tick/key/60fps/custom-mod pass-through
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
        pinst.deployhelper_key = "winona_battery_engineering"
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst) -- deployhelper_key change
        assert(deploy_raw_calls == 4, "TriggerDeployHelpers deployhelper_key change must invalidate dedup!")
        mock_time = mock_time + 0.0167 -- 1 frame @ 60Hz (>= 12ms dedup window)
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst)
        assert(deploy_raw_calls == 5, "TriggerDeployHelpers 60fps >=12ms must pass through!")

        assert(type(comp_hooks["deployhelper"]) == "function", "deployhelper hook missing!")
        local stopped_vanilla = false
        local vanilla_dh = {
            delay = 2,
            StopHelper = function(s) s.delay = nil; stopped_vanilla = true end,
            OnWallUpdate = nil,
        }
        comp_hooks["deployhelper"](vanilla_dh)
        vanilla_dh:OnWallUpdate(1/60)
        vanilla_dh:OnWallUpdate(1/60)
        vanilla_dh:OnWallUpdate(1/60)
        assert(not stopped_vanilla and vanilla_dh.delay > 0,
               "Vanilla deployhelper decayed too fast at 60Hz: delay=" .. tostring(vanilla_dh.delay))
        vanilla_dh:OnWallUpdate(1/60)
        assert(stopped_vanilla and vanilla_dh.delay == nil,
               "Vanilla deployhelper failed to stop after 2 sim ticks!")

        local hitch_stopped = false
        local hitch_dh = {
            delay = 2,
            StopHelper = function(s) s.delay = nil; hitch_stopped = true end,
            OnWallUpdate = nil,
        }
        comp_hooks["deployhelper"](hitch_dh)
        hitch_dh:OnWallUpdate(0)
        assert(not hitch_stopped and hitch_dh.delay == 2,
               "Deployhelper OnWallUpdate(0) must not decrement delay: delay=" .. tostring(hitch_dh.delay))
        hitch_dh:OnWallUpdate(0.08) -- 80ms chest pickup hitch must cap step to 1.0, never killing delay=2 in 1 frame
        assert(not hitch_stopped and math.abs(hitch_dh.delay - 1.0) < 1e-6,
               "Deployhelper 80ms frame hitch prematurely killed range circle: delay=" .. tostring(hitch_dh.delay))
        hitch_dh:OnWallUpdate(0.08)
        assert(hitch_stopped and hitch_dh.delay == nil,
               "Deployhelper failed to stop on second hitch frame!")

        local mock_active_item = { prefab = "spoiled_food" }
        fakeG12.ThePlayer = {
            replica = {
                inventory = {
                    GetActiveItem = function() return mock_active_item end,
                },
            },
        }
        mock_tick = mock_tick + 1
        mock_time = mock_time + 0.02
        local calls_before_act = deploy_raw_calls
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst)
        assert(deploy_raw_calls == calls_before_act + 1, "Initial TriggerDeployHelpers with active item failed!")
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst)
        assert(deploy_raw_calls == calls_before_act + 1, "Same-tick same active_item should dedup!")
        mock_active_item = { prefab = "fertilizer" }
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst)
        assert(deploy_raw_calls == calls_before_act + 2, "Changing active_item must immediately invalidate TriggerDeployHelpers dedup!")
        fakeG12.ThePlayer = nil

        local custom_calls = 0
        local custom_orig = function() custom_calls = custom_calls + 1 end
        local custom_dh = { delay = 2, StopHelper = function() end, OnWallUpdate = custom_orig }
        comp_hooks["deployhelper"](custom_dh)
        assert(custom_dh.OnWallUpdate == custom_orig, "Third-party deployhelper:OnWallUpdate was overwritten!")
        custom_dh:OnWallUpdate(1/60)
        assert(custom_calls == 1, "Third-party deployhelper:OnWallUpdate did not run!")
        local calls_before_custom = deploy_raw_calls
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst)
        fakeG12.TriggerDeployHelpers(10.50, 0, 10.50, 64, rec_a, pinst)
        assert(deploy_raw_calls == calls_before_custom + 2,
               "TriggerDeployHelpers must bypass dedup when custom deployhelper mod is detected: raw=" .. deploy_raw_calls)

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
        g13.HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end
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

        -- 13c-guard. Verify debug.getinfo source guard yields to third-party EntityScript hooks (full + partial)
        do
            local function run_es_guard_case(src_map)
                local es_pe = function() return "mod_pe" end
                local es_ht = function() return "mod_ht" end
                local es_hot = function() return "mod_hot" end
                local es_ha = function() return "mod_ha" end
                local es_hy = function() return "mod_hy" end
                local custom_es = {
                    PushEvent_Internal = es_pe,
                    HasTags = es_ht,
                    HasOneOfTags = es_hot,
                    HasAllTags = es_ha,
                    HasAnyTag = es_hy,
                }
                local g_sub = {}
                for k, v in G.pairs(g13) do g_sub[k] = v end
                g_sub._G = g_sub
                g_sub.EntityScript = custom_es
                g_sub.require = function(m)
                    if m == "entityscript" then return custom_es end
                    if m == "json" then return mock_json end
                    return {}
                end
                g_sub.debug = {
                    getinfo = function(f, what)
                        local s = src_map[f]
                        if s ~= nil then return { source = s } end
                        return G.debug.getinfo(f, what)
                    end,
                    sethook = mock_debug.sethook,
                }
                local env_sub = {}
                for k, v in G.pairs(env13) do env_sub[k] = v end
                env_sub.GLOBAL = g_sub
                setmetatable(env_sub, { __index = g_sub })
                local f_sub = assert(G.kleiloadlua(modmain_path))
                setfenv(f_sub, env_sub)
                f_sub()
                return custom_es, es_pe, es_ht, es_hot, es_ha, es_hy
            end

            -- Case A: all 3 wrapped by third-party mod -> all 3 yielded
            local esA, peA, htA, hotA, haA, hyA = run_es_guard_case(setmetatable({}, { __index = function() return "../mods/workshop-999999/modmain.lua" end }))
            assert(esA.PushEvent_Internal == peA and not esA._hs_fast_pushevent, "Third-party PushEvent_Internal was overwritten!")
            assert(esA.HasTags == htA and esA.HasAllTags == haA and not esA._hs_fast_hastags, "Third-party HasTags was overwritten!")
            assert(esA.HasOneOfTags == hotA and esA.HasAnyTag == hyA and not esA._hs_fast_hasoneoftags, "Third-party HasOneOfTags was overwritten!")

            -- Case B: partial wrap (only HasTags wrapped by third-party mod; others vanilla scripts/entityscript.lua)
            local esB_pe = function() return "v_pe" end
            local esB_ht = function() return "mod_ht" end
            local esB_hot = function() return "v_hot" end
            local custom_B = {
                PushEvent_Internal = esB_pe,
                HasTags = esB_ht,
                HasOneOfTags = esB_hot,
                HasAllTags = esB_ht,
                HasAnyTag = esB_hot,
            }
            local g_B = {}
            for k, v in G.pairs(g13) do g_B[k] = v end
            g_B._G = g_B
            g_B.EntityScript = custom_B
            g_B.require = function(m)
                if m == "entityscript" then return custom_B end
                if m == "json" then return mock_json end
                return {}
            end
            g_B.debug = {
                getinfo = function(f, what)
                    if f == esB_pe or f == esB_hot then return { source = "@scripts/entityscript.lua" } end
                    if f == esB_ht then return { source = "../mods/workshop-999999/modmain.lua" } end
                    return G.debug.getinfo(f, what)
                end,
                sethook = mock_debug.sethook,
            }
            local env_B = {}
            for k, v in G.pairs(env13) do env_B[k] = v end
            env_B.GLOBAL = g_B
            setmetatable(env_B, { __index = g_B })
            local f_B = assert(G.kleiloadlua(modmain_path))
            setfenv(f_B, env_B)
            f_B()
            assert(custom_B.PushEvent_Internal ~= esB_pe and custom_B._hs_fast_pushevent == true, "Vanilla PushEvent_Internal should be patched in partial case!")
            assert(custom_B.HasTags == esB_ht and custom_B.HasAllTags == esB_ht and not custom_B._hs_fast_hastags, "Third-party HasTags must stay untouched in partial case!")
            assert(custom_B.HasOneOfTags ~= esB_hot and custom_B.HasAnyTag == custom_B.HasOneOfTags and custom_B._hs_fast_hasoneoftags == true, "Vanilla HasOneOfTags should still be patched in partial case!")
        end

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
        g14.RPC = {
            AttackButton = 4, LeftClick = 1,
            ToggleController = 58, SetWriteableText = 59, OpenGift = 60,
            RecievePopupMessage = 61, ClosePopup = 62, GetChatHistory = 63,
            AddSkillXP = 67, SetSkillActivatedState = 68,
            SetClientAuthoritativeSetting = 69, PostActivateHandshake = 70,
            OnScrapbookDataTaught = 71,
        }
        g14.CLIENT_RPC = {
            ShowPopup = 1, LearnRecipe = 2, RecieveChatHistory = 8,
            SetSkillActivatedState = 12, AddSkillXP = 13,
            PostActivateHandshake = 14, TryToTeachScrapbookData = 15,
        }
        g14.HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end
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
        local exp_s1 = (2 ^ 26) + (2 ^ 31) -- only ToggleController (58) + GetChatHistory (63)
        assert(captured_masks.s1 == exp_s1, "Expected srv1=" .. tostring(exp_s1) .. ", got " .. tostring(captured_masks.s1))
        assert(captured_masks.s2 == 0, "SkillTree/handshake/entity bit leaked into srv2: " .. tostring(captured_masks.s2))
        -- Only strictly entity-free, handshake-independent ClientRPCs move off Ch0 (LearnRecipe=2, RecieveChatHistory=8);
        -- ShowPopup, SetSkillActivatedState, AddSkillXP, PostActivateHandshake, TryToTeachScrapbookData MUST stay on Ch0!
        local exp_c0 = (2 ^ 2) + (2 ^ 8)
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
            HyperSmooth_Version = function() return "2.9.0" end,
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
        -- Instantly switch dominant light to opposite side (-5, 0) -> single rescan frame MUST smoothly damp, NEVER snap 180 deg!
        mock_lights = { fireB }
        local after_1step = ws_calls[#ws_calls]
        for _ = 1, 6 do
            hud_onupdate({}, 0.05)
            local cur = ws_calls[#ws_calls]
            if math.abs(cur[13] - before_flip[13]) > 0.01 then
                after_1step = cur
                break
            end
        end
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

    -- =========================================================================
    -- TEST 19: v2.9.0 World-Space ShadowMask FBO Lua Bridge & Fail-Closed Fallback
    -- =========================================================================
    do
        local sc_calls = {}
        local bloom_rt_calls = {}
        local bloom_params_19 = nil
        local pp_unis_19 = {}
        local ws_ready_flag = true
        local ws_fail_flag = false
        local sh_init_19, sh_sort_19, hud_upd_19 = nil, nil, nil
        local prefab_hooks_19 = {}
        local fire_x = 6.0
        local fire_mob = {
            IsValid = function() return true end,
            Light = { IsEnabled = function() return true end, GetCalculatedRadius = function() return 6.0 end },
            Transform = { GetWorldPosition = function() return fire_x, 0, 2.0 end },
        }
        local g19 = {
            print = function() end,
            pcall = G.pcall, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            type = G.type, tonumber = G.tonumber, tostring = G.tostring,
            math = G.math, string = G.string, table = G.table,
            rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            json = G.json,
            resolvefilepath = function(p) return "resolved/" .. tostring(p) end,
            GetTick = function() return 0 end, GetTimeRealSeconds = function() return 0 end,
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyDownHandler = function() end, OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            KnownModIndex = { GetModInfo = function() return {} end },
            TheFrontEnd = {}, Settings = {}, TUNING = {}, scheduler = {},
            ANCHOR_RIGHT = 2, ANCHOR_TOP = 1, NUMBERFONT = "stint-ucr", CLIENT_MOD_RPC = {},
            LanguageTranslator = { defaultlang = "zh" },
            STRINGS = { UI = { OPTIONS = { ENABLED = "Enabled", DISABLED = "Disabled" } } },
            TheWorld = { HasTag = function() return false end, state = { phase = "day", timeinphase = 0.35, time = 0.25, isfullmoon = false } },
            ThePlayer = { Transform = { GetWorldPosition = function() return 0, 0, 0 end } },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                FindEntities = function() return { fire_mob } end,
            },
            TheCamera = {
                currentpos = { x = 12.5, y = 0, z = -8.25 },
                GetRightVec = function() return { x = 0.7071, y = 0, z = -0.7071 } end,
                GetDownVec  = function() return { x = 0.7071, y = 0, z =  0.7071 } end,
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
            IsPaused = function() return false end,
            SamplerSizes = { Relative = 1 },
            SamplerColourMode = { RGB = 1 },
            SamplerEffectBase = { BloomSampler = 1, Shader = 2 },
            FILTER_MODE = { LINEAR = 1 }, MIP_FILTER_MODE = { NONE = 0 }, WRAP_MODE = { CLAMP_TO_EDGE = 1 },
            PostProcessorEffects = { Bloom = 1, ColourCube = 2 },
            SamplerEffects = {},
            UniformVariables = {},
            PostProcessor = G.setmetatable({}, {
                __index = {
                    SetBloomSamplerParams = function(_, sm, w, h, cm) bloom_params_19 = { sm, w, h, cm } end,
                    AddUniformVariable = function(_, n) return "U_" .. n end,
                    AddSamplerEffect = function(_, p) return "SE_" .. p end,
                    AddPostProcessEffect = function(_, p) return "PPE_" .. p end,
                    AddSampler = function() end, SetEffectUniformVariables = function() end,
                    SetSamplerEffectState = function() end, SetSamplerEffectFilter = function() end,
                    SetPostProcessEffectAfter = function() end, EnablePostProcessEffect = function() end,
                    SetUniformVariable = function(_, uid, a, b, c, d) pp_unis_19[uid] = { a, b, c, d } end,
                    IsBloomEnabled = function() return false end,
                    SetBloomEnabled = function(_, en) bloom_rt_calls[#bloom_rt_calls + 1] = en end,
                },
            }),
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
            HyperSmooth_SetWorldStudio = function() return true end,
            HyperSmooth_SetShadowDyn   = function() return true end,
            HyperSmooth_SetShadowConfig = function(...)
                sc_calls[#sc_calls + 1] = { ... }
                return ws_ready_flag, ws_fail_flag
            end,
        }
        local env19 = {
            GLOBAL = g19, MODROOT = "../mods/client_v24_lua_test/", Asset = function(t, p) return { t, p } end,
            GetModConfigData = function(k) if k == "visual_preset" then return 2 end return nil end,
            AddModShadersInit = function(fn) sh_init_19 = fn end,
            AddModShadersSortAndEnable = function(fn) sh_sort_19 = fn end,
            AddPrefabPostInit = function(name, fn)
                prefab_hooks_19[name] = prefab_hooks_19[name] or {}
                prefab_hooks_19[name][#prefab_hooks_19[name] + 1] = fn
            end,
            AddPrefabPostInitAny = function() end,
            AddClassPostConstruct = function(cls, fn)
                if cls == "screens/playerhud" then
                    local mock_hud = { AddChild = function(_, w) return w end }
                    fn(mock_hud)
                    hud_upd_19 = mock_hud.OnUpdate
                end
            end,
            AddComponentPostInit = function() end, AddSimPostInit = function() end,
            AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
            modimport = function() end,
        }
        local fn19 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setmetatable(env19, { __index = g19 })
        G.setfenv(fn19, env19); fn19()
        assert(sh_init_19 and sh_sort_19 and hud_upd_19, "Test 19 hooks missing")
        sh_init_19(); sh_sort_19()

        -- 1) v2.9.0 WorldShadowEngine active: NEVER forces BloomSampler ON when user Bloom is OFF!
        for i = 1, #bloom_rt_calls do
            assert(bloom_rt_calls[i] == false, "Test 19: v2.9.0 must NEVER force BloomSampler ON when Game Bloom is OFF!")
        end
        assert(bloom_params_19 == nil,
            "Test 19: v2.9.0+ WorldShadowEngine MUST keep Klei's exact stock 0.25x BloomSampler untouched!")
        assert(pp_unis_19["U_HS_SHADOW_CFG"] and pp_unis_19["U_HS_SHADOW_CFG"][4] == 0.0,
            "Test 19: PS_VISUAL_FINAL HS_SHADOW_CFG.w must be 0.0 when v2.9.0 WorldShadowEngine is active!")

        -- 2) Verify HyperSmooth_SetShadowConfig receives exact density/soft/tint/hd/skip/cam!
        hud_upd_19({}, 1.0 / 240.0)
        assert(#sc_calls >= 1, "Test 19: HyperSmooth_SetShadowConfig was not called!")
        local lc = sc_calls[#sc_calls]
        assert(lc[1] > 0.1 and lc[2] >= 0.4 and lc[6] == 1 and lc[7] == 1,
            "Test 19: SetShadowConfig args invalid: " .. tostring(lc[1]) .. "/" .. tostring(lc[2]) .. "/" .. tostring(lc[6]) .. "/" .. tostring(lc[7]))
        assert(math.abs(lc[8] - 12.5) < 1e-3 and math.abs(lc[9] - (-8.25)) < 1e-3,
            "Test 19: Camera target center did not propagate to SetShadowConfig!")
        assert(math.abs((lc[12] or -1) - 0.68) < 1e-3 and math.abs((lc[13] or -1) - 0.24) < 1e-3,
            "Test 19: Phase 1 default entityRecv/ovalKeep did not propagate: " .. tostring(lc[12]) .. "/" .. tostring(lc[13]))
        g19.c_hstune("shadow_entity_recv", 0.0)
        g19.c_hstune("shadow_oval_keep", 0.80)
        hud_upd_19({}, 1.0 / 240.0)
        local lc2 = sc_calls[#sc_calls]
        assert(math.abs((lc2[12] or -1) - 0.0) < 1e-3 and math.abs((lc2[13] or -1) - 0.80) < 1e-3,
            "Test 19: Live tuned entityRecv/ovalKeep did not propagate: " .. tostring(lc2[12]) .. "/" .. tostring(lc2[13]))

        -- 3) Verify shadow creatures are NOT mutated with SetBloomEffectHandle under v2.9.0!
        local ch19 = {
            bloom_handle = nil,
            IsValid = function() return true end,
            AnimState = {
                SetBloomEffectHandle = function(_, h) ch19.bloom_handle = h end,
                ClearBloomEffectHandle = function() ch19.bloom_handle = nil end,
            },
        }
        prefab_hooks_19["crawlinghorror"][1](ch19)
        assert(ch19.bloom_handle == nil, "Test 19: v2.9.0 handles shadow-creature exclusion natively; must NOT mutate SetBloomEffectHandle!")

        -- 4) Verify 240Hz live moving light tracking WITHOUT needing FindEntities rescan!
        g19.TheWorld.state.phase = "night"
        g19.TheWorld.state.timeinphase = 0.4
        hud_upd_19({}, 0.30) -- populates cached light list
        fire_x = -6.0        -- move cached lantern on next single 240Hz frame (dt=1/240 << 0.25s!)
        local calls_before = #sc_calls
        hud_upd_19({}, 1.0 / 240.0)
        assert(#sc_calls == calls_before + 1, "Test 19: Expected 240Hz shadow update on moving cached light!")

        -- 5) Round 2 regression (P2 #4): (ready=false, failed=false) MUST NOT suppress legacy shadows!
        g19.TheWorld.state.phase = "day"
        g19.TheWorld.state.timeinphase = 0.4
        ws_ready_flag = false
        ws_fail_flag = false
        hud_upd_19({}, 0.05)
        assert(pp_unis_19["U_HS_SHADOW_CFG"][4] > 0.1,
            "Test 19 (Round 2 P2 #4): (ready=false, failed=false) MUST fall back to legacy PS_VISUAL_FINAL shadows!")
        assert(bloom_rt_calls[#bloom_rt_calls] == true,
            "Test 19 (Round 2 P2 #4): (ready=false, failed=false) fallback must arm BloomSampler RT!")
        -- 5b) Once native shadow FBO becomes ready=(true, false), cleanly return to 0-Bloom v2 mode!
        ws_ready_flag = true
        ws_fail_flag = false
        hud_upd_19({}, 0.05)
        assert(pp_unis_19["U_HS_SHADOW_CFG"][4] == 0.0,
            "Test 19 (Round 2): Returning to (ready=true, failed=false) must disable legacy HS_SHADOW_CFG!")
        assert(bloom_rt_calls[#bloom_rt_calls] == false,
            "Test 19 (Round 2): Returning to (ready=true, failed=false) must release BloomSampler RT!")

        -- 6) Verify fail-closed fallback when ws_failed == true!
        ws_ready_flag = false
        ws_fail_flag = true
        hud_upd_19({}, 0.05)
        assert(pp_unis_19["U_HS_SHADOW_CFG"][4] > 0.1,
            "Test 19: Fail-closed fallback must re-enable PS_VISUAL_FINAL HS_SHADOW_CFG when ws_failed=true!")

        print("[CLIENT_V24_TEST] 19. v2.9.0 World-Space ShadowMask FBO Lua Bridge + 0-Bloom + (false,false)/(false,true) Fallback PASS")
    end

    -- =========================================================================
    -- TEST 20: v2.9.5 Multi-Light Dynamic Point Shadows Lua Bridge + Temporal Fade +
    --          Zero Double-Shadow Handoff + 100% Legacy v2.9.0 HS1 ShareCode Parity
    -- =========================================================================
    do
        local sh_init_20, sh_sort_20, hud_upd_20 = nil, nil, nil
        local pl_calls = {}
        local ws_calls_20 = {}
        local sc_calls_20 = {}
        local fire1_on = true
        local fire1_x, fire1_z = 5.0, 2.0
        local fire2_x, fire2_z = -6.0, -3.0
        local weak_bug = {
            IsValid = function() return true end,
            Light = { IsEnabled = function() return true end, GetCalculatedRadius = function() return 1.2 end, GetIntensity = function() return 0.9 end },
            Transform = { GetWorldPosition = function() return 1.0, 0, 1.0 end },
        }
        local fire1 = {
            IsValid = function() return true end,
            Light = { IsEnabled = function() return fire1_on end, GetCalculatedRadius = function() return 6.0 end, GetIntensity = function() return 0.90 end },
            Transform = { GetWorldPosition = function() return fire1_x, 0, fire1_z end },
        }
        local fire2 = {
            IsValid = function() return true end,
            Light = { IsEnabled = function() return true end, GetCalculatedRadius = function() return 4.5 end, GetIntensity = function() return 0.70 end },
            Transform = { GetWorldPosition = function() return fire2_x, 0, fire2_z end },
        }
        local g20 = {
            pcall = G.pcall, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            type = G.type, tonumber = G.tonumber, tostring = G.tostring, print = G.print,
            math = G.math, string = G.string, table = G.table,
            rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            json = G.json,
            resolvefilepath = function(p) return "resolved/" .. tostring(p) end,
            GetTick = function() return 0 end, GetTimeRealSeconds = function() return 0 end,
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyDownHandler = function() end, OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            KnownModIndex = { GetModInfo = function() return {} end },
            TheFrontEnd = {}, Settings = {}, TUNING = {}, scheduler = {},
            ANCHOR_RIGHT = 2, ANCHOR_TOP = 1, NUMBERFONT = "stint-ucr", CLIENT_MOD_RPC = {},
            LanguageTranslator = { defaultlang = "zh" },
            STRINGS = { UI = { OPTIONS = { ENABLED = "Enabled", DISABLED = "Disabled" } } },
            TheWorld = { HasTag = function() return false end, state = { phase = "night", timeinphase = 0.50, time = 0.85, isfullmoon = false } },
            ThePlayer = { Transform = { GetWorldPosition = function() return 0, 0, 0 end } },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                FindEntities = function() return { weak_bug, fire1, fire2 } end,
            },
            TheCamera = {
                currentpos = { x = 0.0, y = 0.0, z = 0.0 },
                GetRightVec = function() return { x = 1.0, y = 0, z = 0.0 } end,
                GetDownVec  = function() return { x = 0.0, y = 0, z = 1.0 } end,
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
            IsPaused = function() return false end,
            SamplerSizes = { Relative = 1 },
            SamplerColourMode = { RGB = 1 },
            SamplerEffectBase = { BloomSampler = 1, Shader = 2 },
            FILTER_MODE = { LINEAR = 1 }, MIP_FILTER_MODE = { NONE = 0 }, WRAP_MODE = { CLAMP_TO_EDGE = 1 },
            PostProcessorEffects = { Bloom = 1, ColourCube = 2 },
            SamplerEffects = {},
            UniformVariables = {},
            PostProcessor = G.setmetatable({}, {
                __index = {
                    SetBloomSamplerParams = function() end,
                    AddUniformVariable = function(_, n) return "U_" .. n end,
                    AddSamplerEffect = function(_, p) return "SE_" .. p end,
                    AddPostProcessEffect = function(_, p) return "PPE_" .. p end,
                    AddSampler = function() end, SetEffectUniformVariables = function() end,
                    SetSamplerEffectState = function() end, SetSamplerEffectFilter = function() end,
                    SetPostProcessEffectAfter = function() end, EnablePostProcessEffect = function() end,
                    SetUniformVariable = function() end,
                    IsBloomEnabled = function() return false end,
                    SetBloomEnabled = function() end,
                },
            }),
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
            HyperSmooth_SetWorldStudio = function(...) ws_calls_20[#ws_calls_20 + 1] = { ... }; return true end,
            HyperSmooth_SetShadowDyn   = function() return true end,
            HyperSmooth_SetShadowConfig = function(...) sc_calls_20[#sc_calls_20 + 1] = { ... }; return true, false end,
            HyperSmooth_SetPointLights = function(...) pl_calls[#pl_calls + 1] = { ... }; return select(3, ...) or 0 end,
        }
        local env20 = {
            GLOBAL = g20, MODROOT = "../mods/client_v24_lua_test/", Asset = function(t, p) return { t, p } end,
            GetModConfigData = function(k) if k == "visual_preset" then return 2 end return nil end,
            AddModShadersInit = function(fn) sh_init_20 = fn end,
            AddModShadersSortAndEnable = function(fn) sh_sort_20 = fn end,
            AddPrefabPostInit = function() end,
            AddPrefabPostInitAny = function() end,
            AddClassPostConstruct = function(cls, fn)
                if cls == "screens/playerhud" then
                    local mock_hud = { AddChild = function(_, w) return w end }
                    fn(mock_hud)
                    hud_upd_20 = mock_hud.OnUpdate
                end
            end,
            AddComponentPostInit = function() end, AddSimPostInit = function() end,
            AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
            modimport = function() end,
        }
        local fn20 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setmetatable(env20, { __index = g20 })
        G.setfenv(fn20, env20); fn20()
        sh_init_20(); sh_sort_20()

        -- 1) Night with 2 campfires + 1 weak firefly (rad=1.2 <= 1.8 filtered out!):
        --    dirStrength MUST be 0 (NO parallel double shadow!), count MUST be 2, density > 0!
        hud_upd_20({}, 0.30)
        local p_night = pl_calls[#pl_calls]
        local ws_night = ws_calls_20[#ws_calls_20]
        local sc_night = sc_calls_20[#sc_calls_20]
        assert(p_night and p_night[1] <= 0.001 and p_night[3] == 2,
            "Test 20: Night must send dirStrength=0 & 2 campfires (filtering firefly), got dir=" .. tostring(p_night and p_night[1]) .. " cnt=" .. tostring(p_night and p_night[3]))
        assert(ws_night and ws_night[16] == 0.0,
            "Test 20: Night radial mode must set directional proj_on=0 to prevent double shadows!")
        assert(sc_night and sc_night[1] > 0.15,
            "Test 20: Shadow density must remain active during radial point-light night!")

        -- 2) Temporal smooth fade-out when fire1 extinguishes (NO instant hard pop!)
        fire1_on = false
        hud_upd_20({}, 0.05) -- 50ms after extinguish: fire1 should still be fading smoothly (> 0), not popped to 0!
        local p_fade1 = pl_calls[#pl_calls]
        assert(p_fade1 and p_fade1[3] == 2,
            "Test 20: Extinguished campfire must fade out smoothly over ~0.25s instead of instant pop!")
        for _ = 1, 20 do hud_upd_20({}, 0.05) end -- 1.0s later: fire1 cleanly pruned, only fire2 remains!
        local p_fade2 = pl_calls[#pl_calls]
        assert(p_fade2 and p_fade2[3] == 1,
            "Test 20: After 1.0s fade-out, extinguished fire1 must be pruned leaving count=1, got " .. tostring(p_fade2 and p_fade2[3]))

        -- 3) Dusk cross-fade: BOTH dirStrength (0..1) AND point lights active simultaneously!
        g20.TheWorld.state.phase = "dusk"
        g20.TheWorld.state.timeinphase = 0.50
        for _ = 1, 15 do hud_upd_20({}, 0.08) end
        local p_dusk = pl_calls[#pl_calls]
        assert(p_dusk and p_dusk[1] > 0.15 and p_dusk[1] < 0.85 and p_dusk[3] >= 1,
            "Test 20: Dusk must cross-fade directional (0<dir<1) + point light, got dir=" .. tostring(p_dusk and p_dusk[1]) .. " cnt=" .. tostring(p_dusk and p_dusk[3]))

        -- 4) Toggling shadow_point_lights=0 cleanly falls back to v2.9.0 directional night mode!
        g20.TheWorld.state.phase = "night"
        g20.TheWorld.state.timeinphase = 0.50
        g20.c_hstune("shadow_point_lights", 0)
        for _ = 1, 10 do hud_upd_20({}, 0.08) end
        local p_off = pl_calls[#pl_calls]
        local ws_off = ws_calls_20[#ws_calls_20]
        assert(p_off and p_off[1] == 1.0 and p_off[3] == 0 and ws_off and ws_off[16] == 1.0,
            "Test 20: shadow_point_lights=0 must restore v2.9.0 directional night shadow cleanly!")

        -- 5) Preset 0 (100% Vanilla Pure) MUST clear point lights & dirStrength to 0!
        env20.HS_PanelApi.SetPreset(0)
        local p_van = pl_calls[#pl_calls]
        assert(p_van and p_van[1] == 0.0 and p_van[3] == 0,
            "Test 20: Preset 0 Vanilla Pure must clear point lights to (0, 1.0, 0)!")

        -- 6) 100% Legacy v2.9.0 HS1 ShareCode ID Parity (keys #49 glint_str, #53 exposure, #81 shadow_entity_recv, #82 shadow_oval_keep MUST NOT shift!)
        --    Construct exact v2.9.0 HS1 binary packet: ver=1, base=0, has_name=0, mask=0, count=4,
        --    (id=49 glint_str=1.20 -> q16=52428), (id=53 exposure=0.40 -> q16=49151), (id=81 shadow_entity_recv=0.25 -> q16=16384), (id=82 shadow_oval_keep=0.75 -> q16=49151)
        local function b64u(bytes)
            local enc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
            local out, n, i = {}, #bytes, 1
            while i <= n do
                local b1, b2, b3 = bytes[i], bytes[i+1], bytes[i+2]
                local v1 = math.floor(b1 / 4)
                local v2 = (b1 % 4) * 16 + (b2 and math.floor(b2 / 16) or 0)
                out[#out + 1] = enc:sub(v1 + 1, v1 + 1)
                out[#out + 1] = enc:sub(v2 + 1, v2 + 1)
                if b2 then
                    local v3 = (b2 % 16) * 4 + (b3 and math.floor(b3 / 64) or 0)
                    out[#out + 1] = enc:sub(v3 + 1, v3 + 1)
                    if b3 then out[#out + 1] = enc:sub((b3 % 64) + 1, (b3 % 64) + 1) end
                end
                i = i + 3
            end
            return table.concat(out)
        end
        local function xor16(a, b)
            local r, p = 0, 1
            for _ = 1, 16 do
                if (a % 2) ~= (b % 2) then r = r + p end
                a = math.floor(a / 2); b = math.floor(b / 2); p = p * 2
            end
            return r
        end
        local raw_v290 = { 1, 0, 0, 4,
            49, math.floor(52428 / 256), 52428 % 256,
            53, math.floor(49151 / 256), 49151 % 256,
            81, math.floor(16384 / 256), 16384 % 256,
            82, math.floor(49151 / 256), 49151 % 256,
        }
        local crc = 0xFFFF
        for i = 1, #raw_v290 do
            crc = xor16(crc, raw_v290[i] * 256)
            for _ = 1, 8 do
                if crc >= 0x8000 then crc = xor16((crc * 2) % 65536, 0x1021) else crc = (crc * 2) % 65536 end
            end
        end
        raw_v290[#raw_v290 + 1] = math.floor(crc / 256) % 256
        raw_v290[#raw_v290 + 1] = crc % 256
        local legacy_code = "HS1-" .. b64u(raw_v290)
        local ok_v, err_v, st_v = env20.HS_PanelApi.ValidateShareCode(legacy_code)
        assert(ok_v and st_v, "Test 20: Legacy v2.9.0 HS1 code rejected: " .. tostring(err_v))
        assert(math.abs(st_v.tune.glint_str - 1.20) < 0.02 and
               math.abs(st_v.tune.exposure - 0.40) < 0.02 and
               math.abs(st_v.tune.shadow_entity_recv - 0.25) < 0.02 and
               math.abs(st_v.tune.shadow_oval_keep - 0.75) < 0.02,
            "Test 20: Legacy v2.9.0 HS1 IDs shifted! glint=" .. tostring(st_v.tune.glint_str) .. " exp=" .. tostring(st_v.tune.exposure))

        print("[CLIENT_V24_TEST] 20. v2.9.5 Multi-Light Point Shadows Bridge + Temporal Fade + Zero Double-Shadow + Legacy HS1 Parity PASS")
    end

    -- =========================================================================
    -- TEST 21: v2.9.5 M2 (Crash Detective + Safe ClipCursor) + M3 (Water Refl)
    --          + M4 (Strict Tag-Whitelist Foliage Sway/Wake, 0 Wall Wobble,
    --            Decoupled from Shadows, Instant Preset-0 Zero, HS1 #85..#89)
    -- =========================================================================
    do
        local wr_calls, wf_calls, fa_calls, clip_calls, crash_calls = {}, {}, {}, {}, {}
        local is_paused = false
        local is_console = false
        local screen_name = "HUD"
        local sh_init_21, sh_sort_21, hud_upd_21 = nil, nil, nil
        local function mk_ent(x, z, tag_list)
            local tset = {}
            for i = 1, #tag_list do tset[tag_list[i]] = true end
            return {
                IsValid = function() return true end,
                HasTag = function(_, tag) return tset[tag] == true end,
                Transform = { GetWorldPosition = function() return x, 0, z end },
            }
        end
        local mixed_ents = {
            mk_ent(2.0,  3.0,  { "plant", "grass" }),                       -- MUST ACCEPT cls=1
            mk_ent(-5.0, 4.0,  { "tree", "plant" }),                        -- MUST ACCEPT cls=2
            mk_ent(1.0,  1.0,  { "structure", "wall", "plant" }),           -- MUST REJECT (wall/structure!)
            mk_ent(3.0,  2.0,  { "structure", "chest" }),                   -- MUST REJECT (chest/structure!)
            mk_ent(4.0, -2.0,  { "player", "character" }),                  -- MUST REJECT (player!)
            mk_ent(-3.0, 1.0,  { "character", "locomotor", "pig" }),        -- MUST REJECT (mob!)
            mk_ent(-6.0, 5.0,  { "tree", "stump" }),                        -- MUST REJECT (stump!)
            mk_ent(6.0,  0.0,  { "flower", "NOCLICK" }),                    -- MUST REJECT (NOCLICK!)
            mk_ent(7.0,  1.0,  { "bush", "plant" }),                        -- MUST ACCEPT cls=1
        }
        local orig_tb_called = 0
        local mock_debug = {
            traceback = function(msg)
                orig_tb_called = orig_tb_called + 1
                return "ORIG_TB:" .. tostring(msg) .. "\\n../mods/workshop-123456789/scripts/boom.lua:42"
            end,
        }
        local g21 = {
            pairs = G.pairs, ipairs = G.ipairs, next = G.next, type = G.type,
            tostring = G.tostring, tonumber = G.tonumber, pcall = G.pcall,
            rawget = G.rawget, rawset = G.rawset, select = G.select, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            print = function() end, debug = mock_debug,
            ANCHOR_RIGHT = 2, ANCHOR_TOP = 1, NUMBERFONT = "stint-ucr", CLIENT_MOD_RPC = {},
            LanguageTranslator = { defaultlang = "zh" },
            STRINGS = { UI = { OPTIONS = { ENABLED = "Enabled", DISABLED = "Disabled" } } },
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
            IsPaused = function() return is_paused end,
            KnownModIndex = {
                GetModFancyName = function(_, id)
                    if id == "workshop-123456789" then return "Exploding Beefalo Mod" end
                    return id
                end,
            },
            TheWorld = {
                HasTag = function() return false end,
                state = { phase = "day", timeinphase = 0.50, isfullmoon = false, israining = false, wetness = 0 },
            },
            ThePlayer = {
                HUD = { IsChatInputScreenOpen = function() return false end },
                Transform = { GetWorldPosition = function() return 0.0, 0.0, 0.0 end },
            },
            TheFrontEnd = {
                IsConsoleOpen = function() return is_console end,
                GetActiveScreen = function() return { name = screen_name } end,
            },
            TheCamera = {
                heading = 45, currentpos = { x = 0, y = 0, z = 0 },
                GetRightVec = function() return { x = 0.707, y = 0, z = -0.707 } end,
            },
            TheSim = {
                FindEntities = function() return mixed_ents end,
                GetPersistentString = function(_, _, cb) if cb then cb(false, "") end end,
                SetPersistentString = function() end,
                GetTick = function() return 100 end, GetTickTime = function() return 1/30 end,
            },
            TheInput = { AddKeyUpHandler = function() end, IsKeyDown = function() return false end },
            UniformVariables = {}, PostProcessorEffects = {},
            PostProcessor = G.setmetatable({}, {
                __index = {
                    SetBloomSamplerParams = function() end,
                    AddUniformVariable = function(_, n) return "U_" .. n end,
                    AddSamplerEffect = function(_, p) return "SE_" .. p end,
                    AddPostProcessEffect = function(_, p) return "PPE_" .. p end,
                    AddSampler = function() end, SetEffectUniformVariables = function() end,
                    SetSamplerEffectState = function() end, SetSamplerEffectFilter = function() end,
                    SetPostProcessEffectAfter = function() end, EnablePostProcessEffect = function() end,
                    SetUniformVariable = function() end,
                    IsBloomEnabled = function() return false end, SetBloomEnabled = function() end,
                },
            }),
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
            HyperSmooth_SetWorldStudio = function() return true end,
            HyperSmooth_SetShadowDyn = function() return true end,
            HyperSmooth_SetShadowConfig = function() return true, false end,
            HyperSmooth_SetPointLights = function() return 0 end,
            HyperSmooth_SetWaterRefl = function(...) wr_calls[#wr_calls + 1] = { ... }; return true, 2 end,
            HyperSmooth_SetWindField = function(...) wf_calls[#wf_calls + 1] = { ... }; return true end,
            HyperSmooth_SetFoliageAnchors = function(...) fa_calls[#fa_calls + 1] = { ... }; return select(1, ...) or 0 end,
            HyperSmooth_SetClipCursor = function(...) clip_calls[#clip_calls + 1] = { ... }; return select(1, ...) and select(2, ...) end,
            HyperSmooth_ReportLuaCrash = function(...) crash_calls[#crash_calls + 1] = { ... } end,
            HyperSmooth_GetCrashReportPath = function() return "D:\\dst\\bin64\\hypersmooth_crash_report.txt" end,
        }
        local env21 = {
            -- Strict real DST mods.lua CreateEnvironment whitelist (NO unpack, NO os, NO select, NO __index = GLOBAL!)
            pairs = G.pairs, ipairs = G.ipairs, print = function() end,
            math = G.math, table = G.table, type = G.type, string = G.string, tostring = G.tostring,
            require = g21.require,
            GLOBAL = g21, MODROOT = "../mods/client_v24_lua_test/", Asset = function(t, p) return { t, p } end,
            GetModConfigData = function(k) if k == "visual_preset" then return 2 end return nil end,
            AddModShadersInit = function(fn) sh_init_21 = fn end,
            AddModShadersSortAndEnable = function(fn) sh_sort_21 = fn end,
            AddPrefabPostInit = function() end, AddPrefabPostInitAny = function() end,
            AddClassPostConstruct = function(cls, fn)
                if cls == "screens/playerhud" then
                    local mock_hud = { AddChild = function(_, w) return w end }
                    fn(mock_hud)
                    hud_upd_21 = mock_hud.OnUpdate
                end
            end,
            AddComponentPostInit = function() end, AddSimPostInit = function() end,
            AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
            modimport = function() end,
        }
        local fn21 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setfenv(fn21, env21); fn21()
        sh_init_21(); sh_sort_21()

        -- 1) M3 Water Reflections + M4 Strict Tag-Whitelist Foliage (0 walls/chests/players/mobs/stumps!)
        hud_upd_21({}, 0.30)
        local wr_p2 = wr_calls[#wr_calls]
        assert(wr_p2 and math.abs(wr_p2[1] - 0.48) < 0.01 and math.abs(wr_p2[2] - 0.55) < 0.01,
            "Test 21 M3: Preset 2 must send water_refl=(0.48, 0.55), got " .. tostring(wr_p2 and wr_p2[1]))
        local wf_p2 = wf_calls[#wf_calls]
        assert(wf_p2 and wf_p2[2] > 0.50 and wf_p2[3] > 0.60 and wf_p2[6] == true,
            "Test 21 M4: Preset 2 must send active wind field + player pos!")
        local fa_p2 = fa_calls[#fa_calls]
        assert(fa_p2 and fa_p2[1] == 3,
            "Test 21 M4: Strict whitelist MUST accept ONLY 3 plants/trees (rejecting wall/chest/player/mob/stump/NOCLICK), got " .. tostring(fa_p2 and fa_p2[1]))
        -- Verify classes: grass=1, evergreen=2, bush=1
        assert(fa_p2[4] == 1 and fa_p2[7] == 2 and fa_p2[10] == 1,
            "Test 21 M4: Wrong foliage classes! got " .. tostring(fa_p2[4]) .. "," .. tostring(fa_p2[7]) .. "," .. tostring(fa_p2[10]))

        -- 2) M4 MUST remain active when shadows are OFF (decoupled from shadow sync!)
        g21.c_hstune("shadow_density", 0)
        hud_upd_21({}, 0.05)
        local wf_noshad = wf_calls[#wf_calls]
        assert(wf_noshad and wf_noshad[2] > 0.50,
            "Test 21 M4: Foliage wind sway must stay active even when shadow_density=0!")

        -- 3) M2 ClipCursor: default OFF -> enable -> active in HUD -> auto-unlocks on Pause, Console, PauseScreen!
        local cl_def = clip_calls[#clip_calls]
        assert(cl_def and cl_def[1] == false and cl_def[2] == false, "Test 21 M2: ClipCursor must default OFF!")
        g21.c_hsclip(true)
        hud_upd_21({}, 0.05)
        local cl_on = clip_calls[#clip_calls]
        assert(cl_on and cl_on[1] == true and cl_on[2] == true, "Test 21 M2: ClipCursor must lock in unpaused HUD!")
        is_paused = true
        hud_upd_21({}, 0.05)
        assert(clip_calls[#clip_calls][2] == false, "Test 21 M2: ClipCursor MUST unlock when paused!")
        is_paused = false; is_console = true
        hud_upd_21({}, 0.05)
        assert(clip_calls[#clip_calls][2] == false, "Test 21 M2: ClipCursor MUST unlock when console open!")
        is_console = false; screen_name = "PauseScreen"
        hud_upd_21({}, 0.05)
        assert(clip_calls[#clip_calls][2] == false, "Test 21 M2: ClipCursor MUST unlock on modal PauseScreen!")
        screen_name = "HUD"; g21.c_hsclip(false)

        -- 4) M2 Crash Detective: debug.traceback hook preserves return verbatim & attributes Mod!
        g21.c_hscrashreport()
        local tb_ret = g21.debug.traceback("nil deref in boom")
        assert(orig_tb_called == 1 and tb_ret:find("ORIG_TB:nil deref in boom", 1, true) ~= nil,
            "Test 21 M2: debug.traceback wrapper must preserve original return value verbatim!")
        local cr = crash_calls[#crash_calls]
        assert(cr and cr[1] == "workshop-123456789" and cr[2] == "Exploding Beefalo Mod" and cr[3]:find("nil deref", 1, true) ~= nil,
            "Test 21 M2: Crash Detective failed to attribute Mod fancy name! got " .. tostring(cr and cr[1]) .. "/" .. tostring(cr and cr[2]))

        -- 5) Preset 0 (Vanilla Pure) MUST instantly zero WaterRefl + WindField + FoliageAnchors!
        env21.HS_PanelApi.SetPreset(0)
        assert(wr_calls[#wr_calls][1] == 0.0, "Test 21 Preset 0: water_refl_str must zero immediately!")
        assert(wf_calls[#wf_calls][2] == 0.0 and wf_calls[#wf_calls][3] == 0.0, "Test 21 Preset 0: wind/wake must zero immediately!")
        assert(fa_calls[#fa_calls][1] == 0, "Test 21 Preset 0: foliage anchors must clear to 0!")

        -- 6) v2.9.5 HS1 ShareCode roundtrip with new #85..#89 knobs!
        env21.HS_PanelApi.SetPreset(2)
        g21.c_hstune("water_refl", 0.92)
        g21.c_hstune("wind_wake", 1.15)
        local code21 = env21.HS_PanelApi.PreviewShareCode("M1234")
        local ok21, err21, st21 = env21.HS_PanelApi.ValidateShareCode(code21)
        assert(ok21 and st21 and math.abs(st21.tune.water_refl_str - 0.92) < 0.02 and math.abs(st21.tune.wind_wake_strength - 1.15) < 0.02,
            "Test 21 HS1: v2.9.5 #85..#89 share code roundtrip failed: " .. tostring(err21))

        print("[CLIENT_V24_TEST] 21. v2.9.5 M2 (CrashDetective+ClipCursor) + M3 (WaterRefl) + M4 (Whitelist Foliage) + Preset0 + HS1 #85..#89 PASS")
    end

    -- =========================================================================
    -- 22. Track A (v2.9.5-A) Fluid Skeletal Animation (A1 Crossfade + A2 Spline)
    --     & Real-Time High-DPI Detail (A3-1) + Tab2 Sub2 UI + HS1 #90..#93
    -- =========================================================================
    do
        local af_calls, det_calls = {}, {}
        local g22 = {
            print = G.print, pcall = G.pcall, xpcall = G.xpcall, error = G.error, assert = G.assert,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, tostring = G.tostring,
            tonumber = G.tonumber, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            setmetatable = G.setmetatable, select = G.select, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            TheNet = { GetIsServer = function() return false end, GetIsClient = function() return true end,
                       GetServerGameMode = function() return "survival" end },
            TheWorld = { ismastersim = false, HasTag = function() return false end,
                         state = { isday = true, phase = "day", time = 0.25, timeinphase = 0.5, israining = false, wetness = 0 } },
            TheSim = {
                GetPersistentString = function(_, _, cb) if type(cb) == "function" then cb(false, "") end end,
                SetPersistentString = function(_, _, _, _, cb) if type(cb) == "function" then cb(true) end end,
                GetTick = function() return 100 end,
                GetTickTime = function() return 1/30 end,
            },
            TheInput = {
                AddKeyDownHandler = function() end,
                IsKeyDown = function() return false end,
            },
            HyperSmooth_Version = function() return "2.9.7" end,
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_SetSkeletal = function() return true end,
            HyperSmooth_SetWorldInterp = function() return true end,
            HyperSmooth_SetVfxSmooth = function() return true end,
            HyperSmooth_SetLowLatency = function() return true end,
            HyperSmooth_SetVisual = function() return true end,
            HyperSmooth_SetHd2d = function() return true end,
            HyperSmooth_SetWorldStudio = function() return true end,
            HyperSmooth_SetShadowDyn = function() return true end,
            HyperSmooth_SetShadowConfig = function() return true, false end,
            HyperSmooth_SetPointLights = function() return true end,
            HyperSmooth_SetWaterRefl = function() return true end,
            HyperSmooth_SetWindField = function() return true end,
            HyperSmooth_SetFoliageAnchors = function() return true end,
            HyperSmooth_SetAnimFluid = function(sp, xf)
                af_calls[#af_calls + 1] = { sp, xf }
                return true
            end,
            HyperSmooth_SetDetailParams = function(gd, se, si, ex)
                det_calls[#det_calls + 1] = { gd, se, si, ex }
                return true
            end,
            PostProcessorEffects = {}, UniformVariables = {}, SamplerSizes = { Relative = 1 }, SamplerColourMode = { RGB = 1 },
            require = function(mod)
                if mod == "widgets/widget" or mod == "widgets/image" or mod == "widgets/imagebutton" or mod == "widgets/text" then
                    return function()
                        local w = { inst = { IsValid = function() return true end } }
                        setmetatable(w, { __index = function(self, k)
                            return function(_, child) return child or self end
                        end })
                        return w
                    end
                end
                return nil
            end,
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, SCALEMODE_PROPORTIONAL = 1,
            BODYTEXTFONT = "body", NUMBERFONT = "num",
        }
        local env22
        env22 = {
            GLOBAL = g22,
            modname = "dst_smooth_highfps",
            MODROOT = "../mods/client_v24_lua_test/",
            GetModConfigData = function(k)
                if k == "visual_preset" then return 0 end
                if k == "skeletal_tween" then return true end
                return nil
            end,
            AddSimPostInit = function(fn) fn() end,
            AddClassPostConstruct = function() end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end,
            AddModShadersInit = function() end,
            AddModShadersSortAndEnable = function() end,
            AddPlayerPostInit = function() end,
            modimport = function(p)
                local fp = "../mods/client_v24_lua_test/target_" .. p:gsub("^%./", "")
                if not fp:match("%.lua$") then fp = fp .. ".lua" end
                local fn = assert(G.kleiloadlua(fp))
                setfenv(fn, env22)
                return fn()
            end,
        }
        setmetatable(env22, { __index = g22 })
        local fn22 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn22, env22)
        fn22()

        -- 1) Preset 0 (Vanilla Pure): detail shaders MUST be 0, while smooth skeletal defaults (0.75, 55ms) stay active!
        env22.HS_PanelApi.SetPreset(0)
        assert(#af_calls > 0 and math.abs(af_calls[#af_calls][1] - 0.75) < 0.01 and math.abs(af_calls[#af_calls][2] - 55.0) < 0.5,
            "Test 22 Preset 0: anim_spline/anim_crossfade defaults mismatch!")
        assert(#det_calls > 0 and det_calls[#det_calls][1] == 0.0 and det_calls[#det_calls][2] == 0.0 and det_calls[#det_calls][3] == 0.0,
            "Test 22 Preset 0: detail shaders MUST be 0.0 in Vanilla Pure!")

        -- 2) Preset 1 (EyeHD) & Preset 2 (Cinema): activate High-DPI ground & sprite detail cleanly!
        env22.HS_PanelApi.SetPreset(1)
        local d1 = det_calls[#det_calls]
        assert(d1[1] > 0.50 and d1[2] > 0.50 and d1[3] > 0.60, "Test 22 Preset 1: EyeHD detail params expected >0.5!")
        env22.HS_PanelApi.SetPreset(2)
        local d2 = det_calls[#det_calls]
        assert(d2[1] > 0.70 and d2[3] > 0.80, "Test 22 Preset 2: Cinema detail params expected >0.7!")

        -- 3) StepTune from Preset 0 on sprite_detail MUST switch to Custom Preset 3 and push non-zero shader!
        env22.HS_PanelApi.SetPreset(0)
        env22.HS_PanelApi.StepTune("sprite_detail", 0.50)
        local snap22 = env22.HS_PanelApi.GetSnapshot()
        assert(snap22.preset == 3 and math.abs(det_calls[#det_calls][3] - 0.50) < 0.01,
            "Test 22 StepTune from Preset 0 must promote to Custom Preset 3 and apply sprite_detail!")

        -- 4) StepTune anim_crossfade & anim_spline pushes immediately to C++!
        env22.HS_PanelApi.StepTune("anim_crossfade", 15.0) -- 55 + 15 = 70ms
        env22.HS_PanelApi.StepTune("anim_spline", -0.25)   -- 0.75 - 0.25 = 0.50
        assert(math.abs(af_calls[#af_calls][1] - 0.50) < 0.01 and math.abs(af_calls[#af_calls][2] - 70.0) < 0.5,
            "Test 22 StepTune anim_fluid live sync failed!")

        -- 5) HS1 ShareCode roundtrip with #90..#93 tail keys!
        local code22 = env22.HS_PanelApi.PreviewShareCode("TrackA")
        local ok22, err22, st22 = env22.HS_PanelApi.ValidateShareCode(code22)
        assert(ok22 and st22 and
               math.abs(st22.tune.anim_spline - 0.50) < 0.02 and
               math.abs(st22.tune.anim_crossfade - 70.0) < 0.5 and
               math.abs(st22.tune.sprite_detail - 0.50) < 0.02,
            "Test 22 HS1 ShareCode #90..#93 roundtrip failed: " .. tostring(err22))

        -- 6) hs_panel.lua instantiates Tab 2 Sub-2 cleanly with zero Lua errors!
        g22.ThePlayer = { HUD = g22.require("widgets/widget")() }
        assert(env22.HS_TogglePanel(true) == true, "Test 22 Panel open failed!")

        print("[CLIENT_V24_TEST] 22. v2.9.5-A Track A (AnimFluid + High-DPI Detail + Tab2 Sub2 + HS1 #90..#93) PASS")
    end

    -- 23. Geometric Placement High-FPS Optimizer (Test 23A) + v2.9.5-B Combat Hit-Feel Engine (Test 23B)
    do
        local combat_cfg_calls = {}
        local impact_calls = {}
        local geo_testfn_calls = 0
        local geo_refresh_pt_calls = 0
        local mock_time_23 = 100.0
        local mock_tick_23 = 500
        local cam_applies = {}

        local mock_placer_mod = {
            TestPoint = function(self, inst, pt)
                if inst == self.inst then
                    geo_testfn_calls = geo_testfn_calls + 1
                end
                return true
            end,
            BuildGridPoint = function(self, bgx, bgz, bgpt)
                self.build_grid[bgx] = self.build_grid[bgx] or {}
                self.build_grid[bgx][bgz] = { x = bgx, z = bgz }
                table.insert(self.refresh_queue, { bgx, bgz })
            end,
            RemoveBuildGrid = function(self)
                self.build_grid = nil
                self.refresh_queue = nil
            end,
            RefreshGridPoint = function(self, bgx, bgz)
                geo_refresh_pt_calls = geo_refresh_pt_calls + 1
            end,
            RefreshBuildGrid = function(self, time_remaining)
                while #self.refresh_queue > 0 do
                    local it = table.remove(self.refresh_queue)
                    self:RefreshGridPoint(it[1], it[2])
                end
            end,
            OnUpdate = function(self, dt)
                if self.build_grid ~= nil and #self.refresh_queue == 0 then
                    for bgx, row in pairs(self.build_grid) do
                        for bgz, _ in pairs(row) do
                            table.insert(self.refresh_queue, { bgx, bgz })
                        end
                    end
                end
                self:RefreshBuildGrid(false)
            end,
        }

        local sim_inits_23 = {}
        local class_hooks_23 = {}
        local player_hooks_23 = {}
        local prefab_hooks_23 = {}
        local loaded_23 = {
            ["components/placer"] = mock_placer_mod,
        }
        local g23
        g23 = {
            TheSim = {
                GetPersistentString = function(self, k, cb) if type(cb) == "function" then cb(false, "") end end,
                SetPersistentString = function(self, k, v, e, cb) if type(cb) == "function" then cb(true) end end,
                GetGameID = function() return "DST" end,
                GetTick = function() return mock_tick_23 end,
                FindEntities = function() return {} end,
            },
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyUpHandler = function() end },
            TheFrontEnd = nil,
            GetTimeRealSeconds = function() return mock_time_23 end,
            Vector3 = function(x, y, z) return { x = x or 0, y = y or 0, z = z or 0, Get = function(s) return s.x, s.y, s.z end } end,
            EQUIPSLOTS = { HANDS = "hands" },
            HyperSmooth_SetFps = function() end,
            HyperSmooth_SetVisual = function() end,
            HyperSmooth_SetHd2d = function() end,
            HyperSmooth_SetAnimFluid = function() end,
            HyperSmooth_SetDetailParams = function() end,
            HyperSmooth_SetCombatConfig = function(en, hs, rc, fl, vf, lg)
                combat_cfg_calls[#combat_cfg_calls + 1] = { en, hs, rc, fl, vf, lg }
            end,
            HyperSmooth_TriggerImpact = function(wx, wy, wz, dx, dz, inten, sty, is_atk, spawn_vfx)
                impact_calls[#impact_calls + 1] = { wx, wy, wz, dx, dz, inten, sty, is_atk, spawn_vfx }
                return true
            end,
            require = function(mod)
                if loaded_23[mod] ~= nil then return loaded_23[mod] end
                if mod == "widgets/widget" or mod == "widgets/image" or mod == "widgets/imagebutton" or mod == "widgets/text" then
                    return function()
                        local w = { inst = { IsValid = function() return true end } }
                        setmetatable(w, { __index = function(self, k)
                            return function(_, child) return child or self end
                        end })
                        return w
                    end
                end
                return nil
            end,
            package = { loaded = loaded_23 },
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, SCALEMODE_PROPORTIONAL = 1,
            BODYTEXTFONT = "body", NUMBERFONT = "num",
            os = G.os, string = G.string, math = G.math, table = G.table, pairs = G.pairs, ipairs = G.ipairs,
            type = G.type, pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, tostring = G.tostring,
            tonumber = G.tonumber, setmetatable = G.setmetatable, print = G.print, error = G.error, unpack = G.unpack,
        }
        local env23
        env23 = {
            GLOBAL = g23,
            modname = "dst_smooth_highfps",
            MODROOT = "../mods/client_v24_lua_test/",
            GetModConfigData = function(k)
                if k == "visual_preset" then return 0 end
                if k == "target_fps" then return 240 end
                if k == "skeletal_tween" then return true end
                if k == "reduce_240hz_churn" then return true end
                if k == "smooth_camera" then return true end
                return nil
            end,
            AddSimPostInit = function(fn) sim_inits_23[#sim_inits_23 + 1] = fn end,
            AddClassPostConstruct = function(name, fn) class_hooks_23[name] = fn end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function(p, fn) prefab_hooks_23[p] = fn end,
            AddPrefabPostInitAny = function() end,
            AddModShadersInit = function() end,
            AddModShadersSortAndEnable = function() end,
            AddPlayerPostInit = function(fn) player_hooks_23[#player_hooks_23 + 1] = fn end,
            modimport = function(p)
                local fp = "../mods/client_v24_lua_test/target_" .. p:gsub("^%./", "")
                if not fp:match("%.lua$") then fp = fp .. ".lua" end
                local fn = assert(G.kleiloadlua(fp))
                setfenv(fn, env23)
                return fn()
            end,
        }
        setmetatable(env23, { __index = g23 })
        local fn23 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn23, env23)
        fn23()
        for _, sfn in ipairs(sim_inits_23) do sfn() end

        -- Test 23A: Geometric Placement 240Hz Adaptive Optimizer
        assert(mock_placer_mod._hs_geo_opt == true, "Test 23A: Placer._hs_geo_opt must be installed!")
        local placer_obj = {
            inst = { Transform = { GetWorldPosition = function() return 0, 0, 0 end, GetRotation = function() return 0 end } },
            _rot = 0,
            build_grid = {},
            refresh_queue = {},
        }
        setmetatable(placer_obj, { __index = mock_placer_mod })
        for gx = -10, 10 do
            placer_obj.build_grid[gx] = {}
            for gz = -10, 10 do
                placer_obj:BuildGridPoint(gx, gz, { x = gx * 0.5, z = gz * 0.5 })
            end
        end
        local pt_same = { x = 1.0, z = 2.0 }
        for k = 1, 8 do
            assert(placer_obj:TestPoint(placer_obj.inst, pt_same) == true)
        end
        assert(geo_testfn_calls == 1, "Test 23A: same-tick stationary TestPoint must cache! got=" .. tostring(geo_testfn_calls))
        assert((g23.HS_PLACER_SKIP_COUNT or 0) >= 7, "Test 23A: expected HS_PLACER_SKIP_COUNT >= 7")

        -- Drain initial queue across 240Hz frames, then verify stationary cooldown avoids thrash
        for frame = 1, 40 do
            mock_time_23 = mock_time_23 + (1.0 / 240.0)
            placer_obj:OnUpdate(1.0 / 240.0)
        end
        assert(#placer_obj.refresh_queue == 0, "Test 23A: refresh_queue must never leak sentinel or stay stuck!")
        local rp_before_idle = geo_refresh_pt_calls
        for frame = 1, 10 do -- 10 * 4.17ms = 41.7ms < 120ms cooldown -> 0 redundant grid sweeps!
            mock_time_23 = mock_time_23 + (1.0 / 240.0)
            placer_obj:OnUpdate(1.0 / 240.0)
        end
        assert(geo_refresh_pt_calls == rp_before_idle,
            "Test 23A: stationary 240Hz frames inside cooldown must do 0 grid thrash! delta=" .. tostring(geo_refresh_pt_calls - rp_before_idle))
        -- Cursor move (BuildGridPoint) immediately cancels cooldown & refreshes moved point!
        placer_obj:BuildGridPoint(11, 0, { x = 5.5, z = 0 })
        mock_time_23 = mock_time_23 + (1.0 / 240.0)
        placer_obj:OnUpdate(1.0 / 240.0)
        assert(geo_refresh_pt_calls > rp_before_idle, "Test 23A: cursor move must immediately cancel idle cooldown!")

        -- Test 23B: Track B Combat Hit-Feel + B4 Zero-Drift Camera Punch + HS1 #94..#99 + Tab 2 Sub-3 UI
        env23.HS_PanelApi.SetPreset(0)
        assert(#combat_cfg_calls > 0 and combat_cfg_calls[#combat_cfg_calls][1] == false,
            "Test 23B Preset 0 must disable CombatConfig!")
        env23.HS_PanelApi.SetPreset(1)
        assert(combat_cfg_calls[#combat_cfg_calls][1] == true, "Test 23B Preset 1 must enable CombatConfig!")
        env23.HS_PanelApi.SetPreset(2)
        assert(combat_cfg_calls[#combat_cfg_calls][1] == true, "Test 23B Preset 2 must enable CombatConfig!")

        local mock_player = {
            Transform = {
                GetWorldPosition = function() return 4.0, 0.0, 8.0 end,
                GetRotation = function() return 0.0 end,
            },
            HUD = g23.require("widgets/widget")(),
        }
        g23.ThePlayer = mock_player

        local mock_cam = {
            pangain = 10, headinggain = 10, distancegain = 10, continuousdistancegain = 10,
            paused = false, cutscene = false,
            target = mock_player,
            currentpos = { x = 10.0, y = 0.0, z = 20.0 },
            targetpos = { x = 10.0, y = 0.0, z = 20.0 },
            Update = function(c, dt) c:Apply() end,
            Snap = function(c) c:Apply() end,
            Apply = function(c)
                cam_applies[#cam_applies + 1] = { c.currentpos.x, c.currentpos.y, c.currentpos.z }
            end,
        }
        assert(type(class_hooks_23["cameras/followcamera"]) == "function", "Test 23B: FollowCamera hook missing!")
        class_hooks_23["cameras/followcamera"](mock_cam)

        local n_imp_before = #impact_calls
        assert(env23.HS_PanelApi.PreviewHitFeel() == true, "Test 23B PreviewHitFeel failed!")
        assert(#impact_calls > n_imp_before, "Test 23B PreviewHitFeel did not call HyperSmooth_TriggerImpact!")

        -- Step FollowCamera across impact -> ZERO camera shake (0.00px deflection & 0.00px drift)!
        mock_cam:Update(0.022, false)
        local peak_apply = cam_applies[#cam_applies]
        local peak_dx = math.abs(peak_apply[1] - 10.0)
        assert(peak_dx < 1e-6, "Test 23B: screen shake MUST be 0.00px (replaced by attacker lunge)! dx=" .. tostring(peak_dx))
        assert(combat_cfg_calls[#combat_cfg_calls][6] ~= nil and combat_cfg_calls[#combat_cfg_calls][6] > 0.7,
            "Test 23B: CombatConfig arg 6 (attacker lunge) missing!")
        for step = 1, 40 do
            mock_cam:Update(0.005, false)
        end
        local final_apply = cam_applies[#cam_applies]
        assert(math.abs(final_apply[1] - 10.0) < 1e-6 and math.abs(final_apply[3] - 20.0) < 1e-6,
            "Test 23B B4 camera residual drift detected!")
        assert(math.abs(mock_cam.currentpos.x - 10.0) < 1e-6 and math.abs(mock_cam.currentpos.z - 20.0) < 1e-6,
            "Test 23B B4 mutated cam.currentpos permanently!")

        -- HS1 ShareCode roundtrip with #94..#105 (Hit-Feel Lunge + Native VFX + Volumetric Light/Cloud/Canopy)
        env23.HS_PanelApi.StepTune("hitfeel_campunch", 0.25) -- 0.95 -> 1.20
        env23.HS_PanelApi.StepTune("hitfeel_vfx", 0.15)      -- 1.05 -> 1.20
        env23.HS_PanelApi.StepTune("vfx_fire_embers", 0.20)  -- 1.05 -> 1.25
        env23.HS_PanelApi.StepTune("vfx_footsteps", 0.15)    -- 1.00 -> 1.15
        env23.HS_PanelApi.StepTune("vol_lightrays", 0.20)    -- 1.00 -> 1.20
        env23.HS_PanelApi.StepTune("vol_cloud_shade", 0.25)  -- 1.00 -> 1.25
        env23.HS_PanelApi.StepTune("vol_canopy_shade", 0.20) -- 0.90 -> 1.10
        local code23 = env23.HS_PanelApi.PreviewShareCode("TrackB")
        local ok23, err23, st23 = env23.HS_PanelApi.ValidateShareCode(code23)
        assert(ok23 and st23 and
               math.abs(st23.tune.hitfeel_enabled - 1.0) < 0.01 and
               math.abs(st23.tune.hitfeel_campunch - 1.20) < 0.02 and
               math.abs(st23.tune.hitfeel_vfx - 1.20) < 0.02 and
               math.abs(st23.tune.vfx_fire_embers - 1.25) < 0.02 and
               math.abs(st23.tune.vfx_footsteps - 1.15) < 0.02 and
               math.abs(st23.tune.vol_lightrays - 1.20) < 0.02 and
               math.abs(st23.tune.vol_cloud_shade - 1.25) < 0.02 and
               math.abs(st23.tune.vol_canopy_shade - 1.10) < 0.02,
            "Test 23B HS1 #94..#105 ShareCode roundtrip failed: " .. tostring(err23))

        -- Tab 2 3-pill UI + Sub-3 Hit-Feel renders cleanly with zero Lua errors
        assert(env23.HS_TogglePanel(true) == true, "Test 23B Panel open failed!")

        print("[CLIENT_V24_TEST] 23. Geo High-FPS Fix + v2.9.5-B/C Zero-Shake Hit-Feel (Attacker Lunge + 0.00px Cam + HS1 #94..#102) PASS")
    end

    -- 24. Panel redesign: Simple/Pro modes, plain-language bundles, QQ group button, persisted UI mode, monkey-click
    do
        local perr = {}
        local saved_ui = nil
        local clip = nil
        local mouse_x, mouse_down, fake_t = 0, false, 100
        local function MW()
            local w = { visible = true, children = {}, inst = { IsValid = function() return true end } }
            setmetatable(w, { __index = function(self, k)
                if k == "AddChild" then return function(s, c) s.children[#s.children + 1] = c; c._parent = s; return c end end
                if k == "SetOnDown" then return function(s, fn) s._ondown = fn end end
                if k == "OnControl" then
                    return function(s, control, down)
                        if not s.focus then return false end
                        for _, c in ipairs(s.children or {}) do
                            if c.focus and c:OnControl(control, down) then return true end
                        end
                        return false
                    end
                end
                if k == "GetWorldPosition" then
                    return function(s)
                        local x, y, n = 0, 0, s
                        while n do
                            local np = G.rawget(n, "_pos")
                            if np then x = x + np[1]; y = y + np[2] end
                            n = G.rawget(n, "_parent")
                        end
                        return { x = x, y = y, z = 0 }
                    end
                end
                if k == "SetText" then return function(s, t) s._txt = t end end
                if k == "SetString" then return function(s, t) s._str = t end end
                if k == "SetOnClick" then return function(s, fn) s._onclick = fn end end
                if k == "Show" then return function(s) s.visible = true end end
                if k == "Hide" then return function(s) s.visible = false end end
                if k == "SetPosition" then return function(s, x, y, z) s._pos = { x, y, z } end end
                return function() end
            end })
            return w
        end
        local hud24 = MW()
        local g24 = {
            pcall = function(f, ...)
                local r = { G.pcall(f, ...) }
                if not r[1] then perr[#perr + 1] = tostring(r[2]) end
                return G.unpack(r)
            end,
            rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable, type = G.type,
            pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error,
            BODYTEXTFONT = "bodyfont", NUMBERFONT = "numfont",
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, ANCHOR_TOP = 1, SCALEMODE_PROPORTIONAL = 2,
            STRINGS = { UI = { OPTIONS = { ENABLED = "Enabled", DISABLED = "Disabled" } } },
            ThePlayer = { HUD = hud24 },
            TheSim = {
                GetPersistentString = function(_, name, cb)
                    if name == "hypersmooth_ui.cfg" then cb(true, "pro") else cb(false, "") end
                end,
                SetPersistentString = function(_, name, val)
                    if name == "hypersmooth_ui.cfg" then saved_ui = val end
                end,
            },
            TheInput = {
                OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end,
                GetScreenPosition = function() return { x = mouse_x, y = 0, z = 0 } end,
                IsMouseDown = function() return mouse_down end,
            },
            MOUSEBUTTON_LEFT = 1000,
            GetTimeRealSeconds = function() return fake_t end,
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_SetSkeletal = function() return true end,
            HyperSmooth_SetVfxSmooth = function() return true end,
            HyperSmooth_SetLowLatency = function() return true end,
            HyperSmooth_SetVisual = function() return true end,
            HyperSmooth_SetHd2d = function() return true end,
            HyperSmooth_SetClipboard = function(s) clip = s; return true end,
            HyperSmooth_GetFrameStats = function() return 240.0, 228.0, 4.18, 0.03, 0, 30 end,
            require = function(p)
                if p == "widgets/widget" or p == "widgets/image" or p == "widgets/imagebutton" or p == "widgets/text" then
                    return function() return MW() end
                end
                return {}
            end,
        }
        setmetatable(g24, {
            __newindex = function(_, k, _)
                G.error("assign to undeclared variable '" .. tostring(k) .. "'", 2)
            end,
        })
        local env24
        env24 = {
            GLOBAL = g24,
            GetModConfigData = function(k)
                if k == "target_fps" then return 240 end
                if k == "skeletal_tween" or k == "low_latency_input" then return true end
                return false
            end,
            AddClassPostConstruct = function() end, AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end, AddSimPostInit = function() end,
            AddClientModRPCHandler = function() end,
            modimport = function(name)
                if name == "hs_panel" then
                    local fp, ep = G.kleiloadlua("../mods/client_v24_lua_test/target_hs_panel.lua")
                    assert(fp ~= nil, "hs_panel.lua load error: " .. tostring(ep))
                    setfenv(fp, env24)
                    fp()
                end
            end,
        }
        local f24 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(f24, env24)
        f24()
        local api24 = env24.HS_PanelApi
        assert(api24.GetUiMode() == "pro", "Test 24: persisted UI mode 'pro' not loaded!")

        -- Bundles (plain-language master switches)
        local function snapOf() return api24.GetSnapshot() end
        assert(type(snapOf().bounds) == "table" and snapOf().bounds.sharp.max > 1.0, "Test 24: snapshot.bounds missing")
        assert(type(snapOf().bundles) == "table" and snapOf().ui_mode == "pro", "Test 24: snapshot.bundles/ui_mode missing")
        assert(snapOf().bundles.clarity == false)
        api24.ToggleBundle("clarity")
        local f1 = snapOf().flags
        assert(f1.cas and f1.af16 and f1.mip_sharp and snapOf().bundles.clarity == true, "Test 24: clarity bundle ON failed")
        api24.ToggleBundle("clarity")
        local f2 = snapOf().flags
        assert(not f2.cas and not f2.af16 and not f2.mip_sharp and snapOf().bundles.clarity == false, "Test 24: clarity bundle OFF failed")
        api24.ToggleBundle("glow")
        assert(snapOf().flags.soft_bloom and snapOf().flags.ao and snapOf().bundles.glow == true, "Test 24: glow bundle failed")
        api24.ToggleBundle("world3d")
        assert(snapOf().tune.shadow_density > 0.3 and snapOf().tune.water_refl_str > 0.3 and snapOf().bundles.world3d == true, "Test 24: world3d ON failed")
        api24.ToggleBundle("world3d")
        assert(snapOf().tune.shadow_density < 0.001 and snapOf().bundles.world3d == false, "Test 24: world3d OFF failed")
        api24.ToggleBundle("hitfeel")
        assert(snapOf().bundles.hitfeel == true, "Test 24: hitfeel bundle failed")
        assert(api24.ToggleBundle("nonsense") == false)

        -- Build panel in Pro mode and collect widget tree
        g24.c_hsmenu(true)
        assert(#hud24.children == 1 and hud24.children[1].visible == true, "Test 24: panel failed to build in Pro mode")
        local all = {}
        local function walk(w)
            all[#all + 1] = w
            for _, c in ipairs(w.children or {}) do walk(c) end
        end
        walk(hud24.children[1])
        local qq_btn = nil
        local nclick = 0
        for _, w in ipairs(all) do
            if w._onclick then nclick = nclick + 1 end
            if type(w._txt) == "string" and w._txt:find("978808336", 1, true) and w._onclick then qq_btn = w end
        end
        assert(nclick > 120, "Test 24: expected >120 clickable widgets, got " .. tostring(nclick))
        assert(qq_btn ~= nil, "Test 24: QQ group 978808336 button missing from header!")
        qq_btn._onclick()
        assert(clip == "978808336", "Test 24: QQ click must copy group number, clip=" .. tostring(clip))
        assert(type(qq_btn._txt) == "string" and (qq_btn._txt:find("已复制", 1, true) or qq_btn._txt:find("Copied", 1, true)),
            "Test 24: QQ button must flash copied state")

        -- Persist UI mode through API
        assert(api24.SetUiMode("simple") == true and saved_ui == "simple", "Test 24: SetUiMode must persist")
        assert(api24.SetUiMode("bogus") == false)
        api24.SetUiMode("pro")

        -- Monkey-click every clickable widget (all tabs, accordion headers, steppers, toggles, slots, mood, share, reset)
        local clickables = {}
        for _, w in ipairs(all) do
            if w._onclick and w ~= qq_btn then clickables[#clickables + 1] = w end
        end
        local mark = #perr
        for _, w in ipairs(clickables) do
            local okc, errc = G.pcall(w._onclick)
            if not okc and tostring(errc):find("target_hs_panel", 1, true) then
                perr[#perr + 1] = "CLICK: " .. tostring(errc)
            end
        end
        env24.HS_PanelNotifySync()
        env24.HS_PanelTickTelemetry()
        local bad = {}
        for i = mark + 1, #perr do
            if perr[i]:find("target_hs_panel", 1, true) then bad[#bad + 1] = perr[i] end
        end
        assert(#bad == 0, "Test 24: hs_panel runtime errors: " .. table.concat(bad, " | "))

        -- Slider / button interaction: hold-repeat, drag, magnet-to-default, FX animation
        do
            g24.c_hsmenu(true)
            local UI = g24.HS_PanelDebug
            assert(type(UI) == "table" and type(UI.steppers) == "table" and #UI.steppers > 20, "Test 24: UI.steppers hook missing")
            local root = hud24.children[1]
            assert(type(G.rawget(root, "OnUpdate")) == "function", "Test 24: root.OnUpdate missing")
            local sw = nil
            for _, e in ipairs(UI.steppers) do
                if e.key == "sharp" and G.rawget(e.hit, "_ondown") and G.rawget(e.minus, "_ondown") then sw = e; break end
            end
            assert(sw ~= nil, "Test 24: sharp slider with _ondown handlers not found")
            local bd = snapOf().bounds.sharp
            local span = bd.max - bd.min
            local function cur() return snapOf().tune.sharp end
            local function tick(n, dt) for _ = 1, n do fake_t = fake_t + dt; root.OnUpdate(root, dt) end end

            -- single press of [-]
            api24.SetTune("sharp", bd.min + span * 0.6)
            local v0 = cur()
            sw.minus._ondown()
            local v1 = cur()
            assert(v1 < v0, "Test 24: [-] press must step down: " .. v0 .. " -> " .. v1)
            assert(UI.hold ~= nil, "Test 24: [-] press must arm hold-repeat")
            -- held: repeats after the initial delay
            mouse_down = true
            tick(3, 0.05)
            assert(cur() == v1, "Test 24: hold-repeat fired before the 0.4s delay")
            tick(20, 0.05)
            local v2 = cur()
            assert(v2 < v1, "Test 24: hold-repeat must keep stepping down: " .. v1 .. " -> " .. v2)
            -- release: stops
            mouse_down = false
            tick(1, 0.05)
            assert(UI.hold == nil, "Test 24: hold must clear on mouse release")
            tick(10, 0.05)
            assert(cur() == v2, "Test 24: value must not move after release")
            -- [+] press
            sw.plus._ondown(); mouse_down = false; tick(1, 0.02)
            assert(cur() > v2, "Test 24: [+] press must step up")

            -- drag the knob: mouse at 75% of the track
            local hp = sw.hit:GetWorldPosition()
            local rp = sw.ref:GetWorldPosition()
            local half = rp.x - hp.x
            assert(half > 40 and half < 60, "Test 24: track half-width calibration off: " .. tostring(half))
            mouse_x = hp.x + (0.75 - 0.5) * 2 * half
            mouse_down = true
            sw.hit._ondown()
            assert(UI.drag == sw and not UI.slider_bad, "Test 24: drag must begin")
            local want = bd.min + 0.75 * span
            assert(math.abs(cur() - want) <= span * 0.01 + 1e-6, "Test 24: drag start value " .. tostring(cur()) .. " vs " .. want)
            -- keep dragging beyond the right end -> clamps to max
            mouse_x = hp.x + 3 * half
            tick(4, 0.06)
            assert(math.abs(cur() - bd.max) <= span * 0.006 + 1e-6, "Test 24: drag beyond end must clamp to max, got " .. tostring(cur()))
            -- magnet to default
            local fdef = (bd.default - bd.min) / span
            mouse_x = hp.x + (fdef + 0.008 - 0.5) * 2 * half
            tick(4, 0.06)
            assert(math.abs(cur() - bd.default) <= 1e-6, "Test 24: drag near default must magnet to default, got " .. tostring(cur()) .. " vs " .. bd.default)
            -- release finalizes and clears drag state
            mouse_x = hp.x + (0.25 - 0.5) * 2 * half
            mouse_down = false
            tick(1, 0.06)
            assert(UI.drag == nil, "Test 24: drag must clear on release")
            assert(math.abs(cur() - (bd.min + 0.25 * span)) <= span * 0.01 + 1e-6, "Test 24: release must apply final position, got " .. tostring(cur()))

            -- coordinate sanity guard: absurd mouse position disables drag instead of corrupting values
            local vkeep = cur()
            mouse_x = hp.x + 50 * half
            mouse_down = true
            sw.hit._ondown()
            assert(UI.slider_bad == true and UI.drag == nil and cur() == vkeep, "Test 24: coordinate mismatch guard failed")
            UI.slider_bad = nil
            mouse_down = false

            -- FX animation drains to idle without errors
            for _, w in ipairs(clickables) do
                local fx = G.rawget(w, "_hs_fx")
                if fx then fx.press = 1.0; fx.hover_t = 1.0; UI.fx_active[fx] = true end
            end
            tick(60, 0.016)
            local pending = 0
            for _ in pairs(UI.fx_active) do pending = pending + 1 end
            assert(pending == 0, "Test 24: FX animation did not settle, pending=" .. pending)
            local mk2 = #perr
            tick(5, 0.016)
            assert(#perr == mk2, "Test 24: OnUpdate raised errors")
        end
        g24.c_hsmenu(false)
        assert(hud24.children[1].visible == false)
        print("[CLIENT_V24_TEST] 24. Panel redesign (Simple/Pro + bundles + persisted mode + QQ 978808336 copy + " .. tostring(#clickables) .. "-widget monkey click, 0 errors + slider drag/hold-repeat/magnet/FX) PASS")
    end

    -- 25. v2.9.5-C Native DST VFX Engine (hs_vfx.lua): strict.lua + Fire Embers + Ambient Motes/Spores + Footsteps + Ground-Physics Hit Debris & Weapon-Tip Glint
    do
        local env_cols, env_scales = {}, {}
        local emitted_by_host = {} -- host_idx -> { [em_idx] = count }
        local follow_calls = {}
        local prefab_hooks = {}
        local px25, pz25 = 10.0, 20.0
        local is_cave25 = false
        local wstate25 = { israining = false, isnight = false, isautumn = true, isspring = false, snowlevel = 0.0 }
        local host_seq = 0
        local created_hosts = {}

        local function MakeMockVfxEnt()
            host_seq = host_seq + 1
            local hid = host_seq
            local counts = {}
            emitted_by_host[hid] = counts
            local function bump(idx) counts[idx] = (counts[idx] or 0) + 1 end
            local valid = true
            local ent = { _hid = hid, _pos = { 0, 0, 0 } }
            ent.IsValid = function() return valid end
            ent.Remove = function() valid = false end
            ent.AddTag = function() end
            ent.Transform = {
                SetPosition = function(_, x, y, z) ent._pos = { x, y, z } end,
                GetWorldPosition = function() return ent._pos[1], ent._pos[2], ent._pos[3] end,
            }
            local vfx = {
                InitEmitters = function(_, n) ent._n_emit = n end,
                AddParticle = function(_, idx) bump(idx) end,
                AddParticleUV = function(_, idx) bump(idx) end,
                AddRotatingParticle = function(_, idx) bump(idx) end,
                AddRotatingParticleUV = function(_, idx) bump(idx) end,
            }
            setmetatable(vfx, { __index = function() return function() end end })
            ent.entity = {
                SetCanSleep = function() end,
                AddTransform = function() return ent.Transform end,
                AddVFXEffect = function() ent.VFXEffect = vfx; return vfx end,
                CallPrefabConstructionComplete = function()
                    assert(ent._n_emit ~= nil and ent._n_emit > 0, "Test 25: CallPrefabConstructionComplete called before InitEmitters!")
                    ent._constructed = true
                end,
                AddFollower = function()
                    ent.Follower = {
                        FollowSymbol = function(_, guid, sym, x, y, z)
                            follow_calls[#follow_calls + 1] = { guid = guid, sym = sym, y = y }
                        end,
                    }
                    return ent.Follower
                end,
                SetParent = function() end,
            }
            created_hosts[hid] = ent
            return ent
        end

        local player25 = {
            GUID = 7777,
            IsValid = function() return true end,
            entity = { IsVisible = function() return true end },
            Transform = { GetWorldPosition = function() return px25, 0.0, pz25 end },
            DoTaskInTime = function(_, _, fn) end,
        }
        local g25 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error,
            TheNet = { IsDedicated = function() return false end },
            TheSim = { GetTickTime = function() return 1.0 / 30.0 end },
            ThePlayer = player25,
            TheWorld = {
                state = wstate25,
                HasTag = function(_, tag) return tag == "cave" and is_cave25 end,
                Map = {
                    IsVisualGroundAtPoint = function() return true end,
                    GetTileAtPoint = function() return 6 end, -- GRASS
                },
            },
            WORLD_TILES = { GRASS = 6, FOREST = 7, DECIDUOUS = 30 },
            BLENDMODE = { Additive = 1, Premultiplied = 2 },
            CreateEntity = MakeMockVfxEnt,
            resolvefilepath = function(p) return p end,
            EnvelopeManager = {
                AddColourEnvelope = function(_, name) env_cols[name] = true end,
                AddVector2Envelope = function(_, name) env_scales[name] = true end,
            },
            EmitterManager = { AddEmitter = function() end },
        }
        setmetatable(g25, {
            __newindex = function(_, k, _)
                G.error("strict.lua violation in hs_vfx: assign to undeclared '" .. tostring(k) .. "'", 2)
            end,
        })
        local env25 = {
            GLOBAL = g25,
            AddPrefabPostInit = function(pref, fn) prefab_hooks[pref] = fn end,
            AddPlayerPostInit = function() end,
        }
        local fv, ev = G.kleiloadlua("../mods/client_v24_lua_test/target_hs_vfx.lua")
        assert(fv ~= nil, "Test 25: hs_vfx.lua load error: " .. tostring(ev))
        setfenv(fv, env25)
        fv()
        local V = env25.HS_VFX
        assert(type(V) == "table", "Test 25: HS_VFX not exported")
        assert(prefab_hooks["campfirefire"] ~= nil and prefab_hooks["coldfirefire"] ~= nil, "Test 25: fire prefab hooks missing")

        -- 1) Vanilla Pure (all 0.0): zero emissions
        V.ApplyConfig(0.0, 0.0, 0.0, false, 0.0)
        for _ = 1, 15 do V.StepTick(0.033) end
        assert(V.GetStats().atmo_emitted == 0 and V.GetStats().foot_emitted == 0 and V.GetStats().combat_emitted == 0,
            "Test 25: Vanilla Pure must emit 0 particles!")

        -- 2) Register warm campfire + cold firepit -> emit warm (em 0) & cold (em 1) embers
        local function MockFire(x, z)
            local f = {
                IsValid = function() return true end,
                Transform = { GetWorldPosition = function() return x, 0.0, z end },
                ListenForEvent = function() end,
            }
            return f
        end
        prefab_hooks["campfirefire"](MockFire(13.0, 20.0))
        prefab_hooks["coldfirefire"](MockFire(8.0, 22.0))
        V.ApplyConfig(1.0, 1.0, 1.0, true, 1.0)
        assert(env_cols["hs_vfx_ember_warm_c"] and env_cols["hs_vfx_hit_wood_c"] and env_cols["hs_vfx_wpn_arcane_c"],
            "Test 25: VFX envelopes not registered!")

        -- 3) Surface Autumn Day standing still -> embers + sun motes + leaves, 0 footsteps!
        for _ = 1, 30 do V.StepTick(0.033) end
        local atmo_c = emitted_by_host[1]
        assert((atmo_c[0] or 0) > 0 and (atmo_c[1] or 0) > 0, "Test 25: warm & cold fire embers missing!")
        assert((atmo_c[2] or 0) > 0 and (atmo_c[4] or 0) > 0, "Test 25: surface sun motes or autumn leaves missing!")
        assert(V.GetStats().foot_emitted == 0, "Test 25: standing still must emit 0 footsteps!")

        -- 4) Switch to Cave -> bioluminescent cave spores (em 3)
        is_cave25 = true
        for _ = 1, 20 do V.StepTick(0.033) end
        assert((atmo_c[3] or 0) > 0, "Test 25: cave spores missing!")
        is_cave25 = false

        -- 5) Turf-aware footsteps: grass dust + leaf, then winter snow, then rain splash
        local foot_c = emitted_by_host[2]
        for step = 1, 20 do px25 = px25 + 0.25; V.StepTick(0.033) end
        assert((foot_c[0] or 0) > 0 and (foot_c[3] or 0) > 0, "Test 25: grass footstep dust/leaf missing!")
        wstate25.snowlevel = 0.6
        for step = 1, 15 do px25 = px25 + 0.25; V.StepTick(0.033) end
        assert((foot_c[1] or 0) > 0, "Test 25: winter snow footstep kick-up missing!")
        wstate25.snowlevel = 0.0; wstate25.israining = true
        for step = 1, 15 do px25 = px25 + 0.25; V.StepTick(0.033) end
        assert((foot_c[2] or 0) > 0, "Test 25: rain splash footsteps missing!")

        -- 6) Ground-physics combat/work impacts + FollowSymbol("swap_object") weapon-tip glint!
        local cbt_c = emitted_by_host[3]
        assert(V.OnImpact(px25 + 1.5, 0, pz25, px25, 0, pz25, 1.0, 5, true) == true)  -- Chop -> wood chips (0)
        assert(V.OnImpact(px25 + 1.5, 0, pz25, px25, 0, pz25, 1.0, 6, true) == true)  -- Mine -> rocks (1) + sparks (2)
        assert(V.OnImpact(px25 + 1.5, 0, pz25, px25, 0, pz25, 1.2, 2, false) == true) -- Shadow combat -> arcane sparks (3) + glints (4) + weapon tip!
        assert((cbt_c[0] or 0) > 0 and (cbt_c[1] or 0) > 0 and (cbt_c[2] or 0) > 0
            and (cbt_c[3] or 0) > 0 and (cbt_c[4] or 0) > 0,
            "Test 25: missing ground-physics wood/rock/spark/glint combat emitters!")
        assert(#follow_calls >= 1 and follow_calls[1].sym == "swap_object",
            "Test 25: weapon-tip FollowSymbol('swap_object') not attached!")
        for _ = 1, 4 do V.StepTick(0.033) end
        assert(V.GetStats().weapon_emitted > 0, "Test 25: weapon-tip swing glints not emitted!")
        for hid = 1, host_seq do
            assert(created_hosts[hid]._constructed == true,
                "Test 25: VFX host #" .. tostring(hid) .. " missed CallPrefabConstructionComplete() (would crash CollectNodes +0x388a2!)")
        end

        -- 7) Clean shutdown
        V.Shutdown()
        assert(created_hosts[1]:IsValid() == false, "Test 25: Shutdown did not clean up local VFX hosts!")
        print("[CLIENT_V24_TEST] 25. v2.9.5-C Native DST VFX Engine (Embers + Motes/Spores + Turf Footsteps + Ground-Physics Debris + swap_object Glint + strict.lua) PASS")
    end

    -- 26. v2.9.5-D Phase 1 World-Space Volumetric Lighting (hs_vollight.lua): 3D Forest Light Rays + ShadeRenderer Cloud/Canopy Shades
    do
        local px26, pz26 = 0.0, 0.0
        local is_cave26 = false
        local cur_tile26 = 7 -- FOREST
        local wstate26 = { isdusk = false, isnight = false, isfullmoon = false, israining = false }
        local shade_types = {}
        local shade_next_id = 0
        local shade_next_h = 0
        local shade_live = {} -- [type] -> { [handle] = true }
        local shade_str = {}
        local shade_en = {}
        local shade_tex = {}
        local shade_global_en = nil
        local ray_ents = {}

        local function MakeEnt26()
            local valid = true
            local e = { _pos = { 0, 0, 0 }, _vis = true, _lo = 0, _rgba = { 1, 1, 1, 1 } }
            e.IsValid = function() return valid end
            e.Remove = function() valid = false end
            e.AddTag = function() end
            e.Show = function() e._vis = true end
            e.Hide = function() e._vis = false end
            e.DoPeriodicTask = function() end
            e.Transform = {
                SetPosition = function(_, x, y, z) e._pos = { x, y, z } end,
                GetWorldPosition = function() return e._pos[1], e._pos[2], e._pos[3] end,
                SetEightFaced = function() end,
                SetRotation = function() end,
            }
            e.AnimState = {
                SetBank = function(_, b) e._bank = b end,
                SetBuild = function(_, b) e._build = b end,
                PlayAnimation = function() end,
                SetLightOverride = function(_, v) e._lo = v end,
                SetFinalOffset = function() end,
                SetScale = function() end,
                Hide = function() end,
                Show = function() end,
                OverrideMultColour = function(_, r, g, b, a) e._rgba = { r, g, b, a } end,
            }
            e.entity = {
                SetCanSleep = function() end,
                AddTransform = function() return e.Transform end,
                AddAnimState = function() ray_ents[#ray_ents + 1] = e; return e.AnimState end,
                CallPrefabConstructionComplete = function() e._constructed = true end,
            }
            return e
        end

        local sr26 = {
            Enable = function(_, on) shade_global_en = on end,
            CreateShadeType = function()
                shade_next_id = shade_next_id + 1
                shade_types[shade_next_id] = true
                shade_live[shade_next_id] = {}
                return shade_next_id
            end,
            SetShadeTexture = function(_, id, tex) shade_tex[id] = tex end,
            SetShadeStrength = function(_, id, s) shade_str[id] = s end,
            SetShadeEnabled = function(_, id, en) shade_en[id] = en end,
            SetShadeMaxRotation = function() end, SetShadeRotationSpeed = function() end,
            SetShadeMaxTranslation = function() end, SetShadeTranslationSpeed = function() end,
            SetShadeMaxScale = function() end, SetShadeScaleSpeed = function() end,
            SpawnShade = function(_, id, x, z, rot, sc)
                shade_next_h = shade_next_h + 1
                shade_live[id][shade_next_h] = { x = x, z = z, sc = sc }
                return shade_next_h
            end,
            RemoveShade = function(_, id, h)
                if shade_live[id] then shade_live[id][h] = nil end
            end,
        }
        local function CountKeys(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end

        local player26 = {
            IsValid = function() return true end,
            Transform = { GetWorldPosition = function() return px26, 0.0, pz26 end },
            DoTaskInTime = function() end,
        }
        local g26 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error,
            FRAMES = 1.0 / 30.0,
            TheNet = { IsDedicated = function() return false end },
            ThePlayer = player26,
            TheCamera = { currentpos = { x = 0, y = 0, z = 0 }, GetDownVec = function() return { x = 0, y = -1, z = 0 } end },
            TheWorld = {
                state = wstate26,
                HasTag = function(_, tag) return tag == "cave" and is_cave26 end,
                Map = { GetTileAtPoint = function() return cur_tile26 end },
            },
            WORLD_TILES = { FOREST = 7, DECIDUOUS = 30, SINKHOLE = 43, FUNGUS = 14, GRASS = 6 },
            ShadeRenderer = sr26,
            CreateEntity = MakeEnt26,
            resolvefilepath = function(p) return p end,
        }
        setmetatable(g26, {
            __newindex = function(_, k, _)
                G.error("strict.lua violation in hs_vollight: assign to undeclared '" .. tostring(k) .. "'", 2)
            end,
        })
        local env26 = { GLOBAL = g26, AddPlayerPostInit = function() end }
        local fl, el = G.kleiloadlua("../mods/client_v24_lua_test/target_hs_vollight.lua")
        assert(fl ~= nil, "Test 26: hs_vollight.lua load error: " .. tostring(el))
        setfenv(fl, env26)
        fl()
        local VL = env26.HS_VOLLIGHT
        assert(type(VL) == "table", "Test 26: HS_VOLLIGHT missing")

        -- 1) Vanilla Pure (0,0,0): 0 rays, 0 shades, ShadeRenderer untouched
        VL.ApplyConfig(0.0, 0.0, 0.0)
        for _ = 1, 5 do VL.StepUpdate(0.1) end
        assert(VL.GetStats().active_rays == 0 and VL.GetStats().active_clouds == 0 and VL.GetStats().active_canopy == 0,
            "Test 26: Vanilla Pure must spawn 0 volumetric light/shades!")
        assert(shade_global_en == nil, "Test 26: Vanilla Pure must not touch ShadeRenderer:Enable!")

        -- 2) Daytime Forest -> spawns 3D lightrays (SetLightOverride=1!) + cloud shades + forest canopy shades
        VL.ApplyConfig(1.0, 1.0, 1.0)
        for _ = 1, 15 do VL.StepUpdate(0.1) end
        local st = VL.GetStats()
        assert(st.active_rays > 0 and st.active_clouds > 0 and st.active_canopy > 0,
            string.format("Test 26: expected rays/clouds/canopy > 0, got %d/%d/%d", st.active_rays, st.active_clouds, st.active_canopy))
        assert(shade_tex[1] == "images/hs_cloud_shade.tex" and shade_tex[2] == "images/tree.tex",
            "Test 26: ShadeRenderer textures mismatch!")
        assert(shade_en[1] == true and shade_en[2] == true and st.cloud_str > 0.15 and st.canopy_str > 0.15,
            "Test 26: Daytime ShadeRenderer strengths did not ramp up!")
        assert(ray_ents[1]._bank == "lightrays" and ray_ents[1]._lo == 1 and ray_ents[1]._rgba[4] > 0.10 and ray_ents[1]._constructed == true,
            "Test 26: Light ray must set bank=lightrays, LightOverride=1, visible alpha, and CallPrefabConstructionComplete()!")

        -- 3) Night (not full moon) -> rays fade to 0 & hide, clouds & canopy disable
        wstate26.isnight = true; wstate26.isfullmoon = false
        for _ = 1, 30 do VL.StepUpdate(0.15) end
        assert(VL.GetStats().ray_alpha < 0.01 and shade_en[1] == false and shade_en[2] == false,
            "Test 26: Night must fade rays to 0 and disable cloud/canopy shades!")

        -- 4) Full Moon -> silver-blue moonbeam rays & gentle cloud drift return!
        wstate26.isfullmoon = true
        for _ = 1, 25 do VL.StepUpdate(0.15) end
        assert(VL.GetStats().ray_alpha > 0.25 and ray_ents[1]._rgba[3] > ray_ents[1]._rgba[1],
            "Test 26: Full moon must activate cool blue-silver moonbeams!")

        -- 5) Cave Sinkhole -> cave shafts stay active, surface clouds & canopy clean up to 0!
        wstate26.isnight = false; wstate26.isfullmoon = false
        is_cave26 = true; cur_tile26 = 43 -- SINKHOLE
        px26 = 200.0; pz26 = 200.0
        for _ = 1, 10 do VL.StepUpdate(0.2) end
        assert(VL.GetStats().active_clouds == 0 and VL.GetStats().active_canopy == 0
            and CountKeys(shade_live[1]) == 0 and CountKeys(shade_live[2]) == 0,
            "Test 26: Cave must evict all surface cloud & canopy shade handles!")
        assert(VL.GetStats().active_rays > 0, "Test 26: Cave sinkhole/fungus must spawn cave light shafts!")

        -- 6) Clean Shutdown -> 0 leaked handles & entities, AND disarms ShadeRenderer texHandle to ""!
        VL.Shutdown()
        assert(VL.GetStats().active_rays == 0 and CountKeys(shade_live[1]) == 0 and CountKeys(shade_live[2]) == 0,
            "Test 26: Shutdown leaked ray entities or ShadeRenderer handles!")
        assert(shade_tex[1] == "" and shade_tex[2] == "",
            "Test 26: Shutdown must clear ShadeRenderer textures to empty string (texHandle=-1)!")
        assert(shade_str[1] == 0.0 and shade_str[2] == 0.0,
            "Test 26: Shutdown must zero ShadeRenderer strengths!")
        local p_ids = VL.GetPersistentShadeIds()
        assert(type(p_ids) == "table" and p_ids.cloud == 1 and p_ids.canopy == 2,
            "Test 26: GetPersistentShadeIds must return persistent ShadeType IDs!")
        print("[CLIENT_V24_TEST] 26. v2.9.5-D Phase 1 Volumetric Lighting (3D Forest/Cave LightRays + ShadeRenderer Cloud/Canopy + strict.lua) PASS")
    end

    -- 26B. ShadeRenderer Crash-Fix Lifecycle Guard (Main Menu Skip + Persistent ID Reuse + Unconditional SimReset Teardown)
    do
        local create_calls = 0
        local tex_calls = {}
        local str_calls = {}
        local sr26b = {
            Enable = function() end,
            CreateShadeType = function() create_calls = create_calls + 1; return 10 + create_calls end,
            SetShadeTexture = function(_, id, tex) tex_calls[#tex_calls + 1] = { id = id, tex = tex } end,
            SetShadeStrength = function(_, id, s) str_calls[id] = s end,
            SetShadeEnabled = function() end,
            SetShadeMaxRotation = function() end, SetShadeRotationSpeed = function() end,
            SetShadeMaxTranslation = function() end, SetShadeTranslationSpeed = function() end,
            SetShadeMaxScale = function() end, SetShadeScaleSpeed = function() end,
            SpawnShade = function() return 1 end,
            RemoveShade = function() end,
        }
        -- (a) Main Menu (TheWorld == nil, ThePlayer == nil): ApplyConfig must NOT allocate ShadeTypes or bind textures!
        local g_menu = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error,
            TheNet = { IsDedicated = function() return false end },
            ShadeRenderer = sr26b,
            resolvefilepath = function(p) return p end,
        }
        local env_menu = { GLOBAL = g_menu, AddPlayerPostInit = function() end }
        local fl_m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_hs_vollight.lua"))
        setfenv(fl_m, env_menu)
        fl_m()
        env_menu.HS_VOLLIGHT.ApplyConfig(1.0, 1.0, 1.0)
        assert(create_calls == 0 and #tex_calls == 0,
            "Test 26B: ApplyConfig in Main Menu (TheWorld == nil) must NOT call CreateShadeType or SetShadeTexture!")

        -- (b) Re-entering world with persisted Settings._hakimi_shade_types reuses IDs with 0 CreateShadeType calls,
        --     and TheWorld onremove triggers Shutdown -> SetShadeTexture(id, '')!
        local onremove_cb = nil
        local s_reuse = { _hakimi_shade_types = { cloud = 7, canopy = 8 } }
        local g_world = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error,
            Settings = s_reuse,
            TheNet = { IsDedicated = function() return false end },
            TheWorld = {
                ListenForEvent = function(_, ev, cb) if ev == "onremove" then onremove_cb = cb end end,
            },
            ThePlayer = { IsValid = function() return true end },
            ShadeRenderer = sr26b,
            CreateEntity = function()
                local e = { _ok = true }
                e.IsValid = function() return e._ok end
                e.Remove = function() e._ok = false end
                e.AddTag = function() end
                e.DoPeriodicTask = function() end
                e.entity = { SetCanSleep = function() end, CallPrefabConstructionComplete = function() end }
                return e
            end,
            resolvefilepath = function(p) return p end,
        }
        local env_w = { GLOBAL = g_world, AddPlayerPostInit = function() end }
        local fl_w = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_hs_vollight.lua"))
        setfenv(fl_w, env_w)
        fl_w()
        env_w.HS_VOLLIGHT.ApplyConfig(1.0, 1.0, 1.0)
        assert(create_calls == 0, "Test 26B: Persisted ShadeType IDs must prevent CreateShadeType calls on world re-entry!")
        assert(onremove_cb ~= nil, "Test 26B: Missing TheWorld onremove listener!")
        assert(#tex_calls >= 2 and tex_calls[1].id == 7 and tex_calls[1].tex == "images/hs_cloud_shade.tex",
            "Test 26B: Expected reused cloud ShadeType id=7 to arm texture!")
        onremove_cb()
        assert(tex_calls[#tex_calls].id == 8 and tex_calls[#tex_calls - 1].id == 7
            and tex_calls[#tex_calls].tex == "" and tex_calls[#tex_calls - 1].tex == ""
            and str_calls[7] == 0.0 and str_calls[8] == 0.0,
            "Test 26B: TheWorld onremove must disarm both ShadeType textures to '' and zero strengths!")

        -- (c) modmain.lua unconditional SimReset teardown runs even when keep_shard_assets = false!
        local carried_26b = nil
        local fakeG_26b = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print,
            Settings = {},
            TheNet = { GetIsServer = function() return false end, IsDedicated = function() return false end },
            TheSim = { GetTick = function() return 1 end, GetStaticTick = function() return 1 end, GetTickTime = function() return 1/30 end, GetPersistentString = function(_, _, cb) cb(false, "") end, SetPersistentString = function() end, GetPosition = function() return 0, 0 end },
            TheInput = { OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end, AddMouseButtonHandler = function() end },
            TheFrontEnd = { GetActiveScreen = function() return { name = "HUD" } end },
            SimReset = function(ip) carried_26b = ip end,
            require = function() return {} end,
        }
        local env_m26b = {
            GLOBAL = fakeG_26b,
            MODROOT = "../mods/dst_smooth_highfps/",
            Asset = function(t, f) return { type = t, file = f } end,
            GetModConfigData = function(k) return k == "target_fps" and 240 or false end,
            AddClassPostConstruct = function() end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function() end,
            AddModShadersInit = function() end,
            AddModShadersSortAndEnable = function() end,
            AddClientModRPCHandler = function() end,
        }
        local fn_m26b = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fn_m26b, env_m26b)
        fn_m26b()
        local vl_shut, vfx_shut = 0, 0
        env_m26b.HS_VOLLIGHT = {
            GetPersistentShadeIds = function() return { cloud = 3, canopy = 4 } end,
            Shutdown = function() vl_shut = vl_shut + 1 end,
        }
        env_m26b.HS_VFX = {
            Shutdown = function() vfx_shut = vfx_shut + 1 end,
        }
        fakeG_26b.SimReset(nil)
        assert(vl_shut == 1 and vfx_shut == 1,
            "Test 26B: SimReset must unconditionally shut down HS_VOLLIGHT & HS_VFX even with keep_shard_assets=false!")
        assert(carried_26b ~= nil and carried_26b._hakimi_shade_types.cloud == 3 and carried_26b._hakimi_shade_types.canopy == 4,
            "Test 26B: SimReset must forward _hakimi_shade_types into instanceparameters!")
        print("[CLIENT_V24_TEST] 26B. ShadeRenderer Crash-Fix Lifecycle Guard (MainMenu Skip + ID Reuse + onremove/SimReset Teardown) PASS")
    end

    -- 27. v2.9.5-E Shadow Pipeline Parity (#106..#110 + soft=0.00 + Weather/Season + Moon Blue + Dusk Fire Gate + MinHeight + Flash Lights + HS1 Roundtrip)
    do
        local sc27, pl27 = {}, {}
        local sh_init_27, sh_sort_27, hud_upd_27 = nil, nil, nil
        local prefab_hooks_27 = {}
        local wstate27 = { phase = "day", timeinphase = 0.5, isfullmoon = false, precipitation = "none", season = "autumn" }
        local fire_ent27 = {
            IsValid = function() return true end,
            Transform = { GetWorldPosition = function() return 4.0, 0.0, 0.0 end },
            Light = {
                IsEnabled = function() return true end,
                GetCalculatedRadius = function() return 6.0 end,
                GetRadius = function() return 6.0 end,
                GetIntensity = function() return 0.90 end,
            },
        }
        local g27 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            setmetatable = G.setmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error, select = G.select,
            TheNet = { IsDedicated = function() return false end, GetClientTable = function() return {} end },
            TheSim = {
                GetPersistentString = function(_, _, cb) if cb then cb(false, "") end end,
                SetPersistentString = function() end,
                GetRenderState = function() return 0 end,
                FindEntities = function() return { fire_ent27 } end,
            },
            TheWorld = { state = wstate27, HasTag = function() return false end },
            ThePlayer = {
                Transform = { GetWorldPosition = function() return 0.0, 0.0, 0.0 end },
                HUD = {},
            },
            TheCamera = {
                currentpos = { x = 0.0, y = 0.0, z = 0.0 },
                GetRightVec = function() return { x = 0.707, y = 0.0, z = -0.707 } end,
                GetDownVec  = function() return { x = 0.707, y = 0.0, z = 0.707 } end,
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
            IsPaused = function() return false end,
            SamplerSizes = { Relative = 1 },
            SamplerColourMode = { RGB = 1 },
            SamplerEffectBase = { BloomSampler = 1, Shader = 2 },
            FILTER_MODE = { LINEAR = 1 }, MIP_FILTER_MODE = { NONE = 0 }, WRAP_MODE = { CLAMP_TO_EDGE = 1 },
            PostProcessorEffects = { Bloom = 1, ColourCube = 2 },
            SamplerEffects = {}, UniformVariables = {},
            PostProcessor = G.setmetatable({}, {
                __index = {
                    SetBloomSamplerParams = function() end, AddUniformVariable = function(_, n) return "U_" .. n end,
                    AddSamplerEffect = function(_, p) return "SE_" .. p end, AddPostProcessEffect = function(_, p) return "PPE_" .. p end,
                    AddSampler = function() end, SetEffectUniformVariables = function() end,
                    SetSamplerEffectState = function() end, SetSamplerEffectFilter = function() end,
                    SetPostProcessEffectAfter = function() end, EnablePostProcessEffect = function() end,
                    SetUniformVariable = function() end, IsBloomEnabled = function() return false end,
                    SetBloomEnabled = function() end,
                },
            }),
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
            HyperSmooth_SetWorldStudio = function() return true end,
            HyperSmooth_SetShadowDyn   = function() return true end,
            HyperSmooth_SetShadowConfig = function(...) sc27[#sc27 + 1] = { ... }; return true, false end,
            HyperSmooth_SetPointLights  = function(...) pl27[#pl27 + 1] = { ... }; return select(3, ...) or 0 end,
        }
        local env27 = {
            GLOBAL = g27, MODROOT = "../mods/client_v24_lua_test/", Asset = function(t, p) return { t, p } end,
            GetModConfigData = function(k) if k == "visual_preset" then return 2 end return nil end,
            AddModShadersInit = function(fn) sh_init_27 = fn end,
            AddModShadersSortAndEnable = function(fn) sh_sort_27 = fn end,
            AddPrefabPostInit = function(name, fn)
                prefab_hooks_27[name] = prefab_hooks_27[name] or {}
                prefab_hooks_27[name][#prefab_hooks_27[name] + 1] = fn
            end,
            AddPrefabPostInitAny = function() end,
            AddClassPostConstruct = function(cls, fn)
                if cls == "screens/playerhud" then
                    local mock_hud = { AddChild = function(_, w) return w end }
                    fn(mock_hud)
                    hud_upd_27 = mock_hud.OnUpdate
                end
            end,
            AddComponentPostInit = function() end, AddSimPostInit = function() end,
            AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
            modimport = function() end,
        }
        local fn27 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setmetatable(env27, { __index = g27 })
        G.setfenv(fn27, env27); fn27()
        sh_init_27(); sh_sort_27()

        -- Configure full MingYuHui-style crisp parity profile:
        g27.c_hstune("shadow_density", 0.50)
        g27.c_hstune("shadow_soft", 0.00)
        g27.c_hstune("shadow_tint_r", 0.00)
        g27.c_hstune("shadow_tint_g", 0.00)
        g27.c_hstune("shadow_tint_b", 0.00)
        g27.c_hstune("shadow_oval_keep", 0.00)
        g27.c_hstune("shadow_weather", 1.00)
        g27.c_hstune("shadow_moon_blue", 1.00)
        g27.c_hstune("shadow_dusk_point", 0.00)
        g27.c_hstune("shadow_min_height", 0.85)
        g27.c_hstune("shadow_flash_lights", 1)

        -- 1) Clear Autumn Day: soft=0.00, pure black tint (0,0,0), minHeight=0.85, density=0.50
        for _ = 1, 20 do hud_upd_27({}, 0.25) end
        local d_aut = sc27[#sc27]
        assert(math.abs(d_aut[1] - 0.50) < 0.02 and d_aut[2] == 0.00 and d_aut[3] == 0.00 and d_aut[4] == 0.00 and d_aut[5] == 0.00
            and math.abs((d_aut[14] or -1) - 0.85) < 1e-3,
            "Test 27.1: Autumn day crisp black + minHeight mismatch: " .. tostring(d_aut[1]) .. "/" .. tostring(d_aut[2]) .. "/" .. tostring(d_aut[14]))

        -- 2) Rainy Autumn Day: smoothly decays directional density to 0.50 * 0.60 = 0.30!
        wstate27.precipitation = "rain"
        for _ = 1, 25 do hud_upd_27({}, 0.25) end
        local d_rain = sc27[#sc27]
        assert(math.abs(d_rain[1] - 0.30) < 0.03, "Test 27.2: Rain decay expected ~0.30, got " .. tostring(d_rain[1]))

        -- 3) Clear Summer vs Clear Winter: Summer -> 0.50 * 1.20 = 0.60, Winter -> 0.50 * 0.80 = 0.40!
        wstate27.precipitation = "none"; wstate27.season = "summer"
        for _ = 1, 25 do hud_upd_27({}, 0.25) end
        local d_sum = sc27[#sc27]
        assert(math.abs(d_sum[1] - 0.60) < 0.03, "Test 27.3a: Summer boost expected ~0.60, got " .. tostring(d_sum[1]))
        wstate27.season = "winter"
        for _ = 1, 25 do hud_upd_27({}, 0.25) end
        local d_win = sc27[#sc27]
        assert(math.abs(d_win[1] - 0.40) < 0.03, "Test 27.3b: Winter decay expected ~0.40, got " .. tostring(d_win[1]))

        -- 4) Full Moon Night: tint automatically shifts from (0,0,0) -> (0.10, 0.15, 0.30)!
        wstate27.season = "autumn"; wstate27.phase = "night"; wstate27.timeinphase = 0.5; wstate27.isfullmoon = true
        for _ = 1, 30 do hud_upd_27({}, 0.25) end
        local d_moon = sc27[#sc27]
        assert(math.abs(d_moon[3] - 0.10) < 0.02 and math.abs(d_moon[4] - 0.15) < 0.02 and math.abs(d_moon[5] - 0.30) < 0.02,
            string.format("Test 27.4: Full-moon blue tint expected (0.10,0.15,0.30), got (%.3f,%.3f,%.3f)", d_moon[3], d_moon[4], d_moon[5]))

        -- 5) Dusk Firelight Gate: shadow_dusk_point=0 -> 0 fire point lights at dusk; night -> fire point light activates!
        wstate27.phase = "dusk"; wstate27.timeinphase = 0.5; wstate27.isfullmoon = false
        for _ = 1, 30 do hud_upd_27({}, 0.25) end
        local pl_dusk0 = pl27[#pl27]
        assert((pl_dusk0[3] or 0) == 0, "Test 27.5a: shadow_dusk_point=0 must suppress campfire point shadow at dusk! got " .. tostring(pl_dusk0[3]))
        g27.c_hstune("shadow_dusk_point", 1.00)
        for _ = 1, 10 do hud_upd_27({}, 0.25) end
        local pl_dusk1 = pl27[#pl27]
        assert((pl_dusk1[3] or 0) >= 1, "Test 27.5b: shadow_dusk_point=1 must allow campfire point shadow at dusk!")
        g27.c_hstune("shadow_dusk_point", 0.00)
        wstate27.phase = "night"; wstate27.isfullmoon = false
        for _ = 1, 30 do hud_upd_27({}, 0.25) end
        local pl_night = pl27[#pl27]
        assert((pl_night[3] or 0) >= 1, "Test 27.5c: Night must still cast campfire point shadow even when shadow_dusk_point=0!")

        -- 6) Transient Lightning Flash Shadow: spawn mock lightning prefab -> immediate point shadow, decays to 0 after 1.1s!
        wstate27.phase = "day"; wstate27.timeinphase = 0.5
        for _ = 1, 25 do hud_upd_27({}, 0.25) end
        assert((pl27[#pl27][3] or 0) == 0, "Test 27.6a: Clear day should have 0 point lights before lightning")
        local bolt = {
            IsValid = function() return true end,
            Transform = { GetWorldPosition = function() return 8.0, 0.0, -5.0 end },
            DoTaskInTime = function(self, _, cb) cb(self) end,
        }
        assert(prefab_hooks_27["lightning"] and #prefab_hooks_27["lightning"] >= 1, "Test 27.6b: lightning hook missing")
        for i = 1, #prefab_hooks_27["lightning"] do prefab_hooks_27["lightning"][i](bolt) end
        hud_upd_27({}, 0.05)
        local pl_bolt = pl27[#pl27]
        assert((pl_bolt[3] or 0) >= 1 and math.abs((pl_bolt[4] or 0) - 8.0) < 0.1 and (pl_bolt[7] or 0) > 0.50,
            "Test 27.6c: Lightning strike did not emit immediate transient flash shadow!")
        for _ = 1, 10 do hud_upd_27({}, 0.15) end
        assert((pl27[#pl27][3] or 0) == 0, "Test 27.6d: Lightning flash shadow failed to decay cleanly after 1s!")

        -- 7) HS1 ShareCode roundtrip for #106..#110 + soft=0.00!
        local code27 = env27.HS_PanelApi.ExportShareCode("MingYuHui-Parity")
        local ok27, err27, st27 = env27.HS_PanelApi.ValidateShareCode(code27)
        assert(ok27 and st27 and st27.tune
            and st27.tune.shadow_soft == 0.00
            and math.abs(st27.tune.shadow_weather - 1.00) < 0.02
            and math.abs(st27.tune.shadow_moon_blue - 1.00) < 0.02
            and math.abs(st27.tune.shadow_dusk_point - 0.00) < 0.02
            and math.abs(st27.tune.shadow_min_height - 0.85) < 0.02
            and st27.tune.shadow_flash_lights == 1,
            "Test 27.7: HS1 #106..#110 ShareCode roundtrip failed: " .. tostring(err27))
        print("[CLIENT_V24_TEST] 27. v2.9.5-E Shadow Pipeline Parity (soft=0 + Rain/Summer/Winter + FullMoonBlue + DuskFireGate + MinHeight + LightningFlash + HS1 #106..#110) PASS")
    end

    -- Test 28: v2.9.5-F PBR Ocean Optics + Horizontal Volumetric Fog (World-Edge Void Mist & Lit Ground Fog) + HS1 #111..#116!
    do
        -- Part A: hs_vollight.lua Horizontal Volumetric Fog under strict.lua
        local env_c28, env_s28 = {}, {}
        local fog_hosts28 = {}
        local fog_emitted28 = { [0] = 0, [1] = 0 }
        local fog_cleared28 = { [0] = 0, [1] = 0 }
        local fog_spawn_vec28 = {}
        local fog_res28 = {}
        local cur_tile28 = 1 -- WORLD_TILES.IMPASSABLE
        local is_cave28 = false
        local wstate28 = { isdusk = false, isnight = false, isfullmoon = false, israining = false }

        local function MakeEnt28()
            local e = { _valid = true, _constructed = false }
            e.IsValid = function(self) return self._valid end
            e.Remove = function(self) self._valid = false end
            e.AddTag = function() end
            e.DoPeriodicTask = function() end
            e.entity = {
                SetCanSleep = function() end,
                AddTransform = function()
                    e.Transform = {
                        _x = 0, _y = 0, _z = 0,
                        SetPosition = function(self, x, y, z) self._x, self._y, self._z = x, y, z end,
                        SetEightFaced = function() end,
                        SetRotation = function() end,
                    }
                    return e.Transform
                end,
                AddAnimState = function()
                    e.AnimState = {
                        SetBank = function() end, SetBuild = function() end, PlayAnimation = function() end,
                        SetLightOverride = function() end, SetFinalOffset = function() end, SetScale = function() end,
                        Hide = function() end, Show = function() end, OverrideMultColour = function() end,
                    }
                    return e.AnimState
                end,
                AddVFXEffect = function()
                    local vfx = {
                        _emitters = 0,
                        InitEmitters = function(self, n) self._emitters = n end,
                        SetRenderResources = function(_, idx, tex, sh) fog_res28[idx] = { tex = tex, sh = sh } end,
                        SetMaxNumParticles = function() end,
                        SetMaxLifetime = function() end,
                        SetColourEnvelope = function() end,
                        SetScaleEnvelope = function() end,
                        SetBlendMode = function() end,
                        SetUVFrameSize = function() end,
                        SetRotationStatus = function() end,
                        SetDragCoefficient = function() end,
                        SetSpawnVectors = function(_, idx, x1, y1, z1, x2, y2, z2)
                            fog_spawn_vec28[idx] = { x1, y1, z1, x2, y2, z2 }
                        end,
                        SetSortOrder = function() end,
                        SetSortOffset = function() end,
                        SetRadius = function() end,
                        ClearAllParticles = function(_, idx) fog_cleared28[idx] = (fog_cleared28[idx] or 0) + 1 end,
                        AddRotatingParticle = function(_, idx) fog_emitted28[idx] = (fog_emitted28[idx] or 0) + 1 end,
                        AddParticle = function(_, idx) fog_emitted28[idx] = (fog_emitted28[idx] or 0) + 1 end,
                    }
                    e.VFXEffect = vfx
                    fog_hosts28[#fog_hosts28 + 1] = e
                    return vfx
                end,
                CallPrefabConstructionComplete = function() e._constructed = true end,
            }
            return e
        end

        local g28 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error,
            FRAMES = 1.0 / 30.0,
            BLENDMODE = { Premultiplied = 2, Additive = 1 },
            TheNet = { IsDedicated = function() return false end },
            ThePlayer = {
                IsValid = function() return true end,
                Transform = { GetWorldPosition = function() return 120.0, 0.0, -80.0 end },
            },
            TheWorld = {
                state = wstate28,
                HasTag = function(_, tag) return tag == "cave" and is_cave28 end,
                Map = {
                    GetTileAtPoint = function() return cur_tile28 end,
                    IsVisualGroundAtPoint = function() return cur_tile28 ~= 1 and cur_tile28 ~= 255 end,
                },
            },
            TheSim = { FindEntities = function() return {} end },
            WORLD_TILES = { IMPASSABLE = 1, INVALID = 255, FOREST = 7, MARSH = 8, DECIDUOUS = 30, FUNGUS = 14, GRASS = 6 },
            EnvelopeManager = {
                AddColourEnvelope = function(_, k, v) env_c28[k] = v end,
                AddVector2Envelope = function(_, k, v) env_s28[k] = v end,
            },
            CreateEntity = MakeEnt28,
            resolvefilepath = function(p) return p end,
        }
        setmetatable(g28, {
            __newindex = function(_, k, _)
                G.error("strict.lua violation in hs_vollight fog: assign to undeclared '" .. tostring(k) .. "'", 2)
            end,
        })
        local env28v = { GLOBAL = g28, AddPlayerPostInit = function() end }
        local fl28 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_hs_vollight.lua"))
        setfenv(fl28, env28v); fl28()
        local VL28 = env28v.HS_VOLLIGHT

        -- 1) Vanilla Pure (0,0,0,0,0): 0 fog hosts, 0 particles
        VL28.ApplyConfig(0, 0, 0, 0, 0)
        for _ = 1, 5 do VL28.StepUpdate(0.15) end
        assert(#fog_hosts28 == 0 and fog_emitted28[0] == 0 and fog_emitted28[1] == 0,
            "Test 28.1: Vanilla Pure must spawn 0 fog host/particles!")

        -- 2) World-Edge Void Ocean Mist (vol_edge_fog=1.0, vol_ground_fog=0.0) near IMPASSABLE void
        VL28.ApplyConfig(0, 0, 0, 1.0, 0.0)
        for _ = 1, 15 do VL28.StepUpdate(0.20) end
        assert(#fog_hosts28 == 1 and fog_hosts28[1]._constructed == true,
            "Test 28.2a: Fog VFX host MUST call CallPrefabConstructionComplete() after InitEmitters(2)!")
        assert(fog_hosts28[1].Transform._x == 0 and fog_hosts28[1].Transform._z == 0,
            "Test 28.2a2: Fog host MUST stay world-anchored at (0,0,0) so moving player NEVER drags existing mist!")
        assert(fog_res28[0].tex == "levels/textures/ds_fog1.tex" and fog_res28[0].sh == "shaders/vfx_particle.ksh",
            "Test 28.2b: Edge fog must use ds_fog1.tex + lightmap-lit vfx_particle.ksh!")
        assert(fog_spawn_vec28[0][1] == -1 and fog_spawn_vec28[0][2] == 0 and fog_spawn_vec28[0][3] == 1
            and fog_spawn_vec28[0][4] == 1 and fog_spawn_vec28[0][5] == 0 and fog_spawn_vec28[0][6] == 1,
            "Test 28.2c: Edge fog must use horizontal XZ SetSpawnVectors(-1,0,1, 1,0,1)!")
        assert(fog_emitted28[0] > 0 and fog_emitted28[1] == 0 and VL28.GetStats().edge_fog_emitted == fog_emitted28[0],
            "Test 28.2d: Edge fog emitted mismatch: " .. tostring(fog_emitted28[0]) .. "/" .. tostring(fog_emitted28[1]))

        -- 3) Lightmap-Lit Forest / Marsh / Cave Ground Fog (vol_edge_fog=0.0, vol_ground_fog=1.0)
        cur_tile28 = 8 -- MARSH
        VL28.ApplyConfig(0, 0, 0, 0.0, 1.0)
        for _ = 1, 20 do VL28.StepUpdate(0.20) end
        assert(fog_cleared28[0] > 0 and fog_emitted28[1] > 0 and VL28.GetStats().ground_fog_emitted == fog_emitted28[1],
            "Test 28.3: Ground fog must clear emitter 0 and emit on MARSH/FOREST/CAVE tiles!")

        -- 4) Sunbeams Everywhere (vol_lightrays_everywhere): GRASS base turf spawns 0 rays when OFF, >0 rays when ON, evicts cleanly on OFF!
        cur_tile28 = 6 -- GRASS (non-forest base turf)
        VL28.ApplyConfig(1.0, 0, 0, 0.0, 0.0, 0)
        for _ = 1, 6 do VL28.StepUpdate(0.15) end
        assert(VL28.GetStats().active_rays == 0 and VL28.GetStats().non_forest_rays == 0,
            "Test 28.4a: GRASS turf must spawn 0 rays when vol_lightrays_everywhere=0!")
        VL28.ApplyConfig(1.0, 0, 0, 0.0, 0.0, 1)
        for _ = 1, 6 do VL28.StepUpdate(0.15) end
        assert(VL28.GetStats().active_rays > 0 and VL28.GetStats().non_forest_rays == VL28.GetStats().active_rays,
            "Test 28.4b: GRASS turf must spawn Waterlogged-style sunbeams when vol_lightrays_everywhere=1!")
        VL28.ApplyConfig(1.0, 0, 0, 0.0, 0.0, 0)
        assert(VL28.GetStats().non_forest_rays == 0 and VL28.GetStats().active_rays == 0,
            "Test 28.4c: Disabling vol_lightrays_everywhere must immediately clear non-forest base sunbeams!")

        -- 4d) Clean Shutdown: clears both emitters & removes fog host cleanly!
        VL28.Shutdown()
        assert(fog_hosts28[1]._valid == false and VL28.GetStats().fog_host_ready == false,
            "Test 28.4d: Shutdown must destroy fog VFX host cleanly!")

        -- Part B: modmain.lua PBR Ocean native bus + Preset 0 purity + HS1 #111..#116 roundtrip!
        local oc_pbr_calls = {}
        local vl_apply_calls = {}
        local sh_init_28, sh_sort_28, hud_upd_28
        local g28m = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os, tonumber = G.tonumber, tostring = G.tostring,
            print = G.print, error = G.error, select = G.select, assert = G.assert,
            TheSim = {
                GetPersistentString = function(_, _, cb) if cb then cb(false, "") end end,
                SetPersistentString = function() end,
                FindEntities = function() return {} end,
            },
            TheInput = { AddKeyUpHandler = function() end, AddKeyDownHandler = function() end, IsKeyDown = function() return false end },
            TheNet = { IsDedicated = function() return false end, GetIsServer = function() return false end, GetIsClient = function() return true end, GetServerGameMode = function() return "survival" end },
            KnownModIndex = { IsModEnabled = function() return false end, GetModsToLoad = function() return {} end },
            Profile = { GetBloomEnabled = function() return true end, SetBloomEnabled = function() end },
            SamplerSizes = { Relative = 1 }, SamplerColourMode = { RGB = 1 }, SamplerEffectBase = { BloomSampler = 1, Shader = 2 },
            FILTER_MODE = { LINEAR = 1 }, MIP_FILTER_MODE = { NONE = 0 }, WRAP_MODE = { CLAMP_TO_EDGE = 1 },
            PostProcessorEffects = { Bloom = 1, ColourCube = 2 }, SamplerEffects = {}, UniformVariables = {},
            PostProcessor = G.setmetatable({}, {
                __index = {
                    SetBloomSamplerParams = function() end, AddUniformVariable = function(_, n) return "U_" .. n end,
                    AddSamplerEffect = function(_, p) return "SE_" .. p end, AddPostProcessEffect = function(_, p) return "PPE_" .. p end,
                    AddSampler = function() end, SetEffectUniformVariables = function() end,
                    SetSamplerEffectState = function() end, SetSamplerEffectFilter = function() end,
                    SetPostProcessEffectAfter = function() end, EnablePostProcessEffect = function() end,
                    SetUniformVariable = function() end, IsBloomEnabled = function() return false end,
                    SetBloomEnabled = function() end,
                    SetColourCubeData = function() end, SetColourCubeLerp = function() end, SetDistortionFactor = function() end,
                },
            }),
            TheWorld = {
                state = { phase = "day", timeinphase = 0.5, season = "autumn", isfullmoon = false, israining = false, precipitation = "none" },
                HasTag = function() return false end,
            },
            ThePlayer = { IsValid = function() return true end, Transform = { GetWorldPosition = function() return 0, 0, 0 end } },
            TheCamera = { currentpos = { x = 0, y = 0, z = 0 }, GetRightVec = function() return { x = 0.707, y = 0, z = -0.707 } end },
            HyperSmooth_SetOceanPbr = function(a, b, c, d) oc_pbr_calls[#oc_pbr_calls + 1] = { a, b, c, d }; return true end,
            HyperSmooth_SetWorldStudio = function() return true end,
            HyperSmooth_SetShadowDyn = function() return true end,
            HyperSmooth_SetShadowConfig = function() return true, false end,
            HyperSmooth_SetPointLights = function() return 0 end,
            HyperSmooth_SetWaterRefl = function() return true, 0 end,
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
            require = function(p)
                if p == "widgets/text" then
                    return function()
                        return G.setmetatable({ inst = { IsValid = function() return true end, entity = { IsValid = function() return true end } } }, { __index = function() return function() end end })
                    end
                end
                return {}
            end,
        }
        local env28m = {
            GLOBAL = g28m,
            MODROOT = "../mods/client_v24_lua_test/",
            HS_VOLLIGHT = {
                ApplyConfig = function(a, b, c, d, e, f) vl_apply_calls[#vl_apply_calls + 1] = { a, b, c, d, e, f } end,
            },
            Asset = function(t, p) return { type = t, file = p } end,
            GetModConfigData = function(k) if k == "visual_preset" then return 0 end return nil end,
            AddModShadersInit = function(cb) sh_init_28 = cb end,
            AddModShadersSortAndEnable = function(cb) sh_sort_28 = cb end,
            AddClassPostConstruct = function(cls, cb)
                if cls == "screens/playerhud" then
                    local mock = { AddChild = function(_, w) return w end, OnUpdate = function() end }
                    cb(mock); hud_upd_28 = mock.OnUpdate
                end
            end,
            AddPrefabPostInit = function() end, AddPrefabPostInitAny = function() end, AddPlayerPostInit = function() end,
            AddComponentPostInit = function() end, AddSimPostInit = function() end,
            AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
            modimport = function() end,
        }
        local fn28m = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setmetatable(env28m, { __index = g28m })
        G.setfenv(fn28m, env28m); fn28m()
        sh_init_28(); sh_sort_28()

        -- 5) Preset 0 purity: HyperSmooth_SetOceanPbr receives (0,0,0,0), HS_VOLLIGHT receives (0,0,0,0,0)
        hud_upd_28({}, 0.05)
        local p0 = oc_pbr_calls[#oc_pbr_calls]
        local v0 = vl_apply_calls[#vl_apply_calls]
        assert(p0 and p0[1] == 0 and p0[2] == 0 and p0[3] == 0 and p0[4] == 0,
            "Test 28.5a: Preset 0 must push HyperSmooth_SetOceanPbr(0,0,0,0)!")
        assert(v0 and v0[4] == 0 and v0[5] == 0,
            "Test 28.5b: Preset 0 must push vol_edge_fog=0 & vol_ground_fog=0!")

        -- 6) Tune ocean_pbr + sky_refl + sun_glow + hide_waves + edge/ground fog from Preset 0 -> switches to Preset 3 & pushes live!
        g28m.c_hstune("ocean_pbr", 1.00)
        g28m.c_hstune("ocean_sky_refl", 0.90)
        g28m.c_hstune("ocean_sun_glow", 0.95)
        g28m.c_hstune("ocean_hide_waves", 1)
        g28m.c_hstune("vol_edge_fog", 1.10)
        g28m.c_hstune("vol_ground_fog", 0.75)
        env28m.HS_PanelApi.ToggleTuneBool("vol_lightrays_everywhere")
        hud_upd_28({}, 0.05)
        local p1 = oc_pbr_calls[#oc_pbr_calls]
        local v1 = vl_apply_calls[#vl_apply_calls]
        assert(math.abs(p1[1] - 1.00) < 0.01 and math.abs(p1[2] - 0.90) < 0.01 and math.abs(p1[3] - 0.95) < 0.01 and p1[4] == 1,
            "Test 28.6a: HyperSmooth_SetOceanPbr propagation mismatch!")
        assert(math.abs(v1[1] - 0.85) < 0.02 and math.abs(v1[4] - 1.10) < 0.01 and math.abs(v1[5] - 0.75) < 0.01 and v1[6] == 1,
            "Test 28.6b: HS_VOLLIGHT edge/ground fog & lightrays_everywhere propagation mismatch!")

        -- 7) HS1 ShareCode #111..#117 export -> validate -> clean import from Preset 0!
        local code28 = env28m.HS_PanelApi.ExportShareCode("ShadowShade-Ocean")
        local ok28, err28, st28 = env28m.HS_PanelApi.ValidateShareCode(code28)
        assert(ok28 and st28 and st28.tune
            and math.abs(st28.tune.ocean_pbr - 1.00) < 0.02
            and math.abs(st28.tune.ocean_sky_refl - 0.90) < 0.02
            and math.abs(st28.tune.ocean_sun_glow - 0.95) < 0.02
            and st28.tune.ocean_hide_waves == 1
            and math.abs(st28.tune.vol_edge_fog - 1.10) < 0.02
            and math.abs(st28.tune.vol_ground_fog - 0.75) < 0.02
            and st28.tune.vol_lightrays_everywhere == 1,
            "Test 28.7a: HS1 #111..#117 validate failed: " .. tostring(err28))
        env28m.HS_PanelApi.SetPreset(0)
        local impOk, impErr = env28m.HS_PanelApi.ImportShareCode(code28)
        assert(impOk, "Test 28.7b: HS1 #111..#117 import failed: " .. tostring(impErr))
        hud_upd_28({}, 0.05)
        local p2 = oc_pbr_calls[#oc_pbr_calls]
        local v2 = vl_apply_calls[#vl_apply_calls]
        assert(math.abs(p2[1] - 1.00) < 0.02 and p2[4] == 1 and v2[6] == 1,
            "Test 28.7c: Imported HS1 did not restore PBR Ocean + Sunbeams Everywhere!")
        print("[CLIENT_V24_TEST] 28. v2.9.5-F/G PBR Ocean Optics + Volumetric Fog + Base Sunbeams + HS1 #111..#117 PASS")
    end

    -- 29. HyperShade Visual Closed-Loop Lua Bridge (HS_SHARE.Look + c_hslook + ShareCard UI + Whitelist Preservation + Undo)
    do
        local hud_upd_29 = nil
        local started_modes = {}
        local poll_queue = {}
        local mock_caps = {
            { key = "exposure", min = -0.8, max = 0.8, default = 0.0, general = true, same_scene = true, stage = 1 },
            { key = "temp",     min = -1.0, max = 1.0, default = 0.0, general = true, same_scene = true, stage = 1 },
            { key = "vignette", min =  0.0, max = 0.5, default = 0.0, general = false, same_scene = true, stage = 3 },
        }
        local function MW29()
            local w = { visible = true, children = {}, inst = { IsValid = function() return true end } }
            setmetatable(w, { __index = function(self, k)
                if k == "AddChild" then return function(s, c) s.children[#s.children + 1] = c; c._parent = s; return c end end
                if k == "SetText" then return function(s, t) s._txt = t end end
                if k == "SetString" then return function(s, t) s._str = t end end
                if k == "SetOnClick" then return function(s, fn) s._onclick = fn end end
                if k == "Show" then return function(s) s.visible = true end end
                if k == "Hide" then return function(s) s.visible = false end end
                if k == "SetPosition" then return function(s, x, y, z) s._pos = { x, y, z } end end
                return function() end
            end })
            return w
        end
        local hud29 = MW29()
        local g29 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable, type = G.type,
            pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = function() end, error = G.error,
            BODYTEXTFONT = "bodyfont", NUMBERFONT = "numfont",
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, ANCHOR_TOP = 1, SCALEMODE_PROPORTIONAL = 2,
            STRINGS = { UI = { OPTIONS = { ENABLED = "Enabled", DISABLED = "Disabled" } } },
            ThePlayer = { HUD = hud29 },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                GetClipboardData = function() return "" end,
            },
            TheInput = {
                OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end,
                GetScreenPosition = function() return { x = 0, y = 0, z = 0 } end,
                IsMouseDown = function() return false end,
            },
            MOUSEBUTTON_LEFT = 1000,
            GetTimeRealSeconds = function() return 100 end,
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_SetSkeletal = function() return true end,
            HyperSmooth_SetVfxSmooth = function() return true end,
            HyperSmooth_SetLowLatency = function() return true end,
            HyperSmooth_SetVisual = function() return true end,
            HyperSmooth_SetHd2d = function() return true end,
            HyperSmooth_SetClipboard = function() return true end,
            HyperSmooth_GetFrameStats = function() return 240.0, 232.0, 4.18, 0.02, 0, 30 end,
            HyperSmooth_StartLookMatch = function(mode, fpath)
                started_modes[#started_modes + 1] = { mode = mode, path = fpath }
                if mode == "fail_text" then
                    return false, "clipboard_has_text_not_image"
                end
                return true, #started_modes
            end,
            HyperSmooth_PollLookMatch = function()
                if #poll_queue > 0 then
                    local nxt = table.remove(poll_queue, 1)
                    return nxt[1], nxt[2]
                end
                return "idle"
            end,
            HyperSmooth_GetLookCapabilities = function() return mock_caps end,
            require = function(p)
                if p == "widgets/widget" or p == "widgets/image" or p == "widgets/imagebutton" or p == "widgets/text" then
                    return function() return MW29() end
                end
                return {}
            end,
        }
        local env29
        env29 = {
            GLOBAL = g29,
            GetModConfigData = function(k)
                if k == "target_fps" then return 240 end
                return false
            end,
            AddClassPostConstruct = function(cls, fn)
                if cls == "screens/playerhud" then
                    local fake_hud = MW29()
                    fake_hud.controls = { bottomright_root = MW29() }
                    fake_hud.inst.DoTaskInTime = function() end
                    fake_hud.OnUpdate = function() end
                    fn(fake_hud)
                    hud_upd_29 = fake_hud.OnUpdate
                end
            end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function() end,
            AddClientModRPCHandler = function() end,
            modimport = function(name)
                if name == "hs_panel" then
                    local fp, ep = G.kleiloadlua("../mods/client_v24_lua_test/target_hs_panel.lua")
                    assert(fp ~= nil, "Test 29 hs_panel.lua load error: " .. tostring(ep))
                    setfenv(fp, env29)
                    fp()
                end
            end,
        }
        local f29 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(f29, env29)
        f29()

        local api29 = env29.HS_PanelApi
        assert(type(api29.StartLookMatch) == "function" and type(api29.GetLookCapabilities) == "function",
            "Test 29.1: panel_api Look Match exports missing!")
        assert(type(g29.c_hslook) == "function" and type(g29.c_hsmatch) == "function",
            "Test 29.1b: c_hslook / c_hsmatch console commands missing!")
        assert(#api29.GetLookCapabilities() == 3, "Test 29.1c: GetLookCapabilities mismatch!")

        -- Set non-grading settings first (must be 100% preserved after Look Match applies!)
        g29.c_hstune("shadow_density", 0.72)
        g29.c_hstune("water_refl_str", 0.65)
        g29.c_hstune("wind_sway_enabled", 1)
        g29.c_hstune("exposure", -0.35) -- previous grading MUST be replaced cleanly, not stacked!

        -- 29.2 Async lifecycle: capturing -> solving -> done -> transactional HS1 apply -> Undo
        poll_queue = {
            { "capturing" },
            { "solving" },
            { "done", {
                mode = "same_scene",
                input_source = "clipboard_dibv5",
                readback_path = "async_pbo",
                warning = "",
                ref_width = 1920, ref_height = 1080, elapsed_ms = 14.2,
                zncc = 0.96, grad_corr = 0.91,
                delta_e_before = 9.4, delta_e_after = 1.3,
                confidence = 0.91, color_confidence = 0.92, spatial_confidence = 0.89,
                vignette_fitted = true, sharp_fitted = false,
                params = { exposure = 0.25, temp = 0.45, contrast = 0.16, sat = 1.22, vignette = 0.20, unowned_hack = 99.0 },
            } },
        }
        local cb_ok, cb_msg, cb_code = nil, nil, nil
        local s_ok = api29.StartLookMatch("clipboard", function(ok_r, msg_r, code_r)
            cb_ok, cb_msg, cb_code = ok_r, msg_r, code_r
        end)
        assert(s_ok == true and started_modes[#started_modes].mode == "clipboard", "Test 29.2a: StartLookMatch failed!")
        hud_upd_29({}, 0.016)
        assert(cb_ok == nil, "Test 29.2b: must stay pending while capturing!")
        hud_upd_29({}, 0.016)
        assert(cb_ok == nil, "Test 29.2c: must stay pending while solving!")
        hud_upd_29({}, 0.016)
        assert(cb_ok == true and type(cb_code) == "string" and cb_code:sub(1, 4) == "HS1-",
            "Test 29.2d: expected HS1- share code on completion! msg=" .. tostring(cb_msg))

        local sn = api29.GetSnapshot()
        assert(math.abs(sn.tune.exposure - 0.25) < 0.01 and math.abs(sn.tune.temp - 0.45) < 0.01
            and math.abs(sn.tune.vignette - 0.20) < 0.01,
            "Test 29.2e: fitted whitelist params not applied accurately!")
        assert(math.abs(sn.tune.shadow_density - 0.72) < 0.01
            and math.abs(sn.tune.water_refl_str - 0.65) < 0.01
            and sn.tune.wind_sway_enabled == 1,
            "Test 29.2f: non-grading settings (shadow/water/wind) were clobbered!")
        assert(sn.tune.unowned_hack == nil, "Test 29.2g: unknown param leaked past whitelist!")

        -- 29.3 One-click Undo restores exact previous exposure (-0.35)!
        assert(g29.c_hslook("undo") == true, "Test 29.3a: c_hslook('undo') failed!")
        local sn_undo = api29.GetSnapshot()
        assert(math.abs(sn_undo.tune.exposure - (-0.35)) < 0.01,
            "Test 29.3b: Undo did not restore prior exposure! got=" .. tostring(sn_undo.tune.exposure))

        -- 29.4 Error & no-delta guards + ShareCard UI Look Match buttons clickable
        local ok_err, msg_err = api29.StartLookMatch("fail_text")
        assert(ok_err == false and type(msg_err) == "string" and #msg_err > 5, "Test 29.4a: error propagation failed!")

        poll_queue = {
            { "done", { mode = "same_frame", warning = "no_post_colourcube_delta", params = {} } }
        }
        local nd_ok, nd_msg = nil, nil
        assert(api29.StartLookMatch("same_frame", function(o, m) nd_ok, nd_msg = o, m end) == true)
        hud_upd_29({}, 0.016)
        assert(nd_ok == false and type(nd_msg) == "string" and #nd_msg > 5, "Test 29.4b: no_post_colourcube_delta not handled!")

        assert(env29.HS_TogglePanel(true) == true, "Test 29.4c0: HS_TogglePanel failed!")
        local found_cb_btn, found_fr_btn = false, false
        local function walk29(w)
            if type(w._txt) == "string" and w._onclick then
                if w._txt:find("剪贴板", 1, true) or w._txt:find("Clipboard", 1, true) then found_cb_btn = true end
                if w._txt:find("同帧", 1, true) or w._txt:find("Capture Frame", 1, true) then found_fr_btn = true end
            end
            for _, c in ipairs(w.children or {}) do walk29(c) end
        end
        walk29(hud29.children[1])
        assert(found_cb_btn and found_fr_btn, "Test 29.4c: ShareCard Look Match buttons missing in hs_panel.lua!")

        print("[CLIENT_V24_TEST] 29. HyperShade Visual Closed-Loop (HS_SHARE.Look + c_hslook + Whitelist Preservation + Undo + ShareCard UI) PASS")
    end

    -- 30. Preset 0 Zero-Leak (Exact 0.25x Vanilla BloomSampler / Magiluminescence Parity + Instant VFX Clear)
    --     + Built-In Curated 6-Preset Gallery Panel (hs_panel.lua + HS_PanelApi + c_hspreset + HS1 Roundtrip + Undo)
    do
        local bloom_resize_calls = 0
        local pp_en30 = {}
        local vfx_clears30 = {}
        local sh_init30, sh_sort30
        local function MW30()
            local w = { visible = true, children = {}, inst = { IsValid = function() return true end } }
            setmetatable(w, { __index = function(self, k)
                if k == "AddChild" then return function(s, c) s.children[#s.children + 1] = c; c._parent = s; return c end end
                if k == "SetText" then return function(s, t) s._txt = t end end
                if k == "SetString" then return function(s, t) s._str = t end end
                if k == "SetOnClick" then return function(s, fn) s._onclick = fn end end
                if k == "Show" then return function(s) s.visible = true end end
                if k == "Hide" then return function(s) s.visible = false end end
                if k == "SetPosition" then return function(s, x, y, z) s._pos = { x, y, z } end end
                return function() end
            end })
            return w
        end
        local hud30 = MW30()
        local g30 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = function() end, error = G.error, select = G.select, assert = G.assert,
            FRAMES = 1.0 / 30.0,
            BLENDMODE = { Premultiplied = 2, Additive = 1 },
            BODYTEXTFONT = "bodyfont", NUMBERFONT = "numfont",
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, ANCHOR_TOP = 1, SCALEMODE_PROPORTIONAL = 2,
            STRINGS = { UI = { OPTIONS = { ENABLED = "启用", DISABLED = "禁用" } } },
            LanguageTranslator = { defaultlang = "zh" },
            TheNet = { IsDedicated = function() return false end, GetIsServer = function() return false end, GetIsClient = function() return true end, GetServerGameMode = function() return "survival" end },
            KnownModIndex = { IsModEnabled = function() return false end, GetModsToLoad = function() return {} end },
            TheSim = {
                GetPersistentString = function(_, _, cb) if cb then cb(false, "") end end,
                SetPersistentString = function() end,
                GetClipboardData = function() return "" end,
                FindEntities = function() return {} end,
            },
            TheInput = {
                OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end,
                GetScreenPosition = function() return { x = 0, y = 0, z = 0 } end,
                IsMouseDown = function() return false end,
            },
            MOUSEBUTTON_LEFT = 1000,
            GetTimeRealSeconds = function() return 200 end,
            SamplerSizes = { Relative = 1 }, SamplerColourMode = { RGB = 1 }, SamplerEffectBase = { BloomSampler = 1, Shader = 2 },
            FILTER_MODE = { LINEAR = 1 }, MIP_FILTER_MODE = { NONE = 0 }, WRAP_MODE = { CLAMP_TO_EDGE = 1 },
            PostProcessorEffects = { Bloom = "VANILLA_BLOOM", ColourCube = "CC" }, SamplerEffects = {}, UniformVariables = {},
            PostProcessor = G.setmetatable({}, {
                __index = {
                    SetBloomSamplerParams = function(_, _, w, h)
                        bloom_resize_calls = bloom_resize_calls + 1
                        assert(math.abs(w - 0.25) < 1e-4 and math.abs(h - 0.25) < 1e-4,
                            "Test 30.1: BloomSampler MUST stay at Klei vanilla 0.25x (got " .. tostring(w) .. ")!")
                    end,
                    AddUniformVariable = function(_, n) return "U_" .. n end,
                    AddSamplerEffect = function(_, p) return "SE_" .. p end,
                    AddPostProcessEffect = function(_, p) return "PPE_" .. p end,
                    AddSampler = function() end, SetEffectUniformVariables = function() end,
                    SetSamplerEffectState = function() end, SetSamplerEffectFilter = function() end,
                    SetPostProcessEffectAfter = function() end,
                    EnablePostProcessEffect = function(_, id, en) pp_en30[id] = en end,
                    SetUniformVariable = function() end,
                    IsBloomEnabled = function() return true end,
                    SetBloomEnabled = function(_, en) pp_en30["VANILLA_BLOOM"] = en end,
                    SetColourCubeData = function() end, SetColourCubeLerp = function() end, SetDistortionFactor = function() end,
                },
            }),
            EnvelopeManager = { AddColourEnvelope = function() end, AddVector2Envelope = function() end },
            CreateEntity = function()
                local e = { _valid = true, IsValid = function(s) return s._valid end, Remove = function(s) s._valid = false end }
                e.entity = {
                    SetCanSleep = function() end, AddTag = function() end, AddFollower = function() end, SetParent = function() end,
                    AddTransform = function() e.Transform = { SetPosition = function() end, GetWorldPosition = function() return 0, 0, 0 end }; return e.Transform end,
                    AddVFXEffect = function()
                        local vfx = {
                            InitEmitters = function(s, n) s._n = n end,
                            ClearAllParticles = function(_, idx) vfx_clears30[#vfx_clears30 + 1] = idx end,
                        }
                        G.setmetatable(vfx, { __index = function() return function() end end })
                        e.VFXEffect = vfx
                        return vfx
                    end,
                    CallPrefabConstructionComplete = function() end,
                }
                return e
            end,
            resolvefilepath = function(p) return p end,
            ThePlayer = { HUD = hud30, IsValid = function() return true end, Transform = { GetWorldPosition = function() return 0, 0, 0 end } },
            TheWorld = { state = { phase = "day", timeinphase = 0.5, season = "autumn", isfullmoon = false, israining = false }, HasTag = function() return false end },
            TheCamera = { currentpos = { x = 0, y = 0, z = 0 }, GetRightVec = function() return { x = 0.707, y = 0, z = -0.707 } end },
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_SetSkeletal = function() return true end,
            HyperSmooth_SetVfxSmooth = function() return true end,
            HyperSmooth_SetLowLatency = function() return true end,
            HyperSmooth_SetVisual = function() return true end,
            HyperSmooth_SetHd2d = function() return true end,
            HyperSmooth_SetWorldStudio = function() return true end,
            HyperSmooth_SetShadowDyn = function() return true end,
            HyperSmooth_SetShadowConfig = function() return true, false end,
            HyperSmooth_SetPointLights = function() return 0 end,
            HyperSmooth_SetWaterRefl = function() return true, 0 end,
            HyperSmooth_SetOceanPbr = function() return true end,
            HyperSmooth_SetDetailParams = function() return true end,
            HyperSmooth_SetCombatConfig = function() return true end,
            HyperSmooth_SetClipboard = function() return true end,
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
            require = function(modname)
                if modname == "widgets/widget" or modname == "widgets/image" or modname == "widgets/imagebutton"
                    or modname == "widgets/text" or modname == "widgets/textedit" then
                    return function() return MW30() end
                end
                return {}
            end,
        }
        local env30 = {
            GLOBAL = g30, MODROOT = "../mods/client_v24_lua_test/",
            Asset = function(t, p) return { type = t, file = p } end,
            GetModConfigData = function(k) if k == "visual_preset" then return 0 end return nil end,
            AddModShadersInit = function(cb) sh_init30 = cb end,
            AddModShadersSortAndEnable = function(cb) sh_sort30 = cb end,
            AddClassPostConstruct = function() end,
            AddPrefabPostInit = function() end, AddPrefabPostInitAny = function() end,
            AddPlayerPostInit = function() end, AddComponentPostInit = function() end,
            AddSimPostInit = function() end, AddGamePostInit = function() end,
            AddClientModRPCHandler = function() end,
        }
        env30.modimport = function(nm)
            local map = {
                hs_vfx = "../mods/client_v24_lua_test/target_hs_vfx.lua",
                hs_vollight = "../mods/client_v24_lua_test/target_hs_vollight.lua",
                hs_panel = "../mods/client_v24_lua_test/target_hs_panel.lua",
            }
            if map[nm] then
                local fn = assert(G.kleiloadlua(map[nm]))
                G.setfenv(fn, env30)
                fn()
            end
        end
        G.setmetatable(env30, { __index = g30 })
        local fn30 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setfenv(fn30, env30); fn30()
        sh_init30(); sh_sort30()

        -- 1) Preset 0 starts with 0 BloomSampler resizes, vanilla Bloom ON, HS_Bloom OFF, HS_VisualFinal OFF!
        assert(bloom_resize_calls == 0, "Test 30.1a: Preset 0 must NEVER call SetBloomSamplerParams!")
        assert(pp_en30["VANILLA_BLOOM"] == true and pp_en30[g30.PostProcessorEffects.HS_Bloom] == false and pp_en30[g30.PostProcessorEffects.HS_VisualFinal] == false,
            "Test 30.1b: Preset 0 must keep exact vanilla Bloom ON and HS_Bloom/HS_VisualFinal OFF!")

        -- 2) Switch Preset 2 -> Preset 0: BloomSampler STILL untouched at 0.25x + HS_VFX immediately calls ClearAllParticles!
        env30.HS_PanelApi.SetPreset(2)
        assert(pp_en30[g30.PostProcessorEffects.HS_Bloom] == true and pp_en30["VANILLA_BLOOM"] == false,
            "Test 30.2a: Preset 2 with game Bloom ON must use HS_Bloom!")
        vfx_clears30 = {}
        env30.HS_PanelApi.SetPreset(0)
        assert(bloom_resize_calls == 0, "Test 30.2b: Switching Preset 2 -> 0 must NEVER disturb vanilla 0.25x BloomSampler!")
        assert(pp_en30["VANILLA_BLOOM"] == true and pp_en30[g30.PostProcessorEffects.HS_Bloom] == false and pp_en30[g30.PostProcessorEffects.HS_VisualFinal] == false,
            "Test 30.2c: Switching Preset 2 -> 0 must restore exact vanilla Bloom & disable HS_VisualFinal!")
        assert(#vfx_clears30 >= 14, "Test 30.2d: Switching to Preset 0 must immediately ClearAllParticles on all VFX hosts (got " .. tostring(#vfx_clears30) .. ")!")

        -- 3) Built-In Curated 6-Preset Gallery: verify API, live hs_panel UI, mutual exclusion, HS1 roundtrip & Undo!
        local gallery = env30.HS_PanelApi.GetBuiltinPresets()
        assert(type(gallery) == "table" and #gallery == 6, "Test 30.3a: Expected 6 built-in curated presets, got " .. tostring(gallery and #gallery))
        assert(env30.HS_TogglePanel(true) == true, "Test 30.3b: Failed opening live panel!")
        local ui30 = g30.HS_PanelDebug
        assert(ui30 and ui30.preset_card and #ui30.preset_card.cards == 6 and type(ui30.TogglePresetCard) == "function",
            "Test 30.3c: Preset Gallery card or 6 cards missing in hs_panel.lua!")

        -- Open ShareCard then open PresetCard -> strict mutual exclusion!
        ui30.ToggleShareCard()
        ui30.TogglePresetCard(true)
        assert(ui30.preset_card.visible == true and ui30.preset_card.root.visible == true,
            "Test 30.3d: Preset Gallery card failed to show!")

        -- Click every single one of the 6 preset cards in the live UI + validate HS1 share code round-trip!
        local expected_ids = { "vanilla", "eyecare_hd", "storybook", "crisp_daylight", "cozy_firelight", "misty_canopy" }
        for i = 1, 6 do
            local card = ui30.preset_card.cards[i]
            assert(card.item.id == expected_ids[i], "Test 30.3e: Gallery order mismatch at #" .. tostring(i))
            card.btn._onclick()
            local sn = env30.HS_PanelApi.GetSnapshot()
            assert(sn.active_builtin_id == expected_ids[i],
                "Test 30.3f: Active builtin preset mismatch after clicking card #" .. tostring(i) .. ": got " .. tostring(sn.active_builtin_id))
            local code = env30.HS_PanelApi.ExportShareCode(card.item.zh)
            local vok, verr = env30.HS_PanelApi.ValidateShareCode(code)
            assert(vok == true, "Test 30.3g: HS1 code invalid for preset " .. card.item.id .. ": " .. tostring(verr))
        end

        -- Click Undo button in Preset Gallery -> reverts from misty_canopy back to cozy_firelight!
        ui30.preset_card.undo_btn._onclick()
        assert(env30.HS_PanelApi.GetSnapshot().active_builtin_id == "cozy_firelight",
            "Test 30.4a: Preset Gallery Undo did not revert to previous preset!")

        -- Manual slider tweak clears active_builtin_id (honest Custom state!)
        env30.HS_PanelApi.SetTune("exposure", 0.20)
        assert(env30.HS_PanelApi.GetSnapshot().active_builtin_id == nil,
            "Test 30.4b: Manual slider tweak must clear active_builtin_id!")

        -- Console c_hspreset() switches cleanly to crisp_daylight & vanilla!
        assert(g30.c_hspreset("crisp_daylight") == true and env30.HS_PanelApi.GetSnapshot().active_builtin_id == "crisp_daylight",
            "Test 30.4c: c_hspreset('crisp_daylight') failed!")
        assert(g30.c_hspreset(1) == true and env30.HS_PanelApi.GetSnapshot().preset == 0 and env30.HS_PanelApi.GetSnapshot().active_builtin_id == "vanilla",
            "Test 30.4d: c_hspreset(1) failed to restore Vanilla Pure!")

        print("[CLIENT_V24_TEST] 30. Preset 0 Zero-Leak (0.25x Magiluminescence Parity + Instant VFX Clear) & 6-Preset Gallery Panel PASS")
    end

    -- Test 31: Tiered Hardware Liberation (Low/Balanced/Unleashed + ;ht= persist + c_hstier) & Live Mod Culprit Profiler
    do
        local store31 = {}
        local native_tier_calls = {}
        local sim_clock31 = 0.0
        local captured_hook31 = nil
        local MW31
        MW31 = function()
            return setmetatable({
                shown = true, visible = true,
                AddChild = function(_, w) return w end,
                SetPosition = function() end, SetHAnchor = function() end, SetVAnchor = function() end,
                SetScaleMode = function() end, Show = function(s) s.visible = true end, Hide = function(s) s.visible = false end,
                MoveToFront = function() end, ScaleToSize = function() end, SetTint = function() end,
                SetRegionSize = function() end, SetHAlign = function() end, SetColour = function() end,
                SetClickable = function() end, SetString = function() end, ForceImageSize = function() end,
                SetText = function() end, SetTextSize = function() end, SetFont = function() end,
                SetTextColour = function() end, SetTextFocusColour = function() end,
                SetOnClick = function() end, SetOnGainFocus = function() end, SetOnLoseFocus = function() end,
            }, { __index = function() return function() end end })
        end

        local g31 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = function() end,
            json = G.json, os = { clock = function() return sim_clock31 end },
            debug = {
                sethook = function(cb) captured_hook31 = cb end,
                getinfo = function() return { short_src = "../mods/workshop-123456789/scripts/heavy_tick.lua", linedefined = 88, name = "HeavyThink" } end,
            },
            KnownModIndex = {
                GetModInfo = function(_, nm)
                    if nm == "workshop-123456789" then return { name = "Heavy Test Mod" } end
                    return {}
                end,
            },
            ANCHOR_RIGHT = 1, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 0, SCALEMODE_PROPORTIONAL = 1,
            TheSim = {
                GetPersistentString = function(_, fn, cb) cb(store31[fn] ~= nil, store31[fn] or "") end,
                SetPersistentString = function(_, fn, v) store31[fn] = v or "" end,
            },
            TheNet = { IsDedicated = function() return false end },
            TheInput = { AddKeyDownHandler = function() end, OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
            HyperSmooth_SetHardwareTier = function(t)
                native_tier_calls[#native_tier_calls + 1] = t
                return t, (t == 2 and "unleashed" or (t == 0 and "low" or "balanced"))
            end,
            HyperSmooth_GetHardwareInfo = function()
                local cur = native_tier_calls[#native_tier_calls] or 1
                return cur, 16, 32768, 4, 6, (cur >= 1), (cur == 2 and 256 or 64), (cur == 2 and 256 or 64), 4096, 8192, 0
            end,
            require = function(m)
                if string.find(m, "^widgets/") then return function() return MW31() end end
                return {}
            end,
        }
        local function load31()
            local sh_i, sh_s = nil, nil
            local e = {
                GLOBAL = g31, MODROOT = "../mods/client_v24_lua_test/",
                Asset = function() return {} end,
                GetModConfigData = function(k) if k == "visual_preset" then return 0 end return nil end,
                AddModShadersInit = function(f) sh_i = f end,
                AddModShadersSortAndEnable = function(f) sh_s = f end,
                AddClassPostConstruct = function() end, AddPrefabPostInit = function() end,
                AddPrefabPostInitAny = function() end, AddPlayerPostInit = function() end,
                AddComponentPostInit = function() end, AddSimPostInit = function() end,
                AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
                modimport = function() end,
            }
            G.setmetatable(e, { __index = g31 })
            local fn = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
            G.setfenv(fn, e); fn()
            if sh_i then sh_i() end; if sh_s then sh_s() end
            return e
        end

        local env31 = load31()
        assert(env31.HS_PanelApi.GetSnapshot().hardware_tier == 1, "Test 31.1a: Default hardware_tier must be 1 (Balanced)")
        assert(env31.HS_PanelApi.SetHardwareTier(2) == true, "Test 31.1b: SetHardwareTier(2) failed")
        local sn31 = env31.HS_PanelApi.GetSnapshot()
        assert(sn31.hardware_tier == 2 and sn31.hw_info.tier == 2 and sn31.hw_info.cores == 16 and sn31.hw_info.vb_max_mb == 256,
            "Test 31.1c: Snapshot hardware_tier/hw_info mismatch after SetHardwareTier(2)")
        assert(type(store31["dst_smooth_highfps_cfg"]) == "string" and string.find(store31["dst_smooth_highfps_cfg"], ";ht=2;", 1, true) ~= nil,
            "Test 31.1d: ;ht=2; not persisted in dst_smooth_highfps_cfg!")

        -- Reload modmain -> restores tier 2 from ;ht=2; and pushes to native DLL!
        native_tier_calls = {}
        local env31_reloaded = load31()
        assert(env31_reloaded.HS_PanelApi.GetSnapshot().hardware_tier == 2 and native_tier_calls[#native_tier_calls] == 2,
            "Test 31.2: Reloaded modmain did not restore persisted hardware_tier=2!")

        -- Console c_hstier switching & inspection
        assert(g31.c_hstier(0) == true and env31_reloaded.HS_PanelApi.GetSnapshot().hardware_tier == 0, "Test 31.3a: c_hstier(0) failed")
        assert(g31.c_hstier("unleashed") == true and env31_reloaded.HS_PanelApi.GetSnapshot().hardware_tier == 2, "Test 31.3b: c_hstier('unleashed') failed")
        assert(g31.c_hstier() == 2, "Test 31.3c: c_hstier() inspection return mismatch")

        -- Live Mod Culprit Profiler (StartModProfiler + top_mods attribution)
        sim_clock31 = 10.0
        assert(env31_reloaded.HS_PanelApi.StartModProfiler(1) == true and type(captured_hook31) == "function",
            "Test 31.4a: StartModProfiler did not arm debug.sethook!")
        assert(env31_reloaded.HS_PanelApi.GetModProfilerState().running == true, "Test 31.4b: Profiler must report running=true")
        for _ = 1, 5 do captured_hook31() end
        sim_clock31 = 11.2
        captured_hook31() -- triggers FinishProf() cleanly!
        local ps = env31_reloaded.HS_PanelApi.GetModProfilerState()
        assert(ps.running == false and ps.total == 5 and type(ps.top_mods) == "table" and #ps.top_mods >= 1,
            "Test 31.4c: Profiler failed to populate top_mods!")
        assert(string.find(ps.top_mods[1].name, "Heavy Test Mod", 1, true) ~= nil and ps.top_mods[1].pct == 100,
            "Test 31.4d: Expected KnownModIndex attribution to 'Heavy Test Mod', got " .. tostring(ps.top_mods[1].name))

        print("[CLIENT_V24_TEST] 31. Tiered Hardware Liberation (Low/Balanced/Unleashed + ;ht= persist + c_hstier) & Live Mod Culprit Profiler PASS")
    end

    -- Test 32: Menu Geometric Non-Overlap, Viewport Overflow Scrolling, Wheel OnControl, Clickability & Simple HW Tier Sync
    do
        local function MW32()
            local w = { visible = true, focus = false, _clickable = true, children = {}, inst = { IsValid = function() return true end } }
            setmetatable(w, { __index = function(self, k)
                if k == "AddChild" then return function(s, c) s.children[#s.children + 1] = c; c._parent = s; return c end end
                if k == "SetClickable" then return function(s, v) s._clickable = (v ~= false) end end
                if k == "SetOnClick" then return function(s, fn) s._onclick = fn end end
                if k == "SetOnDown" then return function(s, fn) s._ondown = fn end end
                if k == "SetText" then return function(s, t) s._txt = t end end
                if k == "SetString" then return function(s, t) s._str = t end end
                if k == "Show" then return function(s) s.visible = true end end
                if k == "Hide" then return function(s) s.visible = false end end
                if k == "SetPosition" then return function(s, x, y, z) s._pos = { x, y, z } end end
                if k == "OnControl" then
                    return function(s, control, down)
                        if not s.focus then return false end
                        for _, c in ipairs(s.children or {}) do
                            if c.focus and c:OnControl(control, down) then return true end
                        end
                        return false
                    end
                end
                if k == "GetWorldPosition" then
                    return function(s)
                        local x, y, n = 0, 0, s
                        while n do
                            local np = G.rawget(n, "_pos")
                            if np then x = x + np[1]; y = y + np[2] end
                            n = G.rawget(n, "_parent")
                        end
                        return { x = x, y = y, z = 0 }
                    end
                end
                return function() end
            end })
            return w
        end
        local hud32 = MW32()
        local g32 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error,
            BODYTEXTFONT = "bodyfont", NUMBERFONT = "numfont",
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, ANCHOR_TOP = 1, SCALEMODE_PROPORTIONAL = 2,
            CONTROL_SCROLLBACK = 41, CONTROL_SCROLLFWD = 42,
            STRINGS = { UI = { OPTIONS = { ENABLED = "启用", DISABLED = "禁用" } } },
            ThePlayer = { HUD = hud32 },
            TheSim = {
                GetPersistentString = function(_, name, cb)
                    if name == "hypersmooth_ui.cfg" then cb(true, "pro") else cb(false, "") end
                end,
                SetPersistentString = function() end,
            },
            TheInput = {
                OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end,
                GetScreenPosition = function() return { x = 0, y = 0, z = 0 } end,
                IsMouseDown = function() return false end,
            },
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_SetSkeletal = function() return true end,
            HyperSmooth_SetVfxSmooth = function() return true end,
            HyperSmooth_SetLowLatency = function() return true end,
            HyperSmooth_SetVisual = function() return true end,
            HyperSmooth_SetHd2d = function() return true end,
            HyperSmooth_SetHardwareTier = function() return true end,
            HyperSmooth_GetFrameStats = function() return 240.0, 228.0, 4.18, 0.03, 0, 30 end,
            require = function(p)
                if p == "widgets/widget" or p == "widgets/image" or p == "widgets/imagebutton" or p == "widgets/text" then
                    return function() return MW32() end
                end
                return {}
            end,
        }
        setmetatable(g32, {
            __newindex = function(_, k, _)
                G.error("strict.lua violation in Test 32: assign to undeclared '" .. tostring(k) .. "'", 2)
            end,
        })
        local env32
        env32 = {
            GLOBAL = g32,
            GetModConfigData = function(k)
                if k == "target_fps" then return 240 end
                if k == "skeletal_tween" or k == "low_latency_input" then return true end
                return false
            end,
            AddClassPostConstruct = function() end, AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end, AddSimPostInit = function() end,
            AddClientModRPCHandler = function() end,
            modimport = function(name)
                if name == "hs_panel" then
                    local fp, ep = G.kleiloadlua("../mods/client_v24_lua_test/target_hs_panel.lua")
                    assert(fp ~= nil, "Test 32 hs_panel.lua load error: " .. tostring(ep))
                    G.setfenv(fp, env32)
                    fp()
                end
            end,
        }
        local f32 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setfenv(f32, env32); f32()
        assert(env32.HS_TogglePanel(true) == true, "Test 32: HS_TogglePanel(true) failed!")
        local UI = g32.HS_PanelDebug
        local api32 = env32.HS_PanelApi
        assert(type(UI) == "table" and type(UI.L) == "table" and type(UI.pages) == "table", "Test 32: UI geometry hooks missing!")

        -- 1) Exhaustive geometric non-overlap check across EVERY Pro tab (2..7) and EVERY section
        local desc_top = UI.L.desc_y + UI.L.desc_h * 0.5
        assert(#UI.pages[4].secs == 6, "Test 32.1a: Tab 4 Light must split section 5 into 6 clean sections, got " .. tostring(#UI.pages[4].secs))
        for tid = 2, 7 do
            local page = UI.pages[tid]
            assert(page ~= nil and #page.secs >= 2, "Test 32.1b: Missing Pro page " .. tostring(tid))
            assert(page.view_bot > desc_top + 1.0,
                string.format("Test 32.1c: Tab %d view_bot (%.1f) overlaps Description Bar top (%.1f)!", tid, page.view_bot, desc_top))
            if page.reset_y then
                local rst_top = page.reset_y + page.reset_h * 0.5
                assert(page.view_bot > rst_top + 1.0,
                    string.format("Test 32.1d: Tab %d view_bot (%.1f) overlaps Reset Button top (%.1f)!", tid, page.view_bot, rst_top))
            end
            for si = 1, #page.secs do
                page.open = si
                page.scroll = 0
                UI.LayoutPage(page)
                assert(page.max_scroll == 0,
                    string.format("Test 32.1e: Tab %d sec %d overflows viewport by %.1fpx!", tid, si, page.max_scroll))
                for s2 = 1, #page.secs do
                    local sec = page.secs[s2]
                    assert(sec.hdr.root.visible == true,
                        string.format("Test 32.1f: Tab %d sec %d header %d unexpectedly hidden", tid, si, s2))
                    assert(sec.hdr._y_top <= page.view_top + 1.0 and sec.hdr._y_bot >= page.view_bot - 1.0,
                        string.format("Test 32.1g: Tab %d sec %d header %d out of bounds [%.1f, %.1f]", tid, si, s2, sec.hdr._y_bot, sec.hdr._y_top))
                    for ri, r in ipairs(sec.rows) do
                        if s2 == si then
                            assert(r.root.visible == true,
                                string.format("Test 32.1h: Tab %d open sec %d row %d hidden!", tid, si, ri))
                            assert(r._y_top <= page.view_top + 1.0 and r._y_bot >= page.view_bot - 1.0,
                                string.format("Test 32.1i: Tab %d open sec %d row %d [%.1f, %.1f] clipped past [%.1f, %.1f]!",
                                    tid, si, ri, r._y_bot, r._y_top, page.view_bot, page.view_top))
                        else
                            assert(r.root.visible == false,
                                string.format("Test 32.1j: Tab %d closed sec %d row %d stayed visible!", tid, s2, ri))
                        end
                    end
                end
            end
            page.open = 1; page.scroll = 0; UI.LayoutPage(page)
        end

        -- 2) Bounded overflow scroll & mouse-wheel OnControl proof
        local over_rows = {}
        for _ = 1, 20 do over_rows[#over_rows + 1] = { id = "sharp" } end
        local p9 = UI.BuildProPage(9, { { zh = "超长分组", en = "Overflow Group", rows = over_rows } }, "重置", "Reset")
        assert(p9.max_scroll > 100 and p9.sb.root.visible == true, "Test 32.2a: 20-row page must activate scrollbar")
        local r_first, r_last = p9.secs[1].rows[1], p9.secs[1].rows[20]
        assert(r_first.root.visible == true and r_last.root.visible == false,
            "Test 32.2b: At scroll=0, row 1 must show and overflowing row 20 must be clipped/hidden (never overlap bottom bars)!")
        for ri, r in ipairs(p9.secs[1].rows) do
            if r.root.visible then
                assert(r._y_bot >= p9.view_bot - 1.0 and r._y_top <= p9.view_top + 1.0,
                    "Test 32.2c: Visible row #" .. tostring(ri) .. " violated viewport bounds!")
            end
        end
        assert(UI.ScrollPage(p9, 99999) == true and math.abs(p9.scroll - p9.max_scroll) < 0.5,
            "Test 32.2d: ScrollPage did not clamp cleanly to max_scroll")
        assert(r_first.root.visible == false and r_last.root.visible == true,
            "Test 32.2e: At bottom scroll, row 1 must clip and row 20 must appear cleanly inside viewport!")
        assert(r_last._y_bot >= p9.view_bot - 1.0 and r_last._y_top <= p9.view_top + 1.0,
            "Test 32.2f: Bottom-scrolled row 20 outside viewport!")

        -- Verify inherited Widget:OnControl click forwarding works alongside wheel handling
        UI.panel.focus = true
        local child_click_fired = 0
        local orig_mode_focus = UI.mode_btns[1].btn.focus
        local orig_mode_oc = G.rawget(UI.mode_btns[1].btn, "OnControl")
        UI.mode_btns[1].btn.focus = true
        UI.mode_btns[1].btn.OnControl = function(s, control, down)
            if down and control == 29 then
                child_click_fired = child_click_fired + 1
                return true
            end
            return false
        end
        assert(UI.panel:OnControl(29, true) == true and child_click_fired == 1,
            "Test 32.2g0: panel:OnControl dropped CONTROL_ACCEPT instead of forwarding to inherited Widget:OnControl!")
        UI.mode_btns[1].btn.OnControl = orig_mode_oc
        UI.mode_btns[1].btn.focus = orig_mode_focus

        -- Wheel OnControl on non-scrollable Tab 2 passes through (false); on scrollable page consumes event (true)
        assert(UI.panel:OnControl(42, true) == false, "Test 32.2g: Non-scrollable tab must not eat wheel zoom")
        local saved_p2 = UI.pages[2]
        UI.pages[2] = p9
        assert(UI.panel:OnControl(41, true) == true and p9.scroll < p9.max_scroll,
            "Test 32.2h: CONTROL_SCROLLBACK must scroll up and consume event!")
        assert(UI.panel:OnControl(42, true) == true,
            "Test 32.2i: CONTROL_SCROLLFWD must scroll down and consume event!")
        UI.pages[2] = saved_p2

        -- 3) Click-swallowing guard proof (decorative chrome cannot eat clicks; main backdrop blocks world clicks)
        assert(UI.desc_bg._clickable == false and UI.desc_accent._clickable == false,
            "Test 32.3a: Description Bar backdrop must have SetClickable(false)")
        assert(UI.panel_bg._clickable ~= false, "Test 32.3b: Main panel backdrop must remain clickable to capture focus")
        for _, tw in ipairs(UI.toggle_widgets or {}) do
            if tw.sw then
                assert(tw.sw.track._clickable == false and tw.sw.knob._clickable == false,
                    "Test 32.3c: Toggle switch pill images must have SetClickable(false)!")
            end
        end

        -- 4) Simple mode Hardware Tier quick strip sync proof
        assert(#UI.hw_groups == 2, "Test 32.4a: Expected 2 synchronized hardware tier strips (Simple + Pro Tab 7), got " .. tostring(#UI.hw_groups))
        api32.SetHardwareTier(0)
        UI.hw_groups[1].btns[2].btn._onclick()
        assert(api32.GetSnapshot().hardware_tier == 2, "Test 32.4b: Simple mode Hardware Tier button failed to switch tier to 2!")

        print("[CLIENT_V24_TEST] 32. Menu Geometric Non-Overlap (Tabs 2..7), Overflow Scroll & Wheel OnControl, Clickability & Simple HW Tier PASS")
    end

    -- Test 33: GPU Clarity Grades (Vanilla/HD/UltraHD/BluRay + ;cg= persist + c_hsclarity + Simple/Pro Tab 2 Panel Sync)
    do
        local store33 = {}
        local clar_calls = {}
        local vis_calls = {}
        local mock_cg = 0
        local function MW33()
            local w = { visible = true, focus = false, _clickable = true, children = {}, inst = { IsValid = function() return true end } }
            setmetatable(w, { __index = function(self, k)
                if k == "AddChild" then return function(s, c) s.children[#s.children + 1] = c; c._parent = s; return c end end
                if k == "SetClickable" then return function(s, v) s._clickable = (v ~= false) end end
                if k == "SetOnClick" then return function(s, fn) s._onclick = fn end end
                if k == "SetOnDown" then return function(s, fn) s._ondown = fn end end
                if k == "SetText" then return function(s, t) s._txt = t end end
                if k == "SetString" then return function(s, t) s._str = t end end
                if k == "Show" then return function(s) s.visible = true end end
                if k == "Hide" then return function(s) s.visible = false end end
                if k == "SetPosition" then return function(s, x, y, z) s._pos = { x, y, z } end end
                return function() end
            end })
            return w
        end
        local g33 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, setmetatable = G.setmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = function() end, error = G.error,
            BODYTEXTFONT = "bodyfont", NUMBERFONT = "numfont",
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, SCALEMODE_PROPORTIONAL = 2,
            STRINGS = { UI = { OPTIONS = { ENABLED = "启用", DISABLED = "禁用" } } },
            ThePlayer = { HUD = MW33() },
            TheSim = {
                GetPersistentString = function(_, fn, cb) cb(store33[fn] ~= nil, store33[fn] or "") end,
                SetPersistentString = function(_, fn, v) store33[fn] = v or "" end,
            },
            TheNet = { IsDedicated = function() return false end },
            TheInput = {
                AddKeyDownHandler = function() end, OnUpdate = function() end,
                IsKeyDown = function() return false end, AddKeyUpHandler = function() end,
                IsMouseDown = function() return false end,
            },
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.9.7-beta1" end,
            HyperSmooth_SetVisual = function(af, flm, bias)
                vis_calls[#vis_calls + 1] = { af = af, flm = flm, bias = bias }
                return true
            end,
            HyperSmooth_SetClarityGrade = function(g, hdr, str)
                mock_cg = g
                clar_calls[#clar_calls + 1] = { g = g, hdr = hdr, str = str }
                return true
            end,
            HyperSmooth_GetClarityStats = function()
                return mock_cg, true, (mock_cg >= 2), (mock_cg == 3 and 0.79 or (mock_cg == 2 and 0.21 or 0.0)), (mock_cg >= 2), (mock_cg == 3 and 4096 or 2048), (mock_cg == 3 and 48.0 or (mock_cg == 2 and 16.0 or 0.0))
            end,
            require = function(m)
                if string.find(m, "^widgets/") then return function() return MW33() end end
                return {}
            end,
        }
        local function load33(with_panel)
            local sh_i, sh_s = nil, nil
            local e
            e = {
                GLOBAL = g33, MODROOT = "../mods/client_v24_lua_test/",
                Asset = function() return {} end,
                GetModConfigData = function(k) if k == "visual_preset" then return 0 end return nil end,
                AddModShadersInit = function(f) sh_i = f end,
                AddModShadersSortAndEnable = function(f) sh_s = f end,
                AddClassPostConstruct = function() end, AddPrefabPostInit = function() end,
                AddPrefabPostInitAny = function() end, AddPlayerPostInit = function() end,
                AddComponentPostInit = function() end, AddSimPostInit = function() end,
                AddGamePostInit = function() end, AddClientModRPCHandler = function() end,
                modimport = function(nm)
                    if with_panel and nm == "hs_panel" then
                        local fp = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_hs_panel.lua"))
                        G.setfenv(fp, e); fp()
                    end
                end,
            }
            G.setmetatable(e, { __index = g33 })
            local fn = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
            G.setfenv(fn, e); fn()
            if sh_i then sh_i() end; if sh_s then sh_s() end
            return e
        end

        local env33 = load33(true)
        assert(env33.HS_PanelApi.GetSnapshot().clarity_grade == 0, "Test 33.1a: Default clarity_grade must be 0")
        assert(env33.HS_PanelApi.SetClarityGrade(3) == true, "Test 33.1b: SetClarityGrade(3) failed")
        local sn33 = env33.HS_PanelApi.GetSnapshot()
        assert(sn33.clarity_grade == 3 and sn33.preset == 1, "Test 33.1c: BluRay grade must promote Vanilla->EyeCareHD(1) & set cg=3")
        assert(clar_calls[#clar_calls].g == 3 and clar_calls[#clar_calls].hdr == true, "Test 33.1d: Native SetClarityGrade did not receive (3, true)")
        assert(math.abs((vis_calls[#vis_calls].bias or 0) - (-0.95)) < 1e-4, "Test 33.1e: BluRay mip bias should be -0.95")
        assert(string.find(store33["dst_smooth_highfps_cfg"] or "", ";cg=3;cs=1.00;", 1, true) ~= nil, "Test 33.1f: ;cg=3;cs=1.00; not persisted!")

        -- Compute strength API & persistence
        assert(env33.HS_PanelApi.SetClarityStrength(1.25) == true, "Test 33.1g: SetClarityStrength(1.25) failed")
        assert(math.abs((clar_calls[#clar_calls].str or 0) - 1.25) < 1e-4, "Test 33.1h: Native SetClarityGrade did not receive str=1.25!")
        assert(string.find(store33["dst_smooth_highfps_cfg"] or "", ";cs=1.25;", 1, true) ~= nil, "Test 33.1i: ;cs=1.25; not persisted!")

        -- Preset 0 cleanly disables GPU Clarity Grade
        env33.HS_PanelApi.SetPreset(0)
        assert(env33.HS_PanelApi.GetSnapshot().clarity_grade == 0 and mock_cg == 0, "Test 33.2: SetPreset(0) must reset clarity_grade to 0")

        -- Console command & reload persistence
        assert(g33.c_hsclarity("uhd", 1.35) == true and g33.c_hsclarity() == 2, "Test 33.3a: c_hsclarity('uhd', 1.35) failed")
        clar_calls = {}
        local env33_rel = load33(true)
        assert(env33_rel.HS_PanelApi.GetSnapshot().clarity_grade == 2 and clar_calls[#clar_calls].g == 2 and
               math.abs((env33_rel.HS_PanelApi.GetSnapshot().clarity_strength or 0) - 1.35) < 1e-4 and
               math.abs((clar_calls[#clar_calls].str or 0) - 1.35) < 1e-4,
            "Test 33.3b: Reloaded modmain did not restore ;cg=2;cs=1.35!")

        -- Panel clarity strips sync, Compute Strength stepper controls & telemetry readout
        assert(env33_rel.HS_TogglePanel(true) == true, "Test 33.4a: HS_TogglePanel failed")
        local ui33 = g33.HS_PanelDebug
        assert(type(ui33.clarity_groups) == "table" and #ui33.clarity_groups == 2,
            "Test 33.4b: Expected 2 synced clarity strips (Simple + Pro Tab 2), got " .. tostring(ui33.clarity_groups and #ui33.clarity_groups))
        assert(ui33.clarity_str_plus ~= nil and ui33.clarity_str_minus ~= nil and ui33.clarity_str_val ~= nil,
            "Test 33.4b2: Missing Pro Tab 2 clarity_str stepper controls!")
        ui33.clarity_str_minus._onclick()
        assert(math.abs((env33_rel.HS_PanelApi.GetSnapshot().clarity_strength or 0) - 1.25) < 1e-4 and ui33.clarity_str_val._txt == "125%",
            "Test 33.4b3: Clicking clarity_str_minus failed to step strength 135%->125%! got " .. tostring(ui33.clarity_str_val._txt))
        ui33.clarity_str_plus._onclick()
        assert(math.abs((env33_rel.HS_PanelApi.GetSnapshot().clarity_strength or 0) - 1.35) < 1e-4 and ui33.clarity_str_val._txt == "135%",
            "Test 33.4b4: Clicking clarity_str_plus failed to step strength 125%->135%!")
        ui33.clarity_groups[1].btns[3].btn._onclick()
        local sn33b = env33_rel.HS_PanelApi.GetSnapshot()
        assert(sn33b.clarity_grade == 3 and sn33b.clarity_stats.hdr_lm == true and sn33b.clarity_stats.shadow_res == 4096,
            "Test 33.4c: Clicking Simple Clarity BluRay button did not sync grade 3 / 16F HDR / 4096 Shadow!")
        assert(type(ui33.clarity_meta._str) == "string" and string.find(ui33.clarity_meta._str, "4096", 1, true) ~= nil,
            "Test 33.4d: Pro Tab 2 clarity_meta telemetry missing '4096' readout: " .. tostring(ui33.clarity_meta._str))
        ui33.clarity_groups[2].btns[1].btn._onclick()
        assert(env33_rel.HS_PanelApi.GetSnapshot().clarity_grade == 1, "Test 33.4e: Pro Tab 2 Clarity button click failed!")

        print("[CLIENT_V24_TEST] 33. GPU Clarity Grades (Vanilla/HD/UltraHD/BluRay + ;cg= persist + c_hsclarity + Panel Sync) PASS")
    end

    -- Test 34: Phase 4.5 TextEdit Focus Release + Hidden OnControl Guard + ActionQueue selection_thread Guard
    do
        local input_onupdate_calls = 0
        local picker_calls = 0
        local mx34, my34 = 400, 300
        local function MW34(is_text_edit)
            local w = {
                visible = true, focus = false, editing = false, force_edit = false,
                _is_te = (is_text_edit == true), _clickable = true,
                children = {}, inst = { IsValid = function() return true end },
            }
            setmetatable(w, { __index = function(self, k)
                if k == "AddChild" then return function(s, c) s.children[#s.children + 1] = c; c._parent = s; return c end end
                if k == "SetClickable" then return function(s, v) s._clickable = (v ~= false) end end
                if k == "SetOnClick" then return function(s, fn) s._onclick = fn end end
                if k == "SetOnDown" then return function(s, fn) s._ondown = fn end end
                if k == "SetText" then return function(s, t) s._txt = t end end
                if k == "SetString" then return function(s, t) s._str = t end end
                if k == "Show" then return function(s) s.visible = true end end
                if k == "Hide" then return function(s) s.visible = false end end
                if k == "SetPosition" then return function(s, x, y, z) s._pos = { x, y, z } end end
                if k == "SetForceEdit" then return function(s, v) s.force_edit = (v == true) end end
                if k == "SetEditing" then return function(s, v) s.editing = (v == true) end end
                if k == "ClearFocus" then
                    return function(s)
                        local function clr(node)
                            node.focus = false
                            for _, ch in ipairs(node.children or {}) do clr(ch) end
                        end
                        clr(s)
                    end
                end
                if k == "OnControl" then
                    return function(s, control, down)
                        if not s.focus then return false end
                        for _, c in ipairs(s.children or {}) do
                            if c.focus and c:OnControl(control, down) then return true end
                        end
                        return false
                    end
                end
                if k == "GetWorldPosition" then
                    return function(s)
                        local x, y, n = 0, 0, s
                        while n do
                            local np = G.rawget(n, "_pos")
                            if np then x = x + np[1]; y = y + np[2] end
                            n = G.rawget(n, "_parent")
                        end
                        return { x = x, y = y, z = 0 }
                    end
                end
                return function() end
            end })
            return w
        end

        local hud34 = MW34(false)
        local fakePlayer34 = {
            HUD = hud34,
            IsValid = function() return true end,
            IsActionsVisible = function() return true end,
            HasTag = function() return false end,
            components = {
                playeractionpicker = {
                    DoGetMouseActions = function()
                        picker_calls = picker_calls + 1
                        return { action = "WALKTO", target = nil }, nil
                    end,
                },
                actionqueuer = nil,
            },
        }
        local fe34 = {
            textProcessorWidget = nil,
            clear_focus_calls = 0,
            SetForceProcessTextInput = function(self, en, w)
                if en then
                    self.textProcessorWidget = w
                elseif self.textProcessorWidget == w then
                    self.textProcessorWidget = nil
                end
            end,
            ClearFocus = function(self)
                self.clear_focus_calls = self.clear_focus_calls + 1
            end,
        }
        local g34 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = function() end, error = G.error,
            BODYTEXTFONT = "bodyfont", NUMBERFONT = "numfont",
            ANCHOR_RIGHT = 2, ANCHOR_MIDDLE = 0, ANCHOR_LEFT = 1, ANCHOR_TOP = 1, SCALEMODE_PROPORTIONAL = 2,
            CONTROL_MOVE_UP = 5, CONTROL_MOVE_DOWN = 6, CONTROL_MOVE_LEFT = 7, CONTROL_MOVE_RIGHT = 8,
            CONTROL_CONTROLLER_ATTACK = 39,
            STRINGS = { UI = { OPTIONS = { ENABLED = "Enabled", DISABLED = "Disabled" } } },
            IsPaused = function() return false end,
            ThePlayer = fakePlayer34,
            TheFrontEnd = fe34,
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                GetPosition = function() return mx34, my34 end,
            },
            TheInput = {
                OnUpdate = function() input_onupdate_calls = input_onupdate_calls + 1 end,
                IsKeyDown = function() return false end, AddKeyUpHandler = function() end,
                GetScreenPosition = function() return { x = mx34, y = my34, z = 0 } end,
                IsMouseDown = function() return false end,
                ControllerAttached = function() return false end,
                GetHUDEntityUnderMouse = function() return nil end,
                GetWorldEntityUnderMouse = function() return nil end,
            },
            HyperSmooth_SetFps = function() return true end,
            HyperSmooth_SetSkeletal = function() return true end,
            HyperSmooth_SetVfxSmooth = function() return true end,
            HyperSmooth_SetLowLatency = function() return true end,
            HyperSmooth_SetVisual = function() return true end,
            HyperSmooth_SetHd2d = function() return true end,
            HyperSmooth_GetFrameStats = function() return 240.0, 232.0, 4.18, 0.02, 0, 30 end,
            require = function(p)
                if p == "widgets/textedit" then
                    return function() return MW34(true) end
                end
                if p == "widgets/widget" or p == "widgets/image" or p == "widgets/imagebutton" or p == "widgets/text" then
                    return function() return MW34(false) end
                end
                return {}
            end,
        }
        setmetatable(g34, {
            __newindex = function(_, k, _)
                G.error("strict.lua violation in Test 34: assign to undeclared '" .. tostring(k) .. "'", 2)
            end,
        })
        local pc_post34 = nil
        local env34
        env34 = {
            GLOBAL = g34,
            GetModConfigData = function(k)
                if k == "target_fps" then return 240 end
                if k == "low_latency_input" then return true end
                return false
            end,
            AddClassPostConstruct = function() end,
            AddComponentPostInit = function(name, fn)
                if name == "playercontroller" then pc_post34 = fn end
            end,
            AddPrefabPostInit = function() end,
            AddSimPostInit = function() end,
            AddClientModRPCHandler = function() end,
            modimport = function(name)
                if name == "hs_panel" then
                    local fp, ep = G.kleiloadlua("../mods/client_v24_lua_test/target_hs_panel.lua")
                    assert(fp ~= nil, "Test 34 hs_panel.lua load error: " .. tostring(ep))
                    G.setfenv(fp, env34)
                    fp()
                end
            end,
        }
        local f34 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        G.setfenv(f34, env34); f34()

        -- 34.1: Open panel & ShareCard -> verify TextEdit children have force_edit==true; closing ShareCard releases focus
        assert(env34.HS_TogglePanel(true) == true, "Test 34.1a: HS_TogglePanel(true) failed!")
        local ui34 = g34.HS_PanelDebug
        assert(type(ui34) == "table" and type(ui34.ToggleShareCard) == "function", "Test 34.1b: ToggleShareCard missing!")
        ui34.ToggleShareCard()

        local found_eds = {}
        local function find_eds34(node)
            if node._is_te then found_eds[#found_eds + 1] = node end
            for _, ch in ipairs(node.children or {}) do find_eds34(ch) end
        end
        find_eds34(ui34.panel)
        assert(#found_eds >= 2, "Test 34.1c: Expected >=2 TextEdit children under panel, got " .. tostring(#found_eds))
        for i, ed in ipairs(found_eds) do
            assert(ed.force_edit == true, "Test 34.1d: TextEdit #" .. tostring(i) .. " missing SetForceEdit(true)!")
        end

        local ed1 = found_eds[1]
        local sc_card = ed1._parent
        ed1.editing = true
        ed1.focus = true
        fe34.textProcessorWidget = ed1
        ui34.panel.focus = true

        ui34.ToggleShareCard()
        assert(ed1.editing == false, "Test 34.1e: Closing ShareCard left TextEdit.editing == true!")
        assert(ed1.focus == false, "Test 34.1f: Closing ShareCard left TextEdit.focus == true!")
        assert(fe34.textProcessorWidget == nil, "Test 34.1g: Closing ShareCard left TheFrontEnd.textProcessorWidget set!")
        assert(fe34.clear_focus_calls >= 1, "Test 34.1h: Closing ShareCard did not call TheFrontEnd:ClearFocus()!")
        assert(sc_card ~= nil and sc_card:OnControl(29, true) == false, "Test 34.1i: Hidden ShareCard OnControl must return false!")

        -- 34.2: Re-open ShareCard, mark editing/focus active, close entire panel -> focus released & panel:OnControl returns false
        ui34.ToggleShareCard()
        ed1.editing = true
        ed1.focus = true
        fe34.textProcessorWidget = ed1
        ui34.panel.focus = true
        assert(env34.HS_TogglePanel(false) == false, "Test 34.2a: HS_TogglePanel(false) did not hide panel!")
        assert(ed1.editing == false, "Test 34.2b: Closing panel left TextEdit.editing == true!")
        assert(ed1.focus == false, "Test 34.2c: Closing panel left TextEdit.focus == true!")
        assert(fe34.textProcessorWidget == nil, "Test 34.2d: Closing panel left TheFrontEnd.textProcessorWidget set!")
        -- Even if a caller forces focus=true on hidden panel/child, panel:OnControl must return false while hidden!
        ui34.panel.focus = true
        ed1.focus = true
        ed1.OnControl = function() return true end
        assert(ui34.panel:OnControl(29, true) == false, "Test 34.2e: Hidden panel:OnControl must return false!")
        ui34.panel.focus = false
        ed1.focus = false

        -- 34.3: ActionQueue selection_thread guard in RefreshFreshClickActions (modmain.lua)
        assert(type(pc_post34) == "function", "Test 34.3a: playercontroller postinit missing!")
        local pc34 = {
            inst = fakePlayer34, ismastersim = false, handler = {},
            UsingMouse = function() return true end,
            IsEnabled = function() return true end,
            IsBusy = function() return false end,
            IsLocalOrRemoteHopping = function() return false end,
            IsAOETargeting = function() return false end,
            EncodeControlMods = function() return 0 end,
            OnUpdate = function() end,
            OnLeftClick = function() end,
            OnRightClick = function() end,
            OnControl = function() end,
        }
        pc_post34(pc34)
        pc34:OnUpdate(1/30)
        mx34, my34 = 520, 340
        fakePlayer34.components.actionqueuer = { selection_thread = true }
        pc34:OnLeftClick(true)
        pc34:OnRightClick(true)
        assert(input_onupdate_calls == 0 and picker_calls == 0,
            string.format("Test 34.3b: selection_thread=true must block RefreshFreshClickActions / TheInput:OnUpdate (got upd=%d pick=%d)",
                input_onupdate_calls, picker_calls))
        fakePlayer34.components.actionqueuer.selection_thread = nil
        pc34:OnLeftClick(true)
        assert(input_onupdate_calls == 1 and picker_calls == 1,
            string.format("Test 34.3c: Expected RefreshFreshClickActions to run once selection_thread cleared (got upd=%d pick=%d)",
                input_onupdate_calls, picker_calls))

        print("[CLIENT_V24_TEST] 34. Phase 4.5 TextEdit Focus Release + Hidden OnControl Guard + ActionQueue selection_thread Guard PASS")
    end

    -- =========================================================================
    -- 35. Phase 5: hs_vollight Tile Probe Caps + SetForceSinglePass + distancefade Snap/Memo
    -- =========================================================================
    do
        -- (a) hs_vollight.lua: fog <= 12 tile probes/step, single-pass lightrays, 3-phase canopy <= 24 probes/step, Shutdown reset
        local tile_calls35 = 0
        local single_pass_count35 = 0
        local ray_spawned35 = 0
        local cur_tile35 = 7 -- FOREST
        local px35 = 100.0
        local fog_emitter35 = {
            SetRenderResources = function() end, SetMaxNumParticles = function() end,
            SetMaxLifetime = function() end, SetColourEnvelope = function() end,
            SetScaleEnvelope = function() end, SetBlendMode = function() end,
            SetUVFrameSize = function() end, SetFollowEmitter = function() end,
            SetSpawnVectors = function() end, SetSortOrder = function() end,
            SetRadius = function() end, ClearAllParticles = function() end,
            AddParticle = function() end,
        }
        local fake_sr35 = {
            SetShadeEnabled = function() end, SetShadeStrength = function() end,
            SpawnShade = function() return 1 end, RemoveShade = function() end,
            SetShadeTexture = function() end,
        }
        local function MakeEnt35()
            local e = {
                _valid = true,
                persists = true,
                IsValid = function(self) return self._valid end,
                Remove = function(self) self._valid = false end,
                Hide = function() end,
                Show = function() end,
                AddTag = function() end,
                DoPeriodicTask = function() end,
            }
            e.entity = {
                SetCanSleep = function() end,
                AddTransform = function()
                    e.Transform = { SetEightFaced = function() end, SetPosition = function() end, SetRotation = function() end }
                end,
                AddAnimState = function()
                    ray_spawned35 = ray_spawned35 + 1
                    e.AnimState = {
                        SetBank = function() end, SetBuild = function() end, PlayAnimation = function() end,
                        SetLightOverride = function() end, SetFinalOffset = function() end,
                        SetForceSinglePass = function(_, b) if b == true then single_pass_count35 = single_pass_count35 + 1 end end,
                        SetScale = function() end, Hide = function() end, Show = function() end,
                        OverrideMultColour = function() end,
                    }
                end,
                AddVFXEffect = function() e.VFXEffect = fog_emitter35 return fog_emitter35 end,
                CallPrefabConstructionComplete = function() end,
            }
            return e
        end
        local g35 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next, unpack = G.unpack,
            math = G.math, string = G.string, table = G.table, os = G.os,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error,
            FRAMES = 1.0 / 30.0,
            BLENDMODE = { Premultiplied = 2, Additive = 1 },
            TheNet = { IsDedicated = function() return false end },
            ThePlayer = {
                IsValid = function() return true end,
                Transform = { GetWorldPosition = function() return px35, 0.0, 50.0 end },
            },
            TheWorld = {
                state = { isday = true, isdusk = false, isnight = false, isfullmoon = false, israining = false },
                HasTag = function() return false end,
                Map = {
                    GetTileAtPoint = function()
                        tile_calls35 = tile_calls35 + 1
                        return cur_tile35
                    end,
                    IsVisualGroundAtPoint = function() return cur_tile35 ~= 1 and cur_tile35 ~= 255 end,
                },
            },
            TheSim = { FindEntities = function() return {} end },
            ShadeRenderer = fake_sr35,
            ShadeTypes = {},
            WORLD_TILES = { IMPASSABLE = 1, INVALID = 255, FOREST = 7, MARSH = 8, DECIDUOUS = 30, FUNGUS = 14, GRASS = 6 },
            EnvelopeManager = { AddColourEnvelope = function() end, AddVector2Envelope = function() end },
            CreateEntity = MakeEnt35,
            resolvefilepath = function(p) return p end,
        }
        local env35 = { GLOBAL = g35, AddPlayerPostInit = function() end }
        local fl35 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_hs_vollight.lua"))
        setfenv(fl35, env35); fl35()
        local VL35 = env35.HS_VOLLIGHT

        -- 1) Fog-only probe cap check: edge_fog=1.0, ground_fog=1.0 on MARSH -> <= 12 tile probes per emitter (<= 24 combined, <= 12 each)
        cur_tile35 = 8 -- MARSH
        VL35.ApplyConfig(0.0, 0.0, 0.0, 1.0, 0.0, 0)
        for _ = 1, 10 do
            tile_calls35 = 0
            VL35.StepUpdate(0.25)
            assert(tile_calls35 <= 12,
                string.format("Test 35.1a: StepVolumetricFog edge_fog exceeded 12 tile probes/step (got %d)", tile_calls35))
        end
        VL35.ApplyConfig(0.0, 0.0, 0.0, 0.0, 1.0, 0)
        for _ = 1, 10 do
            tile_calls35 = 0
            VL35.StepUpdate(0.25)
            assert(tile_calls35 <= 12,
                string.format("Test 35.1b: StepVolumetricFog ground_fog exceeded 12 tile probes/step (got %d)", tile_calls35))
        end
        VL35.Shutdown()

        -- 2) SetForceSinglePass(true) + 3-phase canopy <= 24 probes per step after initial phase warmup
        cur_tile35 = 7 -- FOREST
        ray_spawned35 = 0
        single_pass_count35 = 0
        VL35.ApplyConfig(1.0, 1.0, 1.0, 0.0, 0.0, 0)
        -- First step initializes (only_phase=nil); subsequent steps stream staggered phase 0 -> 1 -> 2
        VL35.StepUpdate(0.15)
        assert(ray_spawned35 > 0 and single_pass_count35 == ray_spawned35,
            string.format("Test 35.2a: Expected SetForceSinglePass(true) on all spawned lightrays (got %d/%d)",
                single_pass_count35, ray_spawned35))
        -- Now move across FOREST tiles; phase 0=rays, phase 1=clouds(0 tiles), phase 2=canopy(<=24 tiles)!
        local saw_phase2_canopy_step = false
        for s = 1, 9 do
            px35 = px35 + 12.0
            tile_calls35 = 0
            VL35.StepUpdate(0.20)
            VL35.StepUpdate(0.20)
            -- Note: stream_timer triggers on 2nd 0.20 step (>= 0.35s); phase order 0 (rays), 1 (clouds), 2 (canopy)
            local phase_after_step = s % 3 -- 1->phase0 done, 2->phase1 done, 0->phase2 done
            if phase_after_step == 0 then
                saw_phase2_canopy_step = true
                assert(tile_calls35 <= 24,
                    string.format("Test 35.2b: Staggered canopy phase exceeded 24 tile probes (got %d)", tile_calls35))
            end
        end
        assert(saw_phase2_canopy_step, "Test 35.2c: Did not exercise phase 2 canopy step")
        VL35.Shutdown()

        -- 3) After Shutdown, phase resets cleanly back to 0 (first post-warmup step runs phase 0 rays, next runs phase 1 clouds)
        px35 = 500.0
        VL35.ApplyConfig(0.0, 0.0, 1.0, 0.0, 0.0, 0) -- canopy only
        VL35.StepUpdate(0.15) -- initial only_phase=nil warmup
        px35 = 520.0
        tile_calls35 = 0
        VL35.StepUpdate(0.25) VL35.StepUpdate(0.25) -- stream_phase 0 -> phase 0 (rays only = 0 canopy tile calls)
        assert(tile_calls35 == 0,
            string.format("Test 35.3: Expected Shutdown() to reset stream_phase to 0 (got tile_calls=%d)", tile_calls35))
        VL35.Shutdown()

        -- (b) modmain.lua distancefade:OnWallUpdate alpha >= 0.996 snap + _hs_last_df_a memoization
        local df_post35 = nil
        local cam_y35 = 0.0
        local fakeG35 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error, debug = G.debug,
            ThePlayer = {},
            TheWorld = { ismastersim = false },
            TheFrontEnd = { GetHUDScale = function() return 1.0 end },
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end, SetPersistentString = function() end,
                GetPosition = function() return 0, 0 end,
            },
            TheInput = { OnUpdate = function() end, IsKeyDown = function() return false end, AddKeyUpHandler = function() end },
            TheCamera = {
                currentpos = { Get = function() return 0.0, 0.0, 0.0 end },
                GetDownVec = function() return { x = 0.0, y = 1.0, z = 0.0 } end,
            },
            require = function() return {} end,
        }
        local env35m = {
            GLOBAL = fakeG35,
            GetModConfigData = function() return true end,
            AddSimPostInit = function() end, AddPlayerPostInit = function() end,
            AddClassPostConstruct = function() end, AddGlobalClassPostConstruct = function() end,
            AddComponentPostInit = function(name, fn) if name == "distancefade" then df_post35 = fn end end,
            AddPrefabPostInit = function() end, AddPrefabPostInitAny = function() end,
            AddInputHandler = function() end, AddGamePostInit = function() end,
            modimport = function() end, MODROOT = "../mods/client_v24_lua_test/", modname = "dst_smooth_highfps",
        }
        local fm35 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fm35, env35m); fm35()
        assert(type(df_post35) == "function", "Test 35.4: Missing distancefade postinit in modmain.lua!")

        local mult_calls35 = 0
        local last_mult_a35 = -1.0
        local cmp35 = {
            range = 10.0,
            fadedist = 20.0,
            extrafn = nil,
            inst = {
                IsValid = function() return true end,
                Transform = { GetWorldPosition = function() return 0.0, cam_y35, 0.0 end },
                AnimState = { OverrideMultColour = function(_, _, _, _, a) mult_calls35 = mult_calls35 + 1 last_mult_a35 = a end },
            },
            OnEntitySleep = function() end, OnEntityWake = function() end, StartUpdatingComponent = function() end,
            OnWallUpdate = nil,
        }
        df_post35(cmp35)
        -- 1) Computed alpha slightly < 1.0 (mody = 10 + 0.02 => 1 - (0.02*1.7/20) = 0.9983 >= 0.996) -> MUST snap to exact 1.0!
        cam_y35 = 10.02
        cmp35:OnWallUpdate(1/30)
        assert(mult_calls35 == 1 and last_mult_a35 == 1.0,
            string.format("Test 35.4a: Expected alpha>=0.996 to snap to 1.0 (got calls=%d a=%.4f)", mult_calls35, last_mult_a35))
        -- 2) Another value snapping to 1.0 (e.g. y = 5.0 -> alpha=1.0) MUST be memoized (0 extra OverrideMultColour calls)
        cam_y35 = 5.0
        cmp35:OnWallUpdate(1/30)
        assert(mult_calls35 == 1,
            string.format("Test 35.4b: Expected repeated 1.0 alpha to skip OverrideMultColour (got calls=%d)", mult_calls35))
        -- 3) Partial alpha (e.g. y = 15.0 -> 1 - (5*1.7/20) = 0.575) updates cleanly, small jitter (<0.004) skips
        cam_y35 = 15.0
        cmp35:OnWallUpdate(1/30)
        assert(mult_calls35 == 2 and math.abs(last_mult_a35 - 0.575) < 1e-4,
            string.format("Test 35.4c: Expected partial alpha=0.575 update (got calls=%d a=%.4f)", mult_calls35, last_mult_a35))
        cam_y35 = 15.02 -- delta ~ 0.0017 < 0.004 -> memoized skip!
        cmp35:OnWallUpdate(1/30)
        assert(mult_calls35 == 2,
            string.format("Test 35.4d: Expected micro alpha jitter < 0.004 to skip OverrideMultColour (got calls=%d)", mult_calls35))

        print("[CLIENT_V24_TEST] 35. Phase 5 hs_vollight Tile Probe Caps + SetForceSinglePass + distancefade Snap/Memo PASS")
    end

    -- =========================================================================
    -- TEST 36: Phase 6 TheInput.OnUpdate Smart Camera/Rotate Throttle & Native IME Auto-Suspend/Resume
    -- =========================================================================
    do
        local tick36 = 100
        local t36 = 10.0
        local mx36, my36 = 400, 300
        local cx36, cy36, cz36 = 0.0, 0.0, 0.0
        local hd36, dist36 = 45.0, 30.0
        local btnDown36 = false
        local hovValid36 = true
        local scrName36 = "HUD"
        local updCalls36 = 0
        local imeCalls36 = {}
        local gameCbs36, simCbs36, playerCbs36 = {}, {}, {}
        local tePost36 = nil

        local fe36 = {
            forceProcessText = false,
            textProcessorWidget = nil,
            GetHUDScale = function() return 1.0 end,
            GetActiveScreen = function() return { name = scrName36 } end,
            PushScreen = function(s, scr)
                if scr and scr.name then scrName36 = scr.name end
            end,
            PopScreen = function(s)
                scrName36 = "HUD"
            end,
            SetForceProcessTextInput = function(s, take, widget)
                s.forceProcessText = (take == true)
                s.textProcessorWidget = take and (widget or {}) or nil
            end,
        }
        local cam36 = {
            currentpos = { Get = function() return cx36, cy36, cz36 end },
            GetDownVec = function() return { x = 0.0, y = 1.0, z = 0.0 } end,
            heading = hd36,
            distance = dist36,
        }
        local hovInst36 = { IsValid = function() return hovValid36 end }
        local input36 = {
            mouse_enabled = true,
            entitiesundermouse = {},
            hoverinst = hovInst36,
            OnUpdate = function() updCalls36 = updCalls36 + 1 end,
            IsMouseDown = function() return btnDown36 end,
            IsKeyDown = function() return false end,
            AddKeyUpHandler = function() end,
        }
        local fakeG36
        fakeG36 = {
            pcall = G.pcall, rawget = G.rawget, rawset = G.rawset,
            getmetatable = G.getmetatable, type = G.type, pairs = G.pairs, ipairs = G.ipairs, next = G.next,
            math = G.math, string = G.string, table = G.table,
            tonumber = G.tonumber, tostring = G.tostring, print = G.print, error = G.error, debug = G.debug,
            MOUSEBUTTON_LEFT = 1000, MOUSEBUTTON_RIGHT = 1001, MOUSEBUTTON_MIDDLE = 1002,
            GetTick = function() return tick36 end,
            GetTimeRealSeconds = function() return t36 end,
            ThePlayer = nil,
            TheWorld = nil,
            TheFrontEnd = fe36,
            TheSim = {
                GetPersistentString = function(_, _, cb) cb(false, "") end,
                SetPersistentString = function() end,
                GetPosition = function() return mx36, my36 end,
            },
            TheInput = input36,
            TheCamera = cam36,
            HyperSmooth_SetImeEnabled = function(en)
                imeCalls36[#imeCalls36 + 1] = en
                return true
            end,
            SimReset = function() end,
            DoRestart = function() end,
            Shutdown = function() end,
            require = function() return {} end,
        }
        local env36 = {
            GLOBAL = fakeG36,
            GetModConfigData = function() return true end,
            AddSimPostInit = function(fn) simCbs36[#simCbs36 + 1] = fn end,
            AddPlayerPostInit = function(fn) playerCbs36[#playerCbs36 + 1] = fn end,
            AddClassPostConstruct = function(cls, fn)
                if cls == "widgets/textedit" then tePost36 = fn end
            end,
            AddGlobalClassPostConstruct = function() end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end, AddPrefabPostInitAny = function() end,
            AddInputHandler = function() end,
            AddGamePostInit = function(fn) gameCbs36[#gameCbs36 + 1] = fn end,
            modimport = function() end, MODROOT = "../mods/client_v24_lua_test/", modname = "dst_smooth_highfps",
        }
        local fm36 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fm36, env36); fm36()

        -- 36.1: Lossless TheInput.OnUpdate skips ONLY when completely stationary (<16ms same-tick),
        --       and runs immediately (0ms) on walking, rotating, 1px mouse move, invalid hoverinst, or non-HUD screen
        fakeG36.TheWorld = {}
        local player36 = { DoTaskInTime = function(_, _, fn) if type(fn) == "function" then fn() end end }
        fakeG36.ThePlayer = player36
        for _, fn in ipairs(gameCbs36) do fn() end
        for _, fn in ipairs(simCbs36) do fn() end
        for _, fn in ipairs(playerCbs36) do fn(player36) end

        -- Initial scan runs at 0ms
        input36:OnUpdate()
        assert(updCalls36 == 1, "Test 36.1a: Expected initial OnUpdate scan to run (got " .. tostring(updCalls36) .. ")")

        -- Stationary same-tick < 16ms skips; >= 16ms runs
        t36 = 10.010
        input36:OnUpdate()
        assert(updCalls36 == 1, "Test 36.1b: Stationary <16ms same-tick should skip")
        t36 = 10.020
        input36:OnUpdate()
        assert(updCalls36 == 2, "Test 36.1c: Stationary >=16ms should run")

        -- Walking (camera moving) must run immediately (0ms) even at +4ms!
        cx36 = 0.2
        t36 = 10.024
        input36:OnUpdate()
        assert(updCalls36 == 3, "Test 36.1d: Walking must pass through at 0ms without throttling")

        -- Rotating (camera heading changing) must run immediately (0ms) even at +4ms!
        cam36.heading = cam36.heading + 0.5
        t36 = 10.028
        input36:OnUpdate()
        assert(updCalls36 == 4, "Test 36.1e: Camera rotation must pass through at 0ms without throttling")

        -- 1px mouse move must run immediately (0ms) even at +4ms!
        mx36 = mx36 + 1
        t36 = 10.032
        input36:OnUpdate()
        assert(updCalls36 == 5, "Test 36.1f: 1px mouse move must pass through at 0ms")

        -- Invalid hoverinst must run immediately (0ms) even when stationary at +4ms!
        hovValid36 = false
        t36 = 10.036
        input36:OnUpdate()
        assert(updCalls36 == 6, "Test 36.1g: Invalid hoverinst must pass through at 0ms")
        hovValid36 = true

        -- Non-HUD screen must run immediately (0ms) even when stationary at +4ms!
        scrName36 = "PauseScreen"
        t36 = 10.040
        input36:OnUpdate()
        assert(updCalls36 == 7, "Test 36.1h: Non-HUD screen must pass through at 0ms")
        scrName36 = "HUD"

        -- 36.2: Zero-UX-Compromise IME guarantee: modmain.lua NEVER invokes HyperSmooth_SetImeEnabled
        if type(tePost36) == "function" then
            local ed36 = {
                editing = false,
                inst = { IsValid = function() return true end },
                SetEditing = function(self, en) self.editing = en end,
                Kill = function() end,
            }
            tePost36(ed36)
            ed36:SetEditing(true)
            ed36:SetEditing(false)
            ed36:Kill()
        end
        fe36:PushScreen({ name = "PauseScreen" })
        fe36:PopScreen()
        fe36:SetForceProcessTextInput(true, {})
        fe36:SetForceProcessTextInput(false, nil)
        fakeG36.SimReset()
        assert(#imeCalls36 == 0,
            "Test 36.2: modmain.lua must NOT toggle native IME (got " .. tostring(#imeCalls36) .. " calls)")

        print("[CLIENT_V24_TEST] 36. Phase 6B Lossless TheInput.OnUpdate (0ms Walk/Rotate/1px) + Zero-IME Contract PASS")
    end

    -- =========================================================================
    -- 37. Track 1D (Generic debug.getinfo PostProcessor CC Yield) +
    --     Track 2C (Smart IME TextEdit Tracking & Focus Regain FlushInput)
    -- =========================================================================
    do
        local imeCalls37 = {}
        local flushCalls37 = 0
        local pendingFlush37 = false
        local scrName37 = "HUD"
        local tePost37 = nil
        local gameCbs37 = {}
        local shInit37 = {}
        local shSort37 = {}

        local ppIdx37 = {
            -- Lua closure (non-C) originating outside DST vanilla scripts/ -> generic external mod!
            SetColourCubeData = function() end,
            SetColourCubeLerp = function() end,
            SetDistortionFactor = function() end,
            AddUniformVariable = function(_, n) return "U_" .. tostring(n) end,
            AddSamplerEffect = function(_, p) return "SE_" .. tostring(p) end,
            AddSampler = function() end,
            SetEffectUniformVariables = function() end,
            SetSamplerEffectState = function() end,
            SetSamplerEffectFilter = function() end,
            AddPostProcessEffect = function(_, p) return "PPE_" .. tostring(p) end,
            SetPostProcessEffectAfter = function() end,
            IsBloomEnabled = function() return false end,
            SetBloomEnabled = function() end,
        }
        local fakePp37 = G.setmetatable({}, { __index = ppIdx37 })

        local fe37 = {
            fadedir = nil,
            Update = function() end,
            PushScreen = function(_, scr) if scr and scr.name then scrName37 = scr.name end end,
            PopScreen = function() scrName37 = "HUD" end,
            GetActiveScreen = function() return { name = scrName37 } end,
            GetFocusWidget = function() return nil end,
        }
        local fakeG37 = {
            table = G.table, math = G.math, string = G.string, debug = G.debug,
            ipairs = G.ipairs, pairs = G.pairs, pcall = G.pcall,
            type = G.type, tostring = G.tostring, tonumber = G.tonumber,
            rawget = G.rawget, rawset = G.rawset, getmetatable = G.getmetatable,
            setmetatable = G.setmetatable, next = G.next, print = G.print,
            IsPaused = function() return false end,
            InGamePlay = function() return true end,
            TheFrontEnd = fe37,
            PostProcessor = fakePp37,
            PostProcessorEffects = { Bloom = 1, ColourCube = 2 },
            SamplerEffects = {},
            UniformVariables = { SAMPLER_PARAMS = "SAMPLER_PARAMS" },
            SamplerSizes = { Relative = 1 },
            SamplerColourMode = { RGB = 1 },
            SamplerEffectBase = { BloomSampler = 1, Shader = 2 },
            FILTER_MODE = { LINEAR = 1 },
            MIP_FILTER_MODE = { NONE = 1 },
            WRAP_MODE = { CLAMP_TO_EDGE = 1 },
            TheInput = {
                OnUpdate = function() end,
                GetWorldEntityUnderMouse = function() return nil end,
            },
            TheInputProxy = {
                FlushInput = function() flushCalls37 = flushCalls37 + 1 end,
            },
            HyperSmooth_SetTextInputActive = function(txtActive, inGameplay)
                imeCalls37[#imeCalls37 + 1] = { txt = txtActive, game = inGameplay }
                local ret = pendingFlush37
                pendingFlush37 = false
                return ret
            end,
            SimReset = function() end,
            DoRestart = function() end,
            Shutdown = function() end,
            require = function() return {} end,
        }
        local env37 = {
            GLOBAL = fakeG37,
            GetModConfigData = function() return true end,
            AddSimPostInit = function() end,
            AddPlayerPostInit = function() end,
            AddModShadersInit = function(fn) shInit37[#shInit37 + 1] = fn end,
            AddModShadersSortAndEnable = function(fn) shSort37[#shSort37 + 1] = fn end,
            AddClassPostConstruct = function(cls, fn)
                if cls == "widgets/textedit" then tePost37 = fn end
            end,
            AddGlobalClassPostConstruct = function() end,
            AddComponentPostInit = function() end,
            AddPrefabPostInit = function() end, AddPrefabPostInitAny = function() end,
            AddInputHandler = function() end,
            AddGamePostInit = function(fn) gameCbs37[#gameCbs37 + 1] = fn end,
            modimport = function() end, MODROOT = "../mods/client_v24_lua_test/", modname = "dst_smooth_highfps",
        }
        local fm37 = assert(G.kleiloadlua("../mods/client_v24_lua_test/target_modmain.lua"))
        setfenv(fm37, env37); fm37()
        fakeG37.TheWorld = {}
        for _, fn in ipairs(shInit37) do fn() end
        for _, fn in ipairs(shSort37) do fn() end
        for _, fn in ipairs(gameCbs37) do fn() end

        -- 37.1: Generic debug.getinfo PostProcessor ColourCube yield (not tied to BCAS globals!)
        assert(ppIdx37._hs_bloom_hooked == true,
            "Test 37.1: Expected AddModShadersSortAndEnable path to run through Bloom hook")
        assert(ppIdx37._hs_ccdata_hooked == nil and ppIdx37._hs_cclerp_hooked == nil,
            "Test 37.1: HyperSmooth must yield and NOT double-wrap external non-C PostProcessor CC hook")

        -- 37.2: Smart IME Shield Lua TextEdit + Focus Regain FlushInput sync
        fe37:Update(0.016)
        assert(#imeCalls37 >= 1 and imeCalls37[#imeCalls37].txt == false and imeCalls37[#imeCalls37].game == true,
            "Test 37.2a: Expected gameplay HUD to suppress IME (txt=false, game=true)")

        assert(type(tePost37) == "function", "Test 37.2: Expected widgets/textedit post-construct")
        local ed37 = {
            editing = false,
            IsValid = function() return true end,
            SetEditing = function(w, en) w.editing = en end,
            OnBeginEditing = function(w) w.editing = true end,
            OnStopEditing = function(w) w.editing = false end,
            Kill = function() end,
        }
        tePost37(ed37)
        ed37:SetEditing(true)
        assert(imeCalls37[#imeCalls37].txt == true and imeCalls37[#imeCalls37].game == true,
            "Test 37.2b: Opening TextEdit during gameplay must immediately restore IME (txt=true)")
        ed37:SetEditing(false)
        assert(imeCalls37[#imeCalls37].txt == false,
            "Test 37.2c: Closing TextEdit must restore IME gameplay shield (txt=false)")
        ed37:OnBeginEditing()
        assert(imeCalls37[#imeCalls37].txt == true, "Test 37.2d: OnBeginEditing -> txt=true")
        ed37:Kill()
        assert(imeCalls37[#imeCalls37].txt == false, "Test 37.2e: Kill() active TextEdit -> txt=false")

        fe37:PushScreen({ name = "PauseScreen" })
        assert(imeCalls37[#imeCalls37].game == false, "Test 37.2f: PauseScreen must set inGameplay=false")
        fe37:PopScreen()
        assert(imeCalls37[#imeCalls37].game == true, "Test 37.2g: PopScreen back to HUD -> inGameplay=true")

        pendingFlush37 = true
        fe37:Update(0.016)
        assert(flushCalls37 == 1, "Test 37.2h: Expected single FlushInput on Win32 focus regain (got " .. tostring(flushCalls37) .. ")")
        fe37:Update(0.016)
        assert(flushCalls37 == 1, "Test 37.2i: Focus regain FlushInput must only trigger once")

        print("[CLIENT_V24_TEST] 37. Track 1D (debug.getinfo PostProcessor CC Yield) + Track 2C (Smart IME TextEdit & FlushInput) PASS")
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
    shutil.copy2(os.path.join(CLIENT_MOD, "hs_vfx.lua"), os.path.join(HARNESS_DIR, "target_hs_vfx.lua"))
    shutil.copy2(os.path.join(CLIENT_MOD, "hs_vollight.lua"), os.path.join(HARNESS_DIR, "target_hs_vollight.lua"))
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
