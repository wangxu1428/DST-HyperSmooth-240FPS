-- =============================================================================
-- DST HyperCore Server v1.2.0 — Layer 2 (Zero-Divergence), Layer 3 & 60Hz SimTick
-- =============================================================================
-- Runs strictly on server master simulation (TheNet:GetIsServer() / IsMasterSim).
-- Safe to install or uninstall at any time: zero save-format lock-in.
-- =============================================================================

local G = GLOBAL
local rawget = G.rawget
local rawset = G.rawset
if not (G.TheNet and G.TheNet:GetIsServer()) then
    return
end

local OPT_FAST_DUMPER       = GetModConfigData("opt_fast_dumper")
local OPT_MERM_RETARGET     = GetModConfigData("opt_merm_retarget")
local OPT_STAGGERED_WAKE    = GetModConfigData("opt_staggered_wake")
local OPT_LAZY_PERISHABLE   = GetModConfigData("opt_lazy_perishable")
local OPT_OFFLINE_AUTOSTACK = GetModConfigData("opt_offline_autostack")
local OPT_WILD_BEEFALO_CAP  = GetModConfigData("opt_wild_beefalo_cap") or 0
local OPT_SIM_60HZ          = GetModConfigData("opt_sim_60hz") == true

-- Internal Lua telemetry counters
local HC_LUA_STATS = {
    merm_retarget_shortcircuits = 0,
    merm_retarget_filtered_calls = 0,
    merm_retarget_mod_preserved = 0,
    staggered_wakes = 0,
    perish_paused_now = 0,
    perish_syncs = 0,
    autostack_merges = 0,
    beefalo_cap_blocks = 0,
    fastdump_hits = 0,
    fastdump_fallbacks = 0,
    sim_tick_rate = 30,
}
rawset(G, "HyperCoreLuaStats", HC_LUA_STATS)

-- Register shared RPC namespace (id=1) without duplicating slot if client mod co-loaded in same VM
-- NOTE: Klei's GetClientModRPC(ns, name) does NOT nil-check CLIENT_MOD_RPC[ns]; inspect table directly!
local _hc_rpc_tbl = rawget(G, "CLIENT_MOD_RPC")
local _hc_rpc_ns = type(_hc_rpc_tbl) == "table" and _hc_rpc_tbl["HyperCoreProto"] or nil
if type(_hc_rpc_ns) ~= "table" or _hc_rpc_ns["SimTickRate"] == nil then
    AddClientModRPCHandler("HyperCoreProto", "SimTickRate", function() end)
end

-- Dynamic GetTime / GetStaticTime wrapper so 60Hz SimTick never reads a stale boot-time upvalue.
-- Supports BOTH Dedicated Server (iphlpapi.dll -> HyperCore_*) AND No-Caves Local Host (dinput8.dll -> HyperSmooth_*).
local _hcGetSimTick = rawget(G, "HyperCore_GetSimTickRate") or rawget(G, "HyperSmooth_GetSimTickRate")
local _hcSetSimTick = rawget(G, "HyperCore_SetSimTickRate") or rawget(G, "HyperSmooth_SetSimTickRate")
local active_sim_hz = (type(_hcGetSimTick) == "function") and _hcGetSimTick() or 30
if OPT_SIM_60HZ and type(_hcSetSimTick) == "function" then
    if _hcSetSimTick(60) then
        active_sim_hz = 60
    end
end
HC_LUA_STATS.sim_tick_rate = active_sim_hz

if active_sim_hz == 60 then
    rawset(G, "GetTime", function()
        return G.TheSim:GetTick() * G.TheSim:GetTickTime()
    end)
    rawset(G, "GetStaticTime", function()
        return G.TheSim:GetStaticTick() * G.TheSim:GetTickTime()
    end)
    -- Replicate "hypercore_sim60" tag on ALL world_network prefabs (surface/caves/events)
    -- AND send ClientModRPC on player join for 100% reliable client auto-negotiation!
    for _, net_prefab in ipairs({ "forest_network", "cave_network", "lavaarena_network", "quagmire_network", "world" }) do
        AddPrefabPostInit(net_prefab, function(inst)
            inst:AddTag("hypercore_sim60")
        end)
    end
    AddSimPostInit(function()
        local w = G.TheWorld
        if w ~= nil and w.ismastersim then
            w:AddTag("hypercore_sim60")
            if w.net ~= nil then
                w.net:AddTag("hypercore_sim60")
            end
            w:ListenForEvent("ms_playerjoined", function(_, player)
                if player ~= nil and player.userid ~= nil then
                    SendModRPCToClient(GetClientModRPC("HyperCoreProto", "SimTickRate"), player.userid, 60)
                end
            end)
        end
    end)
end

