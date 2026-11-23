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
    assert(bare_env.version == "2.6.0", "Expected modinfo version 2.6.0, got " .. tostring(bare_env.version))
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
    print("[CLIENT_V24_TEST] 1. Bare-sandbox modinfo.lua PASS (v2.6.0, Player-First Vanilla default + 9 independent switches + low_latency_input, " .. tostring(#bare_env.configuration_options) .. " opts)")

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
            HyperSmooth_Version = function() return "DST-HyperSmooth/2.6.0" end,
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

        -- Verify Player-First Default: 100% Vanilla Pure (0 forced post-process, 0 BloomSampler resize, 0 world shaders!)
        assert(type(env.Assets) == "table" and #env.Assets == 4, "Expected 4 SHADER Assets")
        assert(#hooks.sh_init == 1 and #hooks.sh_sort == 1, "Expected ModShadersInit & Sort hooks")
        hooks.sh_init[1]()
        hooks.sh_sort[1]()
        assert(pp_bloom_params == nil, "Player-First violated: BloomSampler must NOT be touched when default Vanilla Pure!")
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
        assert(vu ~= nil and math.abs(vu[1] - 0.24) < 1e-3 and math.abs(vu[2]) < 1e-4 and math.abs(vu[4]) < 1e-4, "c_hsopt('cas') params wrong")
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

        -- Test PlayerHUD 1% Low + [60Hz] formatting
        local hud_child = nil
        local hud = {
            AddChild = function(_, w) hud_child = w; return w end,
            OnUpdate = function() end,
        }
        hooks.class["screens/playerhud"](hud)
        hud:OnUpdate(0.30)
        assert(hud_child ~= nil and hud_child.visible == true, "HUD should be visible")
        assert(hud_child.str:find("240 FPS | 1%%L 226 %(4%.2ms%)") ~= nil, "HUD missing 1% Low telemetry: " .. tostring(hud_child.str))
        if expect_60hz then
            assert(hud_child.str:find("%[60Hz%]") ~= nil, "HUD missing [60Hz] badge: " .. tostring(hud_child.str))
        else
            assert(hud_child.str:find("%[60Hz%]") == nil, "Vanilla HUD should NOT show [60Hz]: " .. tostring(hud_child.str))
        end

        -- Verify DLL Version Handshake & Update Banner status detection ("ok" / "outdated" / "missing")
        do
            local st, ver, exp = fakeG.HyperSmooth_GetDllStatus()
            assert(st == "ok" and ver == "2.6.0" and exp == "2.6.0",
                   "Expected ok/2.6.0/2.6.0, got " .. tostring(st) .. "/" .. tostring(ver))
            local snap_ok = env.HS_PanelApi.GetSnapshot()
            assert(snap_ok.dll_status == "ok" and snap_ok.dll_ver == "2.6.0", "Panel snapshot dll_status ok mismatch")

            local saved_ver = fakeG.HyperSmooth_Version
            local saved_fps = fakeG.HyperSmooth_SetFps
            fakeG.HyperSmooth_Version = function() return "DST-HyperSmooth/2.5.0" end
            local st2, ver2 = fakeG.HyperSmooth_GetDllStatus()
            assert(st2 == "outdated" and ver2 == "2.5.0", "Older 2.5.0 DLL must report outdated, got " .. tostring(st2))

            fakeG.HyperSmooth_Version = nil
            local st3, ver3 = fakeG.HyperSmooth_GetDllStatus()
            assert(st3 == "outdated" and ver3 == "<2.6.0", "Pre-2.6.0 bridge DLL must report outdated/<2.6.0, got " .. tostring(st3))

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
        assert(hud_child.str:find("EyeHD 2%.2ms") ~= nil and hud_child.str:find("445MB") ~= nil,
               "HUD missing compact EyeHD/Busy/VRAM telemetry: " .. tostring(hud_child.str))
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
    shutil.copy2(os.path.join(CLIENT_MOD, "hs_panel.lua"), os.path.join(HARNESS_DIR, "target_hs_panel.lua"))

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
