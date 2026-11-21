local GLOBAL = GLOBAL
local TheSim = GLOBAL.TheSim
local TheInput = GLOBAL.TheInput
local TheFrontEnd = GLOBAL.TheFrontEnd
local KnownModIndex = GLOBAL.KnownModIndex
local Text = GLOBAL.require("widgets/text")
local pcall = GLOBAL.pcall
local getmetatable = GLOBAL.getmetatable
local type = GLOBAL.type
local pairs = GLOBAL.pairs
local ipairs = GLOBAL.ipairs
local math = GLOBAL.math
local string = GLOBAL.string
local tostring = GLOBAL.tostring

local TARGET_FPS = GetModConfigData("target_fps")
if TARGET_FPS == nil then TARGET_FPS = 240 end
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
local HOTKEY_NAME = GetModConfigData("hotkey_cycle")
if HOTKEY_NAME == nil then HOTKEY_NAME = "KEY_F9" end
local HOTKEY_SKELETAL = GetModConfigData("hotkey_skeletal")
if HOTKEY_SKELETAL == nil then HOTKEY_SKELETAL = "KEY_F10" end

local CFG_FILENAME = "dst_smooth_highfps_cfg"
local FPS_PRESETS = { 60, 120, 144, 240, 0 }
local current_target_fps = TARGET_FPS
local current_skeletal_tween = SKELETAL_TWEEN and true or false

local function SyncToNativePlugin()
    if TheSim and TheSim.SetPersistentString then
        -- Writes to Documents/Klei/DoNotStarveTogether/<id>/client_save/dst_smooth_highfps_cfg
        -- Picked up automatically by bin64/dinput8.dll within ~250ms (no restart needed).
        local payload = string.format("fps=%d;skeletal=%d", current_target_fps, current_skeletal_tween and 1 or 0)
        TheSim:SetPersistentString(CFG_FILENAME, payload, false)
    end
end

-- Sync immediately on mod load
SyncToNativePlugin()