local _hcVersion = rawget(G, "HyperCore_Version")
local has_native = (type(_hcVersion) == "function")
print(string.format("[HyperCore Server] Loaded v1.2.1 | NativeDLL=%s | SimTick=%dHz | FastDump=%s MermFix=%s StaggerWake=%s LazyPerish=%s AutoStack=%s WildBeefaloCap=%d",
    has_native and _hcVersion() or "none",
    active_sim_hz,
    tostring(OPT_FAST_DUMPER), tostring(OPT_MERM_RETARGET), tostring(OPT_STAGGERED_WAKE),
    tostring(OPT_LAZY_PERISHABLE), tostring(OPT_OFFLINE_AUTOSTACK), OPT_WILD_BEEFALO_CAP))

-- =============================================================================
-- [L2-1] C++ FastDataDumper Bridge (100% Byte-Compatible Vanilla Save Format)
-- =============================================================================
local _nativeFastDumpFn = rawget(G, "HyperCore_FastDump")
if OPT_FAST_DUMPER and type(_nativeFastDumpFn) == "function" then
    local _origDataDumper = G.DataDumper
    local _nativeFastDump = _nativeFastDumpFn

    G.DataDumper = function(value, varname, fastmode, ident)
        if fastmode and ident == nil then
            local dumped, err = _nativeFastDump(value, varname)
            if dumped ~= nil then
                HC_LUA_STATS.fastdump_hits = HC_LUA_STATS.fastdump_hits + 1
                return dumped
            end
            -- On cycle/node-cap/OOM/corrupt_number or unsupported type (function/userdata/depth>128),
            -- fall back cleanly to vanilla DataDumper for exact vanilla semantics & error diagnostics.
            HC_LUA_STATS.fastdump_fallbacks = HC_LUA_STATS.fastdump_fallbacks + 1
        end
        return _origDataDumper(value, varname, fastmode, ident)
    end
end

-- =============================================================================
-- [L2-2] Surgical Fix for scripts/prefabs/merm.lua:L134-L147 RetargetFn Storm
-- =============================================================================
-- Vanilla bug: RetargetFn calls FindEntity(defenseTarget, radius, FindInvaderFn)
-- with ZERO musttags/canttags, AND only checks HasKingAnywhere() / leader.isplayer
-- INSIDE the per-entity Lua predicate callback! With 80 merms + Merm King or Wurt,
-- vanilla scans every tree/wall/chest/candy in range 80x/sec only to return nil.
if OPT_MERM_RETARGET then
    local MERM_PREFABS = {
        "merm",
        "mermguard",
        "merm_shadow",
        "mermguard_shadow",
        "merm_lunar",
        "mermguard_lunar",
    }
    local MERM_INVADER_MUST_TAGS = { "character" }
    local MERM_INVADER_CANT_TAGS = { "merm", "NPC_contestant", "INLIMBO" }

    local function FastFindInvaderFn(guy, inst)
        local leader_guy = guy.components.follower ~= nil and guy.components.follower:GetLeader() or nil
        if leader_guy ~= nil and leader_guy.components.inventoryitem ~= nil then
            leader_guy = leader_guy.components.inventoryitem:GetGrandOwner()
        end
        return not (leader_guy ~= nil and leader_guy:HasTag("merm") and not guy:HasAnyTag("pig", "wonkey"))
    end

    local function MakeFastMermRetarget(orig_fn)
        return function(inst)
            -- Preserve Mummy Merm or modded non-standard states untouched
            if inst.mummy then
                return orig_fn ~= nil and orig_fn(inst) or nil
            end
            if inst:HasTag("NPC_contestant") then
                HC_LUA_STATS.merm_retarget_shortcircuits = HC_LUA_STATS.merm_retarget_shortcircuits + 1
                return nil
            end
            local world = G.TheWorld
            local mkm = world ~= nil and world.components.mermkingmanager or nil
            if mkm ~= nil and mkm:HasKingAnywhere() then
                HC_LUA_STATS.merm_retarget_shortcircuits = HC_LUA_STATS.merm_retarget_shortcircuits + 1
                return nil
            end
            local leader = inst.components.follower ~= nil and inst.components.follower:GetLeader() or nil
            if leader ~= nil and leader.isplayer then
                HC_LUA_STATS.merm_retarget_shortcircuits = HC_LUA_STATS.merm_retarget_shortcircuits + 1
                return nil
            end

            local defend_dist = (inst:HasTag("mermguard") and G.TUNING.MERM_GUARD_DEFEND_DIST) or G.TUNING.MERM_DEFEND_DIST
            defend_dist = defend_dist * defend_dist
            local home = inst.components.homeseeker ~= nil and inst.components.homeseeker.home or nil
            local defenseTarget = (home ~= nil and home:IsValid() and inst:GetDistanceSqToInst(home) < defend_dist and home) or inst

            HC_LUA_STATS.merm_retarget_filtered_calls = HC_LUA_STATS.merm_retarget_filtered_calls + 1
            return G.FindEntity(defenseTarget, G.SpringCombatMod(G.TUNING.MERM_TARGET_DIST), FastFindInvaderFn, MERM_INVADER_MUST_TAGS, MERM_INVADER_CANT_TAGS)
        end
    end

    local _dbg_getinfo = (type(G.debug) == "table" and type(G.debug.getinfo) == "function") and G.debug.getinfo or nil
    local function IsVanillaMermRetarget(fn)
        if _dbg_getinfo == nil then
            return true
        end
        local info = _dbg_getinfo(fn, "S")
        local src = info ~= nil and info.source or nil
        if type(src) ~= "string" then
            return true
        end
        -- Match vanilla scripts/prefabs/merm.lua (forward or backslash)
        return src:find("prefabs[/\\]merm%.lua") ~= nil
    end

    for _, prefab in ipairs(MERM_PREFABS) do
        AddPrefabPostInit(prefab, function(inst)
            if not (G.TheWorld and G.TheWorld.ismastersim) then return end
            local combat = inst.components.combat
            if combat ~= nil and combat.targetfn ~= nil and not inst.mummy then
                if IsVanillaMermRetarget(combat.targetfn) then
                    combat.targetfn = MakeFastMermRetarget(combat.targetfn)
                else
                    -- Preserve third-party mod custom targetfn untouched
                    HC_LUA_STATS.merm_retarget_mod_preserved = HC_LUA_STATS.merm_retarget_mod_preserved + 1
                end
            end
        end)
    end
