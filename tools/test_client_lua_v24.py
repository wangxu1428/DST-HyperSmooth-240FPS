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
CLUSTER_DIR = os.path.join(STORAGE, "DoNotStarveTogether", "BenchCluster")

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
    assert(bare_env.version == "2.4.1", "Expected modinfo version 2.4.1, got " .. tostring(bare_env.version))
    assert(bare_env.client_only_mod == true and bare_env.all_clients_require_mod == false, "Wrong mod flags")
    local cfg_map = {}
    for i = 1, #bare_env.configuration_options do
        local opt = bare_env.configuration_options[i]
        cfg_map[opt.name] = opt
    end
    assert(cfg_map.show_1pct_low ~= nil and cfg_map.show_1pct_low.default == true, "Missing show_1pct_low")
    assert(cfg_map.auto_60hz_sim ~= nil and cfg_map.auto_60hz_sim.default == true, "Missing auto_60hz_sim")
    assert(cfg_map.hotkey_cycle ~= nil and cfg_map.hotkey_cycle.default == "KEY_DISABLED", "Missing hotkey_cycle")
    assert(cfg_map.hotkey_skeletal ~= nil and cfg_map.hotkey_skeletal.default == "KEY_DISABLED", "Missing hotkey_skeletal")
    assert(cfg_map.hotkey_hud ~= nil and cfg_map.hotkey_hud.default == "KEY_DISABLED", "Missing hotkey_hud")
    assert(bare_env.keys ~= nil and #bare_env.keys >= 90, "Expected >=90 bindable keys, got " .. tostring(bare_env.keys and #bare_env.keys))
    local extra_ok = { KEY_DISABLED = true, KEY_QUOTE = true, KEY_BACKQUOTE = true, ["\238\132\130"] = true, ["\238\132\131"] = true, ["\238\132\132"] = true }
    for i = 1, #bare_env.keys do
        local kname = bare_env.keys[i].data
        assert(extra_ok[kname] or G[kname] ~= nil, "Invalid Klei KEY constant in modinfo.lua: " .. tostring(kname))
    end
    print("[CLIENT_V24_TEST] 1. Bare-sandbox modinfo.lua PASS (v2.4.1, " .. tostring(#bare_env.configuration_options) .. " opts, " .. tostring(#bare_env.keys) .. " valid Klei KEY/Mouse bindings)")

    -- =========================================================================
    -- TEST 2: modmain.lua Full Behavioral Suite (OptionsScreen + 1% Low HUD + Route B)
    -- =========================================================================
    local function RunModMainScenario(cfg_overrides, expect_60hz, trigger_mode)
        local hooks = { class = {}, comp = {}, prefab = {}, sim = {}, rpc = {} }
        local native_fps = 240
        local native_skel = true
        local native_sim_hz = 30
        local mock_tick = 120
        local mock_rpc_table = {}
        local mock_rpc_handlers = {}

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
            HyperSmooth_SetFps = function(v) native_fps = v; return true end,
            HyperSmooth_SetSkeletal = function(v) native_skel = v; return true end,
            HyperSmooth_GetFrameStats = function() return 239.8, 226.4, 4.21, 0.04, 15, native_sim_hz end,
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
            GetModConfigData = function(k) return cfg_overrides[k] end,
            AddClassPostConstruct = function(cls, fn) hooks.class[cls] = fn end,
            AddComponentPostInit = function(c, fn) hooks.comp[c] = fn end,
            AddPrefabPostInit = function(p, fn) hooks.prefab[p] = fn end,
            AddSimPostInit = function(fn) hooks.sim[#hooks.sim + 1] = fn end,
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

        -- Test OptionsScreen native Refresh Rate takeover
        local rr_sp = { options = {}, idx = 1, enabled = false, has_mod = false, parent = {} }
        function rr_sp:SetOptions(o) self.options = o end
        function rr_sp:SetSelectedIndex(i) self.idx = i end
        function rr_sp:GetSelectedIndex() return self.idx end
        function rr_sp:Enable() self.enabled = true end
        function rr_sp:SetHasModification(v) self.has_mod = v end
        local applied_hw_hz = nil
        local scr = {
            refreshRateSpinner = rr_sp,
            options = { refreshrate = 60, fullscreen = true, display = 0, mode_idx = 0 },
            working = { refreshrate = 60, fullscreen = true, display = 0, mode_idx = 0 },
            grid_graphics = { num_rows = 8, items = {}, AddItem = function(g, it, c, r) g.items[r] = it end },
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
        assert(scr.hcSkeletalSpinner ~= nil and scr.hcHudSpinner ~= nil, "Right-column toggles missing")
        assert(#scr.right_spinners_graphics == 7, "Expected exactly 7 right-column spinners")
        -- Pick Unlimited (0) in Fullscreen -> ChangeGraphicsMode MUST pass safe hardware 144Hz (NOT 0!)
        scr.working.refreshrate = 0
        scr:ChangeGraphicsMode()
        assert(applied_hw_hz == 144, "PickSafeHardwareHz failed: got " .. tostring(applied_hw_hz))
        scr:Apply()
        assert(native_fps == 0, "Apply failed to push Unlimited (0) to C++ bridge")

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
                GetModConfigData = function(k)
                    if k == "target_fps" then return 240 end
                    return k == "keep_shard_assets" and true or false
                end,
                AddClassPostConstruct = function() end,
                AddComponentPostInit = function() end,
                AddPrefabPostInit = function() end,
                AddSimPostInit = function() end,
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

    print("[CLIENT_V24_TEST] ALL_PASS")
    G.c_shutdown(false)
end)
"""

def main():
    os.makedirs(HARNESS_DIR, exist_ok=True)
    with open(os.path.join(HARNESS_DIR, "modinfo.lua"), "w", encoding="utf-8") as f:
        f.write(HARNESS_MODINFO)
    with open(os.path.join(HARNESS_DIR, "modmain.lua"), "w", encoding="utf-8") as f:
        f.write(HARNESS_MODMAIN)
    shutil.copy2(os.path.join(CLIENT_MOD, "modinfo.lua"), os.path.join(HARNESS_DIR, "target_modinfo.lua"))
    shutil.copy2(os.path.join(CLIENT_MOD, "modmain.lua"), os.path.join(HARNESS_DIR, "target_modmain.lua"))
    shutil.copy2(os.path.join(CLIENT_MOD, "keybind.lua"), os.path.join(HARNESS_DIR, "target_keybind.lua"))

    # Make sure iphlpapi.dll is absent so pure vanilla Klei Lua 5.1 runs cleanly
    dll_path = os.path.join(BIN64, "iphlpapi.dll")
    if os.path.exists(dll_path):
        os.remove(dll_path)

    with open(os.path.join(CLUSTER_DIR, "Master", "modoverrides.lua"), "w", encoding="utf-8") as f:
        f.write(f'return {{ ["{HARNESS_NAME}"] = {{ enabled = true, configuration_options = {{}} }} }}\n')

    exe = os.path.join(BIN64, "dontstarve_dedicated_server_nullrenderer_x64.exe")
    proc = subprocess.run(
        [exe, "-persistent_storage_root", STORAGE, "-conf_dir", "DoNotStarveTogether",
         "-cluster", "BenchCluster", "-shard", "Master", "-offline"],
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

    shutil.rmtree(HARNESS_DIR, ignore_errors=True)
    assert proc.returncode == 0 and "[CLIENT_V24_TEST] ALL_PASS" in out, f"Lua test failed (rc={proc.returncode})"

if __name__ == "__main__":
    main()
