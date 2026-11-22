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

local TARGET_FPS = GetModConfigData("target_fps")
if type(TARGET_FPS) ~= "number" then TARGET_FPS = 240 end
local SKELETAL_TWEEN = GetModConfigData("skeletal_tween")
if SKELETAL_TWEEN == nil then SKELETAL_TWEEN = true end
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
local current_skeletal_tween = SKELETAL_TWEEN and true or false
local current_show_fps_hud = SHOW_FPS_HUD and true or false
local fps_hud_text = nil
local hud_dirty_now = false

local function HasNativePlugin()
    return type(rawget(GLOBAL, "HyperSmooth_SetFps")) == "function"
end

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
    ApplyHudVisibility()
    -- Persist user choices (and modinfo fingerprint mf/ms/mh so explicit modinfo changes win cleanly)
    if save_persist ~= false and TheSim and TheSim.SetPersistentString then
        local payload = string.format("fps=%d;skeletal=%d;hud=%d;mf=%d;ms=%d;mh=%d",
            current_target_fps,
            current_skeletal_tween and 1 or 0,
            current_show_fps_hud and 1 or 0,
            TARGET_FPS,
            SKELETAL_TWEEN and 1 or 0,
            SHOW_FPS_HUD and 1 or 0)
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
            local mf = tonumber(str:match("mf=(%d+)"))
            local ms = tonumber(str:match("ms=(%d+)"))
            local mh = tonumber(str:match("mh=(%d+)"))
            if sf ~= nil and mf == TARGET_FPS then
                current_target_fps = sf
            end
            if ss ~= nil and ms == (SKELETAL_TWEEN and 1 or 0) then
                current_skeletal_tween = (ss ~= 0)
            end
            if sh ~= nil and mh == (SHOW_FPS_HUD and 1 or 0) then
                current_show_fps_hud = (sh ~= 0)
            end
        end
        SyncToNativePlugin(true)
    end)
else
    SyncToNativePlugin(true)