end

-- =============================================================================
-- [L2-3] Staggered Mob Group Wakeup (Eliminates 80-Mob Single-Frame Wake Storm)
-- =============================================================================
if OPT_STAGGERED_WAKE then
    local STAGGER_PREFABS = {
        merm = true, mermguard = true, merm_shadow = true, mermguard_shadow = true,
        merm_lunar = true, mermguard_lunar = true,
        pigman = true, pigguard = true, bunnyman = true, beefalo = true,
    }
    local wake_tick = -1
    local wake_count_this_tick = 0

    for prefab in pairs(STAGGER_PREFABS) do
        AddPrefabPostInit(prefab, function(inst)
            if not (G.TheWorld and G.TheWorld.ismastersim) then return end
            local orig_wake = inst.OnEntityWake
            inst.OnEntityWake = function(self, ...)
                if orig_wake ~= nil then
                    orig_wake(self, ...)
                end
                if G.POPULATING then
                    return
                end
                -- Never delay mobs already in active combat
                if self.components.combat ~= nil and self.components.combat.target ~= nil then
                    return
                end
                local cur_tick = G.GetTick()
                if cur_tick ~= wake_tick then
                    wake_tick = cur_tick
                    wake_count_this_tick = 0
                end
                wake_count_this_tick = wake_count_this_tick + 1
                -- First 6 mobs wake immediately; excess spread smoothly over 1..15 ticks (0.03s..0.50s)
                if wake_count_this_tick > 6 and self.brain ~= nil and G.BrainManager ~= nil then
                    local slot = ((wake_count_this_tick - 7) % 15) + 1
                    G.BrainManager:Sleep(self.brain, slot * G.FRAMES)
                    HC_LUA_STATS.staggered_wakes = HC_LUA_STATS.staggered_wakes + 1
                end
            end
        end)
    end
end