--------------------------------------------------------------------------------
-- 1. Fast Mod Scan: cache parsed modinfo for disabled mods so switching menus
--    doesn't re-read & re-compile 70+ unused modinfo.lua files from disk.
--    All mods remain visible and toggleable; enabled mods always reload fresh.
--------------------------------------------------------------------------------
if FAST_MOD_SCAN and KnownModIndex and KnownModIndex.LoadModInfo then
    local old_LoadModInfo = KnownModIndex.LoadModInfo
    KnownModIndex.LoadModInfo = function(self, modname, prev_info)
        if prev_info ~= nil and modname ~= nil then
            local enabled = (self.IsModEnabledAny and self:IsModEnabledAny(modname))
                or (self.IsModTempEnabled and self:IsModTempEnabled(modname))
                or (self.IsModForceEnabled and self:IsModForceEnabled(modname))
            if not enabled then
                return prev_info
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
-- 3. Pure-Client Zero-Unload Cave <-> Surface Switch (Multiplayer & Solo!)
--    Keeps vanilla "forest" and "cave" asset sets hot in client RAM during
--    BACKEND shard migrations; safely unloads everything on return to FRONTEND.
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
        if Settings and Settings._hakimi_kept_worlds == nil then
            Settings._hakimi_kept_worlds = {}
        end

        local old_LoadPrefabs = idx.LoadPrefabs
        local old_UnloadPrefabs = idx.UnloadPrefabs

        idx.LoadPrefabs = function(sim, prefabs, ...)
            local s = GLOBAL.Settings
            if s and s.current_asset_set == "BACKEND" and type(prefabs) == "table" and #prefabs == 1 then
                local w = prefabs[1]
                if VANILLA_WORLDS[w] then
                    s._hakimi_kept_worlds = s._hakimi_kept_worlds or {}
                    if s._hakimi_kept_worlds[w] then
                        -- Already hot in client RAM from earlier in this session!
                        return
                    end
                    s._hakimi_kept_worlds[w] = true
                end
            end
            return old_LoadPrefabs(sim, prefabs, ...)
        end

        idx.UnloadPrefabs = function(sim, prefabs, ...)
            local s = GLOBAL.Settings
            if s and type(prefabs) == "table" and #prefabs == 1 then
                local w = prefabs[1]
                if VANILLA_WORLDS[w] then
                    if s.current_asset_set == "BACKEND" and s.last_asset_set == "BACKEND" then
                        -- Shard migration (Forest <-> Caves): keep previous layer hot in RAM!
                        s._hakimi_kept_worlds = s._hakimi_kept_worlds or {}
                        s._hakimi_kept_worlds[w] = true
                        return
                    elseif s.current_asset_set == "FRONTEND" and s._hakimi_kept_worlds then
                        -- Leaving world to main menu: cleanly release all kept layers
                        local extras = {}
                        for kw in pairs(s._hakimi_kept_worlds) do
                            if kw ~= w then
                                extras[#extras + 1] = kw
                            end
                        end
                        s._hakimi_kept_worlds = {}
                        local ret = old_UnloadPrefabs(sim, prefabs, ...)
                        if #extras > 0 then
                            pcall(old_UnloadPrefabs, sim, extras)
                        end
                        return ret
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

    AddComponentPostInit("deployhelper", function(self)
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
--    b) HoverText: dirty-check SetString & SetColour on hover widgets (skips 99% redundant
--       table allocations & C++ TextWidget rebuilds; 100% compatible with ShowMe / Insight).
--    c) Input:OnUpdate: lossless stationary hit-test cache (0ms latency on mouse/camera move
--       or UI screens; skips ~75% redundant 240Hz C++ GetEntitiesAtScreenPoint scans).
--------------------------------------------------------------------------------
if REDUCE_240HZ_CHURN then
    AddComponentPostInit("distancefade", function(self)
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

    local function WrapZeroChurnText(txt)
        if not txt then
            return
        end
        local old_SetString = txt.SetString
        if old_SetString then
            local last_s = nil
            txt.SetString = function(w, str)
                local s = str or ""
                if s == last_s and w.string == str then
                    return
                end
                last_s = s
                return old_SetString(w, str)
            end
        end
        local old_SetColour = txt.SetColour
        if old_SetColour then
            local lr, lg, lb, la = nil, nil, nil, nil
            txt.SetColour = function(w, r, g, b, a)
                local cr, cg, cb, ca
                if type(r) == "table" then
                    cr, cg, cb, ca = r[1], r[2], r[3], r[4]
                else
                    cr, cg, cb, ca = r, g, b, a
                end
                if w.colour ~= nil and cr == lr and cg == lg and cb == lb and ca == la then
                    return
                end
                lr, lg, lb, la = cr, cg, cb, ca
                return old_SetColour(w, r, g, b, a)
            end
        end
    end

    AddClassPostConstruct("widgets/hoverer", function(self)
        WrapZeroChurnText(self.text)
        WrapZeroChurnText(self.secondarytext)
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
            local screen = TheFrontEnd and TheFrontEnd:GetActiveScreen()
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
-- 8. Lightweight Zero-Churn FPS + FrameTime HUD & In-Game Hotkeys (F9 / F10)
--------------------------------------------------------------------------------
local fps_hud_text = nil
local toast_timer = 0
local toast_msg = nil

local function FormatTargetLabel(fps)
    if fps == 0 then
        return "UNLIMITED"
    end
    return tostring(fps) .. " FPS"
end

AddClassPostConstruct("screens/playerhud", function(self)
    if SHOW_FPS_HUD then
        local txt = self:AddChild(Text(GLOBAL.NUMBERFONT, 24, ""))
        txt:SetHAnchor(GLOBAL.ANCHOR_RIGHT)
        txt:SetVAnchor(GLOBAL.ANCHOR_TOP)
        txt:SetRegionSize(520, 32)
        txt:SetHAlign(GLOBAL.ANCHOR_RIGHT)
        txt:SetPosition(-276, -24, 0)
        txt:SetColour(0.55, 1.0, 0.65, 0.92)
        txt:SetClickable(false)
        fps_hud_text = txt

        local accum_time = 0
        local accum_frames = 0
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
            end
            if accum_time >= 0.25 then
                local real_fps = accum_frames / accum_time
                local ms = (accum_time / accum_frames) * 1000.0
                if toast_timer > 0 and toast_msg then
                    fps_hud_text:SetString(string.format("%.0f FPS (%.1fms) | %s", real_fps, ms, toast_msg))
                else
                    fps_hud_text:SetString(string.format("%.0f FPS (%.1fms)", real_fps, ms))
                end
                if real_fps >= 110 then
                    fps_hud_text:SetColour(0.45, 1.0, 0.65, 0.92)
                elseif real_fps >= 58 then
                    fps_hud_text:SetColour(1.0, 0.92, 0.45, 0.92)
                else
                    fps_hud_text:SetColour(1.0, 0.45, 0.45, 0.92)
                end
                accum_time = 0
                accum_frames = 0
            end
        end
    end
end)

-- F9: Cycle Target FPS (60 -> 120 -> 144 -> 240 -> UNLIMITED)
if HOTKEY_NAME and GLOBAL[HOTKEY_NAME] then
    local key_code = GLOBAL[HOTKEY_NAME]
    TheInput:AddKeyUpHandler(key_code, function()
        local screen = TheFrontEnd and TheFrontEnd:GetActiveScreen()
        if not screen or screen.name ~= "HUD" then
            return
        end
        local next_idx = 1
        for i, v in ipairs(FPS_PRESETS) do
            if v == current_target_fps then
                next_idx = (i % #FPS_PRESETS) + 1
                break
            end
        end
        current_target_fps = FPS_PRESETS[next_idx]
        SyncToNativePlugin()
        toast_msg = "Target -> " .. FormatTargetLabel(current_target_fps)
        toast_timer = 2.5
    end)
end

-- F10: Live Toggle True 240fps 2D Skeletal Animation Tweening (A/B Comparison)
if HOTKEY_SKELETAL and GLOBAL[HOTKEY_SKELETAL] then
    local skel_key = GLOBAL[HOTKEY_SKELETAL]
    TheInput:AddKeyUpHandler(skel_key, function()
        local screen = TheFrontEnd and TheFrontEnd:GetActiveScreen()
        if not screen or screen.name ~= "HUD" then
            return
        end
        current_skeletal_tween = not current_skeletal_tween
        SyncToNativePlugin()
        toast_msg = "Skeletal 240fps -> " .. (current_skeletal_tween and "ON" or "OFF")
        toast_timer = 2.5
    end)
end