end

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
-- 5. Frame-Rate-Independent Exponential Damping for FollowCamera
--    Calibrated to 60Hz reference so 120/144/240Hz have identical feel + 4x smoothness.
--    Wraps original FollowCamera:Update without replacing it (100% mod-compatible).
--------------------------------------------------------------------------------
if SMOOTH_CAMERA then
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

        self.Update = function(cam, dt, dontupdatepos)
            if dt == nil or dt <= 0 or dt > 0.25 or cam.paused then
                return old_Update(cam, dt, dontupdatepos)
            end

            local orig_pan = cam.pangain
            local orig_heading = cam.headinggain
            local orig_dist = cam.distancegain
            local orig_cdist = cam.continuousdistancegain

            cam.pangain = EffectiveGain(orig_pan, dt)
            cam.headinggain = EffectiveGain(orig_heading, dt)
            cam.distancegain = EffectiveGain(orig_dist, dt)
            cam.continuousdistancegain = EffectiveGain(orig_cdist, dt)

            old_Update(cam, dt, dontupdatepos)

            cam.pangain = orig_pan
            cam.headinggain = orig_heading
            cam.distancegain = orig_dist
            cam.continuousdistancegain = orig_cdist
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

    -- Update official Refresh Rate tooltip to reflect live HyperSmooth v2.4.0 RTSS control
    if self.refreshRateSpinner.parent then
        if is_zh then
            self.refreshRateSpinner.parent.tooltip_text = has_dll
                and "由 HyperSmooth v2.4.0 零争抢 RTSS 引擎实时接管：支持全屏/窗口自由切帧（60~360/无限制）并深度优化 1% Low 稳帧"
                or "需安装配套 bin64/dinput8.dll 原生插件方可突破官方 60 帧上限与启用 RTSS 稳帧（当前仅控制全屏显示器模式）"
        else
            self.refreshRateSpinner.parent.tooltip_text = has_dll
                and "Powered live by HyperSmooth v2.4.0 Zero-Contention RTSS Pacer: 60-360 FPS or Unlimited with 1% Low stabilization."
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

    if self.grid_graphics and self.right_spinners_graphics and self.grid_graphics.num_rows
        and (#self.right_spinners_graphics + 2) <= self.grid_graphics.num_rows then
        local toggle_opts = {
            { text = disabled_str, data = false },
            { text = enabled_str, data = true },
        }
        local tip_txt, tip_div = FindGraphicsTooltipWidgets(self)

        local skel_label = is_zh and "240帧骨骼补帧" or "240fps Skeletal"
        local skel_tip = is_zh
            and (has_dll and "开启全实体/特效/UI 真 240 帧骨骼动画亚刻插值（dinput8.dll 已直连，实时生效）"
                         or "需安装 bin64/dinput8.dll 原生插件方可生效")
            or (has_dll and "True 240fps 2D skeletal animation interpolation across World, Overlays & UI (dinput8.dll active)."
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
        self.grid_graphics:AddItem(self.hcSkeletalSpinner.parent, 2, r1)
        AttachTooltip(self.hcSkeletalSpinner.parent, tip_txt, tip_div)

        local r2 = #self.right_spinners_graphics + 1
        table.insert(self.right_spinners_graphics, self.hcHudSpinner)
        self.grid_graphics:AddItem(self.hcHudSpinner.parent, 2, r2)
        AttachTooltip(self.hcHudSpinner.parent, tip_txt, tip_div)

        self.hcSkeletalSpinner:SetSelectedIndex(self.working.hc_skeletal and 2 or 1)
        self.hcHudSpinner:SetSelectedIndex(self.working.hc_show_hud and 2 or 1)
        BindPendingHighlight(self.hcSkeletalSpinner)
        BindPendingHighlight(self.hcHudSpinner)
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

-- Channel 1: ClientModRPC("HyperCoreProto", "SimTickRate") without duplicating slot id if co-loaded
local function OnSimTickRateRpc(hz)
    if hz == 60 then
        TryActivate60HzSim("ClientModRPC")
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
            if ret == true then
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
            if self.recent_bufferedaction ~= nil and self.recent_bufferedaction.act ~= nil then
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
                        if inst.ClearBufferedAction then inst:ClearBufferedAction() end
                        if self.CooldownRemoteController then self:CooldownRemoteController() end
                        if not self.directwalking then
                            self.time_direct_walking = 0
                            self.directwalking = true
                            self.dragwalking = false
                            self.predictwalking = false
                        end
                        self:RemoteDirectWalking(dir.x, dir.z)
                    end
                end
            elseif self.directwalking and type(self.RemoteStopWalking) == "function" then
                self.directwalking = false
                self.dragwalking = false
                self.predictwalking = false
                if self.CooldownRemoteController then self:CooldownRemoteController() end
                self:RemoteStopWalking()
            end
            return ret
        end
    end)
end

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

    local txt = self:AddChild(Text(GLOBAL.NUMBERFONT, 24, ""))
    txt:SetHAnchor(GLOBAL.ANCHOR_RIGHT)
    txt:SetVAnchor(GLOBAL.ANCHOR_TOP)
    txt:SetRegionSize(860, 32)
    txt:SetHAlign(GLOBAL.ANCHOR_RIGHT)
    txt:SetPosition(-446, -24, 0)
    txt:SetColour(0.55, 1.0, 0.65, 0.92)
    txt:SetClickable(false)
    if not current_show_fps_hud then
        txt:Hide()
    end
    fps_hud_text = txt

    local accum_time = 0
    local accum_frames = 0
    local last_real_fps = 0
    local last_ms = 0
    local old_OnUpdate = self.OnUpdate

    self.OnUpdate = function(hud, dt)
        if old_OnUpdate then
            old_OnUpdate(hud, dt)
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
            if toast_timer <= 0 and not current_show_fps_hud then
                fps_hud_text:Hide()
            end
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
                    local badge = (simhz == 60) and " [60Hz]" or ""
                    metric_str = string.format("%.0f FPS | 1%%L %.0f (%.1fms)%s", avg, low1, p99 or last_ms, badge)
                end
            end
            if not metric_str then
                local badge = (active_sim_tick_hz == 60) and " [60Hz]" or ""
                metric_str = string.format("%.0f FPS (%.1fms)%s", disp_fps, last_ms, badge)
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

            if color_metric >= 95 or (disp_fps >= 110 and color_metric >= disp_fps * 0.72) then
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
end