-- =============================================================================
-- [L2-4] Safe-Whitelist Lazy Perishable Integral System
-- =============================================================================
-- Eliminates 10-second DoPeriodicTask churn across thousands of sleeping ground
-- items (Winter's Feast candies!) and closed containers (megabase iceboxes),
-- while preserving 100% vanilla accuracy via continuous world integrals and
-- exact sync on Wake / Open / Drop / Save / GetPercent / Modify.
if OPT_LAZY_PERISHABLE then
    -- Global 4-channel environmental decay integrals updated once every 5s (O(1) total!)
    local EnvIntegral = {
        initialized = false,
        last_t = 0,
        -- Integrated effective decay time (seconds) for each baseline regime:
        normal_ground = 0,     -- dry ground (temperature curve only, * PERISH_GROUND_MULT applied per item)
        wet_ground_extra = 0,  -- extra wet multiplier contribution during rain
        acid_extra_pct = 0,    -- integrated acid-rain percent decay (when caves & acid raining)
        normal_indoor = 0,     -- inside container (temperature curve only, container mult applied per item)
        frozen_mult_int = 0,   -- for items with "frozen" tag (winter_cold = PERISH_COLD_FROZEN_MULT)
    }

    local function StepEnvIntegral()
        local world = G.TheWorld
        if world == nil or world.state == nil then return end
        local now = G.GetTime()
        if not EnvIntegral.initialized then
            EnvIntegral.initialized = true
            EnvIntegral.last_t = now
            return
        end
        local dt = now - EnvIntegral.last_t
        if dt <= 0 then return end
        EnvIntegral.last_t = now

        local st = world.state
        local temp = st.temperature or 20
        local T = G.TUNING

        local temp_mult = 1.0
        local frozen_temp_mult = 1.0
        if temp < 0 then
            temp_mult = T.PERISH_WINTER_MULT or 0.75
            frozen_temp_mult = T.PERISH_COLD_FROZEN_MULT or 0.0
        elseif temp > (T.OVERHEAT_TEMP or 70) then
            temp_mult = T.PERISH_SUMMER_MULT or 1.25
            frozen_temp_mult = temp_mult
        end

        local global_mult = T.PERISH_GLOBAL_MULT or 1.0
        local base_step = dt * temp_mult * global_mult
        local frozen_step = dt * frozen_temp_mult * global_mult

        EnvIntegral.normal_indoor = EnvIntegral.normal_indoor + base_step
        EnvIntegral.frozen_mult_int = EnvIntegral.frozen_mult_int + frozen_step

        -- Ground wetness factor (when raining, ground items get PERISH_WET_MULT)
        local wet_mult = (st.israining and (T.PERISH_WET_MULT or 1.3)) or 1.0
        EnvIntegral.normal_ground = EnvIntegral.normal_ground + base_step * wet_mult

        if st.isacidraining then
            local rate = st.precipitationrate or 0
            EnvIntegral.acid_extra_pct = EnvIntegral.acid_extra_pct + rate * (T.ACIDRAIN_PERISHABLE_ROT_PERCENT or 0) * dt
        end
    end

    AddSimPostInit(function()
        if G.TheWorld ~= nil and G.TheWorld.ismastersim then
            StepEnvIntegral()
            G.TheWorld:DoPeriodicTask(5, StepEnvIntegral)
        end
    end)

    -- Strict safety whitelist: returns false for ANY special perishable item
    local function IsEligibleForLazyPerish(self, inst)
        if inst == nil or not inst:IsValid() then return false end
        -- Skip ALL 17+ special prefabs with custom perishfn or onreplacedfn!
        if self.perishfn ~= nil or self.onreplacedfn ~= nil then return false end
        -- Skip items with frozenfiremult or custom temperature cooling (hot cooked dishes)
        if self.frozenfiremult then return false end
        local edible = inst.components.edible
        if edible ~= nil and edible.temperaturedelta ~= nil and edible.temperaturedelta > 0 then
            return false
        end
        -- Skip light-emitting items (lightbulb, pumpkin_lantern, spore) & living creatures
        if inst.Light ~= nil or inst.components.health ~= nil or inst.components.locomotor ~= nil then
            return false
        end
        -- Skip anything with moisture immunity/custom wetness edge cases if acid rain active
        if inst.components.rainimmunity ~= nil then return false end
        return true
    end

    local function ComputeOwnerMult(inst, owner)
        local T = G.TUNING
        local mult = 1.0
        if owner == nil then
            return T.PERISH_GROUND_MULT or 1.5
        end
        if owner.components.preserver ~= nil then
            mult = owner.components.preserver:GetPerishRateMultiplier(inst) or mult
        elseif owner:HasTag("fridge") then
            if inst:HasTag("frozen") and not owner:HasAnyTag("nocool", "lowcool") then
                mult = T.PERISH_COLD_FROZEN_MULT or 0
            else
                mult = T.PERISH_FRIDGE_MULT or 0.5
            end
        elseif owner:HasTag("foodpreserver") then
            mult = T.PERISH_FOOD_PRESERVER_MULT or 0.75
        elseif owner:HasTag("cage") and inst:HasTag("small_livestock") then
            mult = T.PERISH_CAGE_MULT or 0.5
        end
        if owner:HasTag("spoiler") then
            mult = mult * (T.PERISH_GROUND_MULT or 1.5)
        end
        return mult
    end

    AddComponentPostInit("perishable", function(self, inst)
        if not (G.TheWorld and G.TheWorld.ismastersim) then return end

        -- Pure side-effect-free decay estimator (safe inside GetPercent / OnSave / POPULATING)
        local function ComputeLazyDelta()
            StepEnvIntegral()
            local local_mult = self.localPerishMultiplyer or 1.0
            local owner_mult = self._hc_lazy_owner_mult or 1.0
            local delta_decay = 0
            if not self._hc_lazy_is_indoor then
                local dg = self._hc_lazy_is_frozen
                    and (EnvIntegral.frozen_mult_int - (self._hc_lazy_snap_frozen or EnvIntegral.frozen_mult_int))
                    or  (EnvIntegral.normal_ground - (self._hc_lazy_snap_ground or EnvIntegral.normal_ground))
                delta_decay = dg * owner_mult * local_mult
                local dacid = EnvIntegral.acid_extra_pct - (self._hc_lazy_snap_acid or EnvIntegral.acid_extra_pct)
                if dacid > 0 and self.perishtime ~= nil and self.perishtime > 0 then
                    delta_decay = delta_decay + self.perishtime * dacid
                end
            else
                local di = self._hc_lazy_is_frozen
                    and (EnvIntegral.frozen_mult_int - (self._hc_lazy_snap_frozen or EnvIntegral.frozen_mult_int))
                    or  (EnvIntegral.normal_indoor - (self._hc_lazy_snap_indoor or EnvIntegral.normal_indoor))
                delta_decay = di * owner_mult * local_mult
            end
            return delta_decay
        end

        local function ComputeLazyRemaining()
            if self.perishremainingtime == nil then return nil end
            local delta_decay = ComputeLazyDelta()
            local max_t = self.perishtime or self.perishremainingtime
            return math.min(max_t, self.perishremainingtime - delta_decay)
        end

        local function EnterLazyMode()
            -- CRITICAL SAVE-SAFETY INVARIANT:
            -- NEVER enter lazy mode during PopulateWorld (G.POPULATING == true), because
            -- SpawnSaveRecord calls Transform:SetPosition() -> OnEntitySleep() BEFORE SetPersistData()!
            if G.POPULATING or self._hc_lazy_active or self.updatetask == nil then return end
            if not IsEligibleForLazyPerish(self, inst) then return end
            StepEnvIntegral()
            local owner = inst.components.inventoryitem ~= nil and inst.components.inventoryitem.owner or nil
            -- Never lazy-pause items held by players or inside open containers or pocket dimensions with dynamic handlers
            if owner ~= nil then
                if owner:HasTag("player") or owner.components.inventory ~= nil then return end
                if owner.components.container ~= nil and owner.components.container:IsOpen() then return end
            end
            -- Never lazy-pause items sitting inside a local temperatureoverrider bubble (e.g. scaled furnace / polar light)
            if G.GetTemperatureAtXZ ~= nil and G.TheWorld ~= nil and G.TheWorld.state ~= nil then
                local pos_ent = owner ~= nil and owner or inst
                local px, _, pz = pos_ent.Transform:GetWorldPosition()
                local local_temp = G.GetTemperatureAtXZ(px, pz)
                local world_temp = G.TheWorld.state.temperature or 20
                if local_temp ~= nil and math.abs(local_temp - world_temp) > 0.5 then
                    return
                end
            end

            self.updatetask:Cancel()
            self.updatetask = nil
            self._hc_lazy_active = true
            self._hc_lazy_is_indoor = (owner ~= nil)
            self._hc_lazy_owner_mult = ComputeOwnerMult(inst, owner)
            self._hc_lazy_is_frozen = inst:HasTag("frozen")
            self._hc_lazy_snap_ground = EnvIntegral.normal_ground
            self._hc_lazy_snap_indoor = EnvIntegral.normal_indoor
            self._hc_lazy_snap_frozen = EnvIntegral.frozen_mult_int
            self._hc_lazy_snap_acid   = EnvIntegral.acid_extra_pct
            HC_LUA_STATS.perish_paused_now = HC_LUA_STATS.perish_paused_now + 1
        end

        local function SyncLazyMode(resume_task)
            if not self._hc_lazy_active then return end
            local delta_decay = ComputeLazyDelta()
            self._hc_lazy_active = nil
            HC_LUA_STATS.perish_paused_now = math.max(0, HC_LUA_STATS.perish_paused_now - 1)
            HC_LUA_STATS.perish_syncs = HC_LUA_STATS.perish_syncs + 1

            local expired = false
            if self.perishremainingtime ~= nil and delta_decay ~= 0 then
                local old_val = self.perishremainingtime
                local raw_rem = math.min(self.perishtime or old_val, old_val - delta_decay)
                if raw_rem <= 0 then
                    expired = true
                    self.perishremainingtime = 0
                else
                    self.perishremainingtime = raw_rem
                end
                if math.floor(old_val * 100) ~= math.floor(self.perishremainingtime * 100) then
                    inst:PushEvent("perishchange", { percent = self:GetPercent() })
                end
            elseif self.perishremainingtime ~= nil and self.perishremainingtime <= 0 then
                expired = true
                self.perishremainingtime = 0
            end

            if expired then
                if G.POPULATING then
                    -- Never remove an entity synchronously during PopulateWorld; spoil 1 tick after load
                    self.perishremainingtime = 0.01
                    inst:DoTaskInTime(0, function()
                        if inst:IsValid() and self.perishremainingtime ~= nil and self.perishremainingtime <= 0.01 then
                            self:Perish()
                        end
                    end)
                else
                    self:Perish()
                end
                return
            end

            if resume_task and inst:IsValid() then
                self:StartPerishing()
            end
        end

        self._HC_EnterLazy = EnterLazyMode
        self._HC_SyncLazy  = SyncLazyMode

        -- Patch ALL critical Perishable API entrypoints for 100% behavioral invariance
        local _origIsPerishing = self.IsPerishing
        function self:IsPerishing()
            return self._hc_lazy_active == true or _origIsPerishing(self)
        end

        local _origStartPerishing = self.StartPerishing
        function self:StartPerishing()
            if self._hc_lazy_active then
                self._hc_lazy_active = nil
                HC_LUA_STATS.perish_paused_now = math.max(0, HC_LUA_STATS.perish_paused_now - 1)
            end
            return _origStartPerishing(self)
        end

        local _origStopPerishing = self.StopPerishing
        function self:StopPerishing()
            if self._hc_lazy_active then
                self._hc_lazy_active = nil
                HC_LUA_STATS.perish_paused_now = math.max(0, HC_LUA_STATS.perish_paused_now - 1)
            end
            return _origStopPerishing(self)
        end

        -- 100% PURE READ-ONLY GetPercent(): ZERO state mutation, ZERO Perish(), ZERO inst:Remove()!
        -- Prevents crash when Perishable's `onpercent` setter calls GetPercent() inside OnLoad/SetPersistData,
        -- and keeps ShowMe/Insight/UI queries O(1) & side-effect-free.
        local _origGetPercent = self.GetPercent
        function self:GetPercent()
            if self._hc_lazy_active and self.perishremainingtime ~= nil and self.perishtime ~= nil and self.perishtime > 0 then
                local rem = ComputeLazyRemaining()
                if rem ~= nil then
                    return math.min(1, math.max(0, rem / self.perishtime))
                end
            end
            return _origGetPercent(self)
        end

        for _, mname in ipairs({ "AddTime", "SetPercent", "ReducePercent", "Dilute", "SetLocalMultiplier" }) do
            local _orig = self[mname]
            if _orig ~= nil then
                self[mname] = function(s, ...)
                    if s._hc_lazy_active then
                        SyncLazyMode(true)
                        if not inst:IsValid() then return end
                    end
                    return _orig(s, ...)
                end
            end
        end

        local _origLongUpdate = self.LongUpdate
        function self:LongUpdate(dt)
            if self._hc_lazy_active then
                SyncLazyMode(true)
                if not inst:IsValid() then return end
            end
            return _origLongUpdate(self, dt)
        end

        -- 100% SIDE-EFFECT-FREE OnSave():
        -- NEVER calls Perish() / inst:Remove() / SpawnPrefab() during world serialization,
        -- NEVER writes negative `time`, and NEVER writes `paused=true` for lazy-paused items!
        local _origOnSave = self.OnSave
        function self:OnSave()
            local data = _origOnSave(self)
            if self._hc_lazy_active then
                local rem = ComputeLazyRemaining()
                if data == nil then data = {} end
                data.paused = nil -- ZERO LOCK-IN: never persist lazy pause as vanilla paused=true
                if rem ~= nil and rem <= 0 then
                    data.time = 0.01
                    inst:DoTaskInTime(0, function()
                        if inst:IsValid() and self._hc_lazy_active then
                            SyncLazyMode(true)
                        end
                    end)
                elseif rem ~= nil then
                    data.time = rem
                end
            elseif data ~= nil and data.time ~= nil and data.time < 0 then
                data.time = 0.01
            end
            return data
        end

        -- Defensive OnLoad(): clears any lazy flag and rescues legacy saves containing negative `data.time`
        -- so Perishable:OnLoad -> stackable:OnLoad NEVER crashes on world startup!
        local _origOnLoad = self.OnLoad
        function self:OnLoad(data, ...)
            if self._hc_lazy_active then
                self._hc_lazy_active = nil
                HC_LUA_STATS.perish_paused_now = math.max(0, HC_LUA_STATS.perish_paused_now - 1)
            end
            if data ~= nil and data.time ~= nil and data.time <= 0 then
                data.time = 0.01
            end
            return _origOnLoad(self, data, ...)
        end

        local _origOnRemove = self.OnRemoveEntity
        function self:OnRemoveEntity()
            if self._hc_lazy_active then
                self._hc_lazy_active = nil
                HC_LUA_STATS.perish_paused_now = math.max(0, HC_LUA_STATS.perish_paused_now - 1)
            end
            if _origOnRemove ~= nil then return _origOnRemove(self) end
        end

        -- Hook ground sleep / wake transitions (strictly skipped during PopulateWorld)
        local orig_sleep = inst.OnEntitySleep
        inst.OnEntitySleep = function(ent, ...)
            if orig_sleep ~= nil then orig_sleep(ent, ...) end
            if not G.POPULATING then
                EnterLazyMode()
            end
        end
        local orig_wake = inst.OnEntityWake
        inst.OnEntityWake = function(ent, ...)
            if orig_wake ~= nil then orig_wake(ent, ...) end
            if not G.POPULATING then
                SyncLazyMode(true)
            end
        end

        -- Hook inventory put-in / drop transitions (strictly skipped during PopulateWorld)
        inst:ListenForEvent("ondropped", function()
            if not G.POPULATING then SyncLazyMode(true) end
        end)
        inst:ListenForEvent("onputininventory", function(_, owner)
            if G.POPULATING then return end
            SyncLazyMode(true)
            if not inst:IsValid() then return end
            if owner ~= nil and not owner:HasTag("player") and owner.components.container ~= nil and not owner.components.container:IsOpen() then
                inst:DoTaskInTime(0, function()
                    if inst:IsValid() and owner:IsValid() and owner.components.container ~= nil and not owner.components.container:IsOpen() then
                        EnterLazyMode()
                    end
                end)
            end
        end)

        -- Also pause existing sleeping/closed-container items shortly after spawn/load
        inst:DoTaskInTime(0.5 + math.random() * 0.5, function()
            if G.POPULATING or not inst:IsValid() then return end
            local owner = inst.components.inventoryitem ~= nil and inst.components.inventoryitem.owner or nil
            if (owner == nil and inst:IsAsleep()) or
               (owner ~= nil and not owner:HasTag("player") and owner.components.container ~= nil and not owner.components.container:IsOpen()) then
                EnterLazyMode()
            end
        end)
    end)

    -- Hook Container Open / Close / RemoveItem to keep closed-container items synced seamlessly
    AddComponentPostInit("container", function(self, inst)
        if not (G.TheWorld and G.TheWorld.ismastersim) then return end
        if inst:HasTag("player") then return end

        local _origOpen = self.Open
        function self:Open(doer, ...)
            -- Snapshot slots BEFORE syncing because Perish() -> GiveItem(goop, slot) may mutate self.slots!
            local to_sync = {}
            for _, item in pairs(self.slots) do
                if item ~= nil and item.components.perishable ~= nil and item.components.perishable._HC_SyncLazy ~= nil then
                    to_sync[#to_sync + 1] = item
                end
            end
            for i = 1, #to_sync do
                local item = to_sync[i]
                if item:IsValid() and item.components.perishable ~= nil and item.components.perishable._HC_SyncLazy ~= nil then
                    item.components.perishable._HC_SyncLazy(true)
                end
            end
            return _origOpen(self, doer, ...)
        end

        local _origClose = self.Close
        function self:Close(doer, ...)
            local r = _origClose(self, doer, ...)
            if not self:IsOpen() then
                for _, item in pairs(self.slots) do
                    if item ~= nil and item.components.perishable ~= nil and item.components.perishable._HC_EnterLazy ~= nil then
                        item.components.perishable._HC_EnterLazy()
                    end
                end
            end
            return r
        end

        local _origRemoveSlot = self.RemoveItemBySlot
        function self:RemoveItemBySlot(slot, ...)
            local item = self.slots[slot]
            if item ~= nil and item.components.perishable ~= nil and item.components.perishable._HC_SyncLazy ~= nil then
                item.components.perishable._HC_SyncLazy(true)
            end
            return _origRemoveSlot(self, slot, ...)
        end
    end)
end

-- =============================================================================
-- [L3-OptIn-1] Safe Sleeping Ground Clutter Auto-Stack (NEVER deletes items!)
-- =============================================================================
if OPT_OFFLINE_AUTOSTACK then
    local MERGE_WHITELIST = {
        log = true, rocks = true, twigs = true, cutgrass = true, flint = true,
        nitre = true, goldnugget = true, charcoal = true, ash = true,
        spoiled_food = true, poop = true, guano = true, hounds_tooth = true,
        silk = true, spidergland = true, monstermeat = true, stinger = true,
        pinecone = true, acorn = true, twiggy_nut = true, seeds = true,
    }
    local function TryAutoStackOnSleep(inst)
        if G.POPULATING or not (inst:IsValid() and inst:IsAsleep()) then return end
        local inv = inst.components.inventoryitem
        local stk = inst.components.stackable
        if inv == nil or inv.owner ~= nil or stk == nil or stk:IsFull() then return end
        local x, y, z = inst.Transform:GetWorldPosition()
        -- Strict player distance guard (> 90 units from all players)
        for _, p in ipairs(G.AllPlayers) do
            if p:IsValid() and p:GetDistanceSqToPoint(x, y, z) < 8100 then
                return
            end
        end
        local ents = G.TheSim:FindEntities(x, 0, z, 6, { "_stackable" }, { "INLIMBO", "NOCLICK", "fire" })
        for _, other in ipairs(ents) do
            if other ~= inst and other:IsValid() and other.prefab == inst.prefab and other:IsAsleep() then
                local oinv = other.components.inventoryitem
                local ostk = other.components.stackable
                if oinv ~= nil and oinv.owner == nil and ostk ~= nil and not ostk:IsFull() then
                    local put_res = stk:Put(other)
                    HC_LUA_STATS.autostack_merges = HC_LUA_STATS.autostack_merges + 1
                    if stk:IsFull() then break end
                end
            end
        end
    end

    for prefab in pairs(MERGE_WHITELIST) do
        AddPrefabPostInit(prefab, function(inst)
            if not (G.TheWorld and G.TheWorld.ismastersim) then return end
            local orig_sleep = inst.OnEntitySleep
            inst.OnEntitySleep = function(ent, ...)
                if orig_sleep ~= nil then orig_sleep(ent, ...) end
                if G.POPULATING then return end
                if ent._hc_stack_task ~= nil then ent._hc_stack_task:Cancel() end
                ent._hc_stack_task = ent:DoTaskInTime(180 + math.random() * 20, TryAutoStackOnSleep)
            end
            local orig_wake = inst.OnEntityWake
            inst.OnEntityWake = function(ent, ...)
                if orig_wake ~= nil then orig_wake(ent, ...) end
                if ent._hc_stack_task ~= nil then
                    ent._hc_stack_task:Cancel()
                    ent._hc_stack_task = nil
                end
            end
        end)
    end
end

-- =============================================================================
-- [L3-OptIn-2] Wild Beefalo Soft Carrying-Capacity (NEVER deletes any beefalo!)
-- =============================================================================
if OPT_WILD_BEEFALO_CAP > 0 then
    local tracked_beefalos = {}
    AddPrefabPostInit("beefalo", function(inst)
        if not (G.TheWorld and G.TheWorld.ismastersim) then return end
        tracked_beefalos[inst] = true
        inst:ListenForEvent("onremove", function()
            tracked_beefalos[inst] = nil
        end)
    end)

    local function CountWildBeefalo()
        local count = 0
        for b in pairs(tracked_beefalos) do
            if b:IsValid() then
                local dom = b.components.domesticatable
                local ride = b.components.rideable
                local is_player_managed =
                    (dom ~= nil and dom.IsDomesticated ~= nil and dom:IsDomesticated()) or
                    (dom ~= nil and dom.GetDomestication ~= nil and dom:GetDomestication() > 0.1) or
                    (ride ~= nil and ride.IsSaddled ~= nil and ride:IsSaddled())
                if not is_player_managed then
                    count = count + 1
                end
            else
                tracked_beefalos[b] = nil
            end
        end
        return count
    end

    AddPrefabPostInit("beefaloherd", function(inst)
        if not (G.TheWorld and G.TheWorld.ismastersim) then return end
        local ps = inst.components.periodicspawner
        if ps ~= nil and ps.spawntest ~= nil then
            local orig_test = ps.spawntest
            ps:SetSpawnTestFn(function(herd_inst)
                if CountWildBeefalo() >= OPT_WILD_BEEFALO_CAP then
                    HC_LUA_STATS.beefalo_cap_blocks = HC_LUA_STATS.beefalo_cap_blocks + 1
                    return false
                end
                return orig_test(herd_inst)
            end)
        end
    end)
end

-- =============================================================================
-- Console X-Ray Readout: c_hyperstats()
-- =============================================================================
rawset(G, "c_hyperstats", function()
    print(string.format("=== DST HyperCore Server v1.2.0 X-Ray Stats (SimTick=%dHz) ===", HC_LUA_STATS.sim_tick_rate))
    local _getStats = rawget(G, "HyperCore_GetStats")
    if type(_getStats) == "function" then
        local s = _getStats()
        local ticks = math.max(1, s.ticks_total or 1)
        print(string.format("  [Native] ticks=%d | tick_us(avg=%.0f p95=%.0f p99=%.0f max=%.0f overruns=%d)",
            s.ticks_total, s.tick_avg_us, s.tick_p95_us, s.tick_p99_us, s.tick_max_us, s.tick_overruns))
        print(string.format("  [Native] GC: avg=%.0fus/t max=%.0fus full=%d kb=%d budget=%dus",
            s.gc_us_sum / ticks, s.gc_max_us, s.gc_full_count, s.gc_last_kb, s.gc_last_budget_us))
        print(string.format("  [Native] FindEntities: calls=%d avg=%.0fus/t | SpatialZC=%d cells=%d",
            s.find_entities_calls, s.find_entities_us_sum / ticks, s.spatial_zerocopy_hits, s.spatial_cells_visited))
        print(string.format("  [Native] FastDump: calls=%d total=%.2fms MB=%.2f fallbacks=%d",
            s.fastdump_calls, s.fastdump_us_sum / 1000, s.fastdump_bytes_sum / (1024*1024), s.fastdump_fallbacks))
        print(string.format("  [Native] libdeflate: calls=%d total=%.2fms in=%.2fMB out=%.2fMB",
            s.deflate_calls, s.deflate_us_sum / 1000, s.deflate_in_bytes / (1024*1024), s.deflate_out_bytes / (1024*1024)))
    else
        print("  [Native] iphlpapi.dll not installed (running pure-Lua Layer 2/3 mode)")
    end
    print(string.format("  [Lua] MermShortCircuit=%d MermFiltered=%d StaggerWake=%d PerishPaused=%d PerishSyncs=%d FastDumpHits=%d/%d AutoStack=%d BeefaloCapBlocks=%d",
        HC_LUA_STATS.merm_retarget_shortcircuits,
        HC_LUA_STATS.merm_retarget_filtered_calls,
        HC_LUA_STATS.staggered_wakes,
        HC_LUA_STATS.perish_paused_now,
        HC_LUA_STATS.perish_syncs,
        HC_LUA_STATS.fastdump_hits,
        HC_LUA_STATS.fastdump_fallbacks,
        HC_LUA_STATS.autostack_merges,
        HC_LUA_STATS.beefalo_cap_blocks))
end)
