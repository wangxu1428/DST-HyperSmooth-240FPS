-- =============================================================================
-- DST HyperCore Server v1.6.0 — Layer 2 (Zero-Divergence), Layer 3 & 60Hz SimTick
-- =============================================================================
-- Runs strictly on server master simulation (TheNet:GetIsServer() / IsMasterSim).
-- Safe to install or uninstall at any time: zero save-format lock-in.
-- =============================================================================

local G = GLOBAL
local rawget = G.rawget
local rawset = G.rawset
local pcall = G.pcall
local type = G.type
local tostring = G.tostring
local string = G.string
local math = G.math
local print = G.print
local ipairs = G.ipairs
local pairs = G.pairs
if not (G.TheNet and G.TheNet:GetIsServer()) then
    return
end

local current_server_lang = "auto"

local function IsServerHostChinese()
    local lt = rawget(G, "LanguageTranslator")
    if type(lt) == "table" and type(lt.defaultlang) == "string" and #lt.defaultlang > 0 then
        local dl = lt.defaultlang
        if dl == "zh" or dl == "zhr" or dl == "zht" then
            return true
        end
        if dl ~= "en" then
            return false
        end
    end
    local strings = rawget(G, "STRINGS")
    local opt_str = type(strings) == "table" and type(strings.UI) == "table" and strings.UI.OPTIONS or nil
    if type(opt_str) == "table" and type(opt_str.ENABLED) == "string" then
        return opt_str.ENABLED == "启用" or opt_str.ENABLED == "啟用"
    end
    return false
end

local function ResolveServerIsZh()
    if current_server_lang == "zh" then return true end
    if current_server_lang == "en" then return false end
    return IsServerHostChinese()
end

rawset(G, "c_hyperlang", function(lang)
    if type(lang) == "string" then
        local s = lang:lower()
        if s == "zh" or s == "cn" then
            current_server_lang = "zh"
        elseif s == "en" or s == "eng" then
            current_server_lang = "en"
        elseif s == "auto" then
            current_server_lang = "auto"
        end
    end
    local zh = ResolveServerIsZh()
    local tag = current_server_lang == "zh" and "简体中文"
        or (current_server_lang == "en" and "English" or (zh and "自动检测(中文)" or "Auto(English)"))
    print(zh and ("[HyperCore 服务端] 控制台输出语言 -> " .. tag)
             or ("[HyperCore Server] Console output language -> " .. tag))
end)

local OPT_FAST_DUMPER       = GetModConfigData("opt_fast_dumper")
local OPT_MERM_RETARGET     = GetModConfigData("opt_merm_retarget")
local OPT_STAGGERED_WAKE    = GetModConfigData("opt_staggered_wake")
local OPT_LAZY_PERISHABLE   = GetModConfigData("opt_lazy_perishable")
local OPT_OFFLINE_AUTOSTACK = GetModConfigData("opt_offline_autostack")
local OPT_WILD_BEEFALO_CAP  = GetModConfigData("opt_wild_beefalo_cap") or 0
local OPT_SIM_60HZ          = GetModConfigData("opt_sim_60hz") == true
local OPT_HARDWARE_TIER     = GetModConfigData("hardware_tier")
if OPT_HARDWARE_TIER == nil then OPT_HARDWARE_TIER = 1 end
local OPT_REPLICA_PACING    = GetModConfigData("opt_replica_pacing")
if OPT_REPLICA_PACING == nil then OPT_REPLICA_PACING = -1 end

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
    hardware_tier = 1,
    replica_pacing_budget = -1,
}
rawset(G, "HyperCoreLuaStats", HC_LUA_STATS)

local _hcSetHwTier = rawget(G, "HyperCore_SetHardwareTier")
if type(_hcSetHwTier) == "function" then
    local ok_ht, _, applied_ht = pcall(_hcSetHwTier, OPT_HARDWARE_TIER)
    if ok_ht and type(applied_ht) == "number" then
        HC_LUA_STATS.hardware_tier = applied_ht
    end
end

local _hcSetRepPace = rawget(G, "HyperCore_SetReplicaPacing")
if type(_hcSetRepPace) == "function" and type(OPT_REPLICA_PACING) == "number" and OPT_REPLICA_PACING >= 0 then
    local ok_rp, _, applied_rp = pcall(_hcSetRepPace, OPT_REPLICA_PACING)
    if ok_rp and type(applied_rp) == "number" then
        HC_LUA_STATS.replica_pacing_budget = applied_rp
    end
end

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
if ResolveServerIsZh() then
    print(string.format("[HyperCore 服务端] 已加载 v1.6.0 | 原生插件=%s | 逻辑刻=%dHz 硬件档位=%d | 极速存档=%s 鱼人修复=%s 错峰苏醒=%s 惰性腐烂=%s 自动堆叠=%s 野牛上限=%d",
        has_native and _hcVersion() or "未安装",
        active_sim_hz, HC_LUA_STATS.hardware_tier,
        tostring(OPT_FAST_DUMPER), tostring(OPT_MERM_RETARGET), tostring(OPT_STAGGERED_WAKE),
        tostring(OPT_LAZY_PERISHABLE), tostring(OPT_OFFLINE_AUTOSTACK), OPT_WILD_BEEFALO_CAP))
else
    print(string.format("[HyperCore Server] Loaded v1.6.0 | NativeDLL=%s | SimTick=%dHz HWTier=%d | FastDump=%s MermFix=%s StaggerWake=%s LazyPerish=%s AutoStack=%s WildBeefaloCap=%d",
        has_native and _hcVersion() or "none",
        active_sim_hz, HC_LUA_STATS.hardware_tier,
        tostring(OPT_FAST_DUMPER), tostring(OPT_MERM_RETARGET), tostring(OPT_STAGGERED_WAKE),
        tostring(OPT_LAZY_PERISHABLE), tostring(OPT_OFFLINE_AUTOSTACK), OPT_WILD_BEEFALO_CAP))
end

-- Dynamic Lua 5.1 32-bit unsigned bitmask builder for causal 3-channel RPC QoS
local function RegisterServerRpcQosMasks()
    local fnSetMasks = rawget(G, "HyperCore_SetRpcChannelMasks")
    if type(fnSetMasks) ~= "function" then return end

    -- Strictly causal-safe, entity-free, handshake-independent Client->Server RPC whitelist:
    --   * AddSkillXP, SetSkillActivatedState, SetClientAuthoritativeSetting MUST stay on Ch0
    --     because PostActivateHandshake(CTS_LOADED) requires strict FIFO delivery after them
    --     (otherwise ValidateCharacterData sees skillxp=0 on cave migration and resets skill tree to 0).
    --   * ClosePopup, RecievePopupMessage, OpenGift, SetWriteableText, OnScrapbookDataTaught
    --     stay on Ch0 because they mutate live player StateGraphs or pass live entity references.
    local srv_non_combat_names = {
        "ToggleController", "GetChatHistory",
    }
    -- Strictly entity-free, handshake-independent Server->Client broadcast whitelist
    -- (ShowPopup/RecievePopupMessage carry live entities; SetSkillActivatedState/AddSkillXP
    --  must stay strictly FIFO-ordered with CLIENT_RPC.PostActivateHandshake on Ch0!)
    local cli_non_entity_names = {
        "LearnRecipe", "LearnFoodStats",
        "LearnPlantStage", "LearnFertilizerStage", "TakeOversizedPicture",
        "RecieveChatHistory", "LearnBuilderRecipe", "UpdateAccomplishment",
        "UpdateCountAccomplishment",
    }

    local srv_tbl = rawget(G, "RPC")
    local cli_tbl = rawget(G, "CLIENT_RPC")
    local srv0, srv1, srv2, cli0, cli1 = 0, 0, 0, 0, 0

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
            if type(id) == "number" and id >= 1 and id < 64 and math.floor(id) == id and not seen[id] then
                seen[id] = true
                if id < 32 then
                    cli0 = cli0 + (2 ^ id)
                else
                    cli1 = cli1 + (2 ^ (id - 32))
                end
            end
        end
    end
    pcall(fnSetMasks, srv0, srv1, srv2, cli0, cli1)
end

G.HyperCore_RegisterRpcQosMasks = RegisterServerRpcQosMasks
RegisterServerRpcQosMasks()
AddSimPostInit(RegisterServerRpcQosMasks)

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
            HC_LUA_STATS.fastdump_fallbacks = HC_LUA_STATS.fastdump_fallbacks + 1
            if err == "buffer_limit_exceeded" or err == "corrupt_number" then
                if ResolveServerIsZh() then
                    print("[HyperCore 极速存档] 拒绝不安全的原版回退：" .. tostring(err))
                    G.error("[HyperCore 极速存档] 已中止异常序列化（" .. tostring(err) .. "）", 2)
                else
                    print("[HyperCore FastDataDumper] Refusing unsafe vanilla fallback: " .. tostring(err))
                    G.error("[HyperCore FastDataDumper] Aborted unsafe serialization (" .. tostring(err) .. ")", 2)
                end
            elseif err == "cycle_detected" or err == "node_limit_exceeded" or err == "depth_limit" or err == "stack_limit" then
                if ResolveServerIsZh() then
                    print("[HyperCore 极速存档] 触发安全阈值（" .. tostring(err) .. "），自动降级为原版防环序列化")
                else
                    print("[HyperCore FastDataDumper] Safety limit (" .. tostring(err) .. "); downgrading to cycle-safe slow DataDumper")
                end
                return _origDataDumper(value, varname, false, ident)
            end
        end
        return _origDataDumper(value, varname, fastmode, ident)
    end
end

-- =============================================================================
-- [L2-1B] C++ FastJson Bridge (json.encode / json.encode_compliant / json.decode)
-- =============================================================================
do
    local json_mod = rawget(G, "json")
    if type(json_mod) ~= "table" and type(G.require) == "function" then
        local ok_j, m_j = G.pcall(G.require, "json")
        if ok_j and type(m_j) == "table" then
            json_mod = m_j
        end
    end
    local fnEnc = rawget(G, "HyperCore_FastJsonEncode") or rawget(G, "HyperSmooth_FastJsonEncode")
    local fnDec = rawget(G, "HyperCore_FastJsonDecode") or rawget(G, "HyperSmooth_FastJsonDecode")
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

-- =============================================================================
-- [L2-1C] Zero-Alloc EntityScript:PushEvent_Internal & Unrolled HasTags/HasOneOfTags
-- =============================================================================
do
    local EntityScript = rawget(G, "EntityScript")
    if type(EntityScript) ~= "table" and type(G.require) == "function" then
        local ok_es, es_mod = G.pcall(G.require, "entityscript")
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
                        local first_ent, first_fns = G.next(listeners)
                        if first_ent ~= nil then
                            if G.next(listeners, first_ent) == nil then
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
                                for _, fns in G.pairs(listeners) do
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
                        local sgm = rawget(G, "SGManager")
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

        if rawget(G, "BRANCH") ~= "dev" and not EntityScript._hs_fast_hastags then
            EntityScript._hs_fast_hastags = true
            local _unpack = rawget(G, "unpack") or G.unpack

            function EntityScript:HasTags(t1, ...)
                if t1 == nil then
                    return self.entity:HasAllTags(...)
                elseif G.type(t1) == "table" then
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
                elseif G.type(t1) == "table" then
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

    -- Pure read-only environmental integral snapshot (ZERO state mutation; safe inside GetPercent / OnSave)
    local function PeekEnvIntegral()
        local ng = EnvIntegral.normal_ground
        local ni = EnvIntegral.normal_indoor
        local nf = EnvIntegral.frozen_mult_int
        local na = EnvIntegral.acid_extra_pct
        local world = G.TheWorld
        if world == nil or world.state == nil or not EnvIntegral.initialized then
            return ng, ni, nf, na
        end
        local dt = G.GetTime() - EnvIntegral.last_t
        if dt <= 0 then
            return ng, ni, nf, na
        end
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
        local wet_mult = (st.israining and (T.PERISH_WET_MULT or 1.3)) or 1.0
        ni = ni + base_step
        nf = nf + frozen_step
        ng = ng + base_step * wet_mult
        if st.isacidraining then
            local rate = st.precipitationrate or 0
            na = na + rate * (T.ACIDRAIN_PERISHABLE_ROT_PERCENT or 0) * dt
        end
        return ng, ni, nf, na
    end

    -- Deduplicated weak-key queue of expired lazy items observed during OnSave();
    -- drained outside OnSave() so OnSave() has ZERO scheduler/entity side effects!
    local PendingExpiredLazy = G.setmetatable({}, { __mode = "k" })

    local function DrainPendingExpiredLazy()
        if G.next(PendingExpiredLazy) == nil then return end
        local snapshot = {}
        local n = 0
        for ent, comp in G.pairs(PendingExpiredLazy) do
            PendingExpiredLazy[ent] = nil
            n = n + 1
            snapshot[n] = { ent = ent, comp = comp }
        end
        for i = 1, n do
            local item = snapshot[i]
            if item.ent ~= nil and item.ent:IsValid() and item.comp ~= nil and item.comp._hc_lazy_active then
                item.comp._HC_SyncLazy(true)
            end
        end
    end

    AddSimPostInit(function()
        if G.TheWorld ~= nil and G.TheWorld.ismastersim then
            StepEnvIntegral()
            G.TheWorld:DoPeriodicTask(5, StepEnvIntegral)
            G.TheWorld:DoPeriodicTask(1, DrainPendingExpiredLazy)
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

        -- Decay estimator: read_only=true uses pure PeekEnvIntegral() with ZERO global mutation!
        local function ComputeLazyDelta(read_only)
            local ng, ni, nf, na
            if read_only then
                ng, ni, nf, na = PeekEnvIntegral()
            else
                StepEnvIntegral()
                ng = EnvIntegral.normal_ground
                ni = EnvIntegral.normal_indoor
                nf = EnvIntegral.frozen_mult_int
                na = EnvIntegral.acid_extra_pct
            end
            local local_mult = self.localPerishMultiplyer or 1.0
            local owner_mult = self._hc_lazy_owner_mult or 1.0
            local delta_decay = 0
            if not self._hc_lazy_is_indoor then
                local dg = self._hc_lazy_is_frozen
                    and (nf - (self._hc_lazy_snap_frozen or nf))
                    or  (ng - (self._hc_lazy_snap_ground or ng))
                delta_decay = dg * owner_mult * local_mult
                local dacid = na - (self._hc_lazy_snap_acid or na)
                if dacid > 0 and self.perishtime ~= nil and self.perishtime > 0 then
                    delta_decay = delta_decay + self.perishtime * dacid
                end
            else
                local di = self._hc_lazy_is_frozen
                    and (nf - (self._hc_lazy_snap_frozen or nf))
                    or  (ni - (self._hc_lazy_snap_indoor or ni))
                delta_decay = di * owner_mult * local_mult
            end
            return delta_decay
        end

        local function ComputeLazyRemaining()
            if self.perishremainingtime == nil then return nil end
            local delta_decay = ComputeLazyDelta(true)
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
                -- Settle accumulated lazy decay FIRST so StartPerishing never drops elapsed decay!
                SyncLazyMode(false)
                if not inst:IsValid() then return end
            end
            return _origStartPerishing(self)
        end

        local _origStopPerishing = self.StopPerishing
        function self:StopPerishing()
            if self._hc_lazy_active then
                -- Settle accumulated lazy decay FIRST so StopPerishing never jumps freshness backward!
                SyncLazyMode(false)
                if not inst:IsValid() then return end
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
        -- NEVER calls Perish() / inst:Remove() / SpawnPrefab() / DoTaskInTime() during serialization,
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
                    -- Deduplicated weak-key mark; drained outside OnSave() by DrainPendingExpiredLazy()
                    PendingExpiredLazy[inst] = self
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
            PendingExpiredLazy[inst] = nil
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
            PendingExpiredLazy[inst] = nil
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
-- Console X-Ray Readout: c_hyperstats() & c_hypertier(tier)
-- =============================================================================
local function FormatHwTierLine(info, zh)
    if type(info) ~= "table" then return nil end
    local tnames_zh = { [0] = "省电低配(0)", [1] = "均衡标准(1)", [2] = "高端解放(2)" }
    local tnames_en = { [0] = "Low(0)", [1] = "Balanced(1)", [2] = "Unleashed(2)" }
    local t = info.tier or 1
    local tname = (zh and tnames_zh[t] or tnames_en[t]) or tostring(info.tier_name or t)
    local core_str = (info.ideal_core ~= nil and info.ideal_core >= 0) and ("CPU" .. tostring(info.ideal_core)) or (zh and "自由调度" or "auto")
    if zh then
        return string.format("  [硬件档位] 分片=%s 档位=%s | 线程=%d(P核=%d, 亲和=%s) 内存=%.1fGB | EcoQoS关闭=%s mimalloc延迟=%dms | 压缩=L%d 队列=%dMB 寻路池=%d 副本限流=%d | GC应急=%dMB(斜坡%.0fMB 忙=%.2fms)",
            tostring(info.shard or "?"), tname,
            info.cores or 0, info.perf_cores or 0, core_str, (info.ram_mb or 0) / 1024,
            tostring(info.eco_qos_disabled == true), info.mi_purge_ms or 0,
            info.deflate_level or 5, info.async_queue_mb or 64, info.astar_pool or 64,
            info.replica_construct_budget or 0,
            info.gc_emerg_mb or 256, info.gc_ramp_mb or 32, info.gc_busy_ms or 0.35)
    else
        return string.format("  [HWTier] shard=%s tier=%s | cores=%d(P=%d, ideal=%s) ram=%.1fGB | ecoOff=%s miPurge=%dms | deflate=L%d asyncQ=%dMB astarPool=%d repPace=%d | gcEmerg=%dMB(ramp=%.0fMB busy=%.2fms)",
            tostring(info.shard or "?"), tname,
            info.cores or 0, info.perf_cores or 0, core_str, (info.ram_mb or 0) / 1024,
            tostring(info.eco_qos_disabled == true), info.mi_purge_ms or 0,
            info.deflate_level or 5, info.async_queue_mb or 64, info.astar_pool or 64,
            info.replica_construct_budget or 0,
            info.gc_emerg_mb or 256, info.gc_ramp_mb or 32, info.gc_busy_ms or 0.35)
    end
end

rawset(G, "c_hyperpacing", function(budget)
    local zh = ResolveServerIsZh()
    local fnSet = rawget(G, "HyperCore_SetReplicaPacing")
    local fnGet = rawget(G, "HyperCore_GetHardwareInfo")
    if budget ~= nil then
        if type(fnSet) ~= "function" then
            print(zh and "[HyperCore 副本限流] 原生引擎未安装，无法调节副本构造分帧限流。"
                     or "[HyperCore RepPacing] Native engine not installed.")
            return false
        end
        local ok, applied = fnSet(budget)
        if ok and type(applied) == "number" then
            HC_LUA_STATS.replica_pacing_budget = applied
        end
        print(zh
            and string.format("[HyperCore 副本限流] 当前 Tier-1 每刻构造上限 -> %d (0=关闭, auto=跟随硬件档位)", applied or 0)
            or string.format("[HyperCore RepPacing] Effective Tier-1 construct budget -> %d/tick (0=off, 'auto'=tier-managed)", applied or 0))
        return applied
    end
    local info = (type(fnGet) == "function") and fnGet() or nil
    if type(info) == "table" then
        local cur = info.replica_construct_budget or 0
        print(zh
            and string.format("[HyperCore 副本限流] 当前=%d/刻 (手动覆盖=%s) | 用法: c_hyperpacing('auto'|0|16..1024)",
                cur, tostring(info.replica_budget_explicit == true))
            or string.format("[HyperCore RepPacing] current=%d/tick (explicit=%s) | Usage: c_hyperpacing('auto'|0|16..1024)",
                cur, tostring(info.replica_budget_explicit == true)))
        return cur
    end
    print(zh and "[HyperCore 副本限流] 原生引擎未安装。" or "[HyperCore RepPacing] Native engine not installed.")
    return -1
end)

rawset(G, "c_hypertier", function(tier)
    local zh = ResolveServerIsZh()
    local fnSet = rawget(G, "HyperCore_SetHardwareTier")
    local fnGet = rawget(G, "HyperCore_GetHardwareInfo")
    if tier ~= nil then
        if type(fnSet) ~= "function" then
            print(zh and "[HyperCore 硬件档位] 原生引擎未安装，无法切换底层硬件档位。"
                     or "[HyperCore HWTier] Native engine not installed.")
            return false
        end
        local ok, applied = fnSet(tier)
        if ok and type(applied) == "number" then
            HC_LUA_STATS.hardware_tier = applied
        end
        local info = (type(fnGet) == "function") and fnGet() or nil
        local line = FormatHwTierLine(info, zh)
        if line then print(line) end
        return applied or HC_LUA_STATS.hardware_tier
    end
    local info = (type(fnGet) == "function") and fnGet() or nil
    local line = FormatHwTierLine(info, zh)
    if line then
        print(line)
        print(zh and "  用法: c_hypertier(0|1|2) 或 c_hypertier('low'|'balanced'|'unleashed')"
                 or "  Usage: c_hypertier(0|1|2) or c_hypertier('low'|'balanced'|'unleashed')")
        return info.tier or HC_LUA_STATS.hardware_tier
    end
    print(zh and "[HyperCore 硬件档位] 原生引擎未安装。" or "[HyperCore HWTier] Native engine not installed.")
    return HC_LUA_STATS.hardware_tier
end)

rawset(G, "c_hyperstats", function()
    local zh = ResolveServerIsZh()
    print(zh
        and string.format("=== DST HyperCore 服务端 v1.6.0 性能体检 (逻辑刻=%dHz) ===", HC_LUA_STATS.sim_tick_rate)
        or string.format("=== DST HyperCore Server v1.6.0 X-Ray Stats (SimTick=%dHz) ===", HC_LUA_STATS.sim_tick_rate))
    local _getHw = rawget(G, "HyperCore_GetHardwareInfo")
    if type(_getHw) == "function" then
        local hw_line = FormatHwTierLine(_getHw(), zh)
        if hw_line then print(hw_line) end
    end
    local _getStats = rawget(G, "HyperCore_GetStats")
    if type(_getStats) == "function" then
        local s = _getStats()
        local ticks = math.max(1, s.ticks_total or 1)
        if zh then
            print(string.format("  [原生刻] 总刻数=%d | 单刻耗时(均=%.0f P95=%.0f P99=%.0f 峰值=%.0f 超时=%d)",
                s.ticks_total, s.tick_avg_us, s.tick_p95_us, s.tick_p99_us, s.tick_max_us, s.tick_overruns))
            print(string.format("  [垃圾回收] 均=%.0fus/刻 峰值=%.0fus 全量=%d 内存=%dKB 预算=%dus",
                s.gc_us_sum / ticks, s.gc_max_us, s.gc_full_count, s.gc_last_kb, s.gc_last_budget_us))
            print(string.format("  [空间网格] 实体查询=%d (均=%.0fus/刻) | 零拷贝=%d 光照=%d 遍历格=%d",
                s.find_entities_calls, s.find_entities_us_sum / ticks, s.spatial_zerocopy_hits, s.light_spatial_queries or 0, s.spatial_cells_visited))
            print(string.format("  [极速寻路(%s)] 初始化=%d 步数=%d 总计=%.2fms 成功=%d 失败=%d 回退=%d",
                tostring(s.opt_fast_astar == true), s.astar_inits or 0, s.astar_steps or 0, (s.astar_us_sum or 0) / 1000,
                s.astar_completed or 0, s.astar_failed or 0, s.astar_fallbacks or 0))
            print(string.format("  [极速序列] FastDump=%d次 %.2fms (%.2fMB 回退=%d) | FastJson(编=%d 解=%d 回退=%d)",
                s.fastdump_calls, s.fastdump_us_sum / 1000, s.fastdump_bytes_sum / (1024*1024), s.fastdump_fallbacks,
                s.fastjson_enc_calls or 0, s.fastjson_dec_calls or 0, s.fastjson_fallbacks or 0))
            print(string.format("  [异步压缩] deflate=%d次 %.2fms (入=%.2fMB 出=%.2fMB) | 解压=%d次 (出=%.2fMB)",
                s.deflate_calls, s.deflate_us_sum / 1000, s.deflate_in_bytes / (1024*1024), s.deflate_out_bytes / (1024*1024),
                s.uncomp_calls or 0, (s.uncomp_out_bytes or 0) / (1024*1024)))
            local luaC = math.max(1, s.prof_lua_update_calls or 1)
            local netC = math.max(1, s.prof_net_update_calls or 1)
            local repC = math.max(1, s.prof_replica_update_calls or 1)
            print(string.format("  [分阶段] Lua更新=%.0fus(峰=%d) 网络=%.0fus(峰=%d) 副本同步=%.0fus(峰=%d)",
                (s.prof_lua_update_us_sum or 0) / luaC, s.prof_lua_update_us_max or 0,
                (s.prof_net_update_us_sum or 0) / netC, s.prof_net_update_us_max or 0,
                (s.prof_replica_update_us_sum or 0) / repC, s.prof_replica_update_us_max or 0))
            print(string.format("  [副本加速(%s)] 预算=%d/刻 近距感知=%s | QRL=%d 构造=%d 销毁=%d | 延后=%d 洪峰刻=%d 父提升=%d 近距直达=%d",
                tostring(s.opt_replica_fastpath == true), s.replica_construct_budget or 0,
                tostring(s.replica_proximity_active == true),
                s.replica_qrl_calls or 0, s.replica_constructed_total or 0, s.replica_destroyed_total or 0,
                s.replica_paced_deferred_total or 0, s.replica_paced_burst_ticks or 0,
                s.replica_paced_parent_promotes or 0, s.replica_paced_near_bypasses or 0))
            print(string.format("  [IO与网桥] 负缓存(%s)=%d/%d | 异步存盘(%s)=入队%d 完成%d 压缩%d(%.2fMB %.1fms) 刷盘%d 回退%d 错%d IO=%.2fms | 优先RPC(%s)=直达%d 分流%d/%d",
                tostring(s.opt_fs_neg_cache == true), s.fs_neg_hits or 0, s.fs_neg_misses or 0,
                tostring(s.opt_async_save == true), s.async_save_enqueued or 0, s.async_save_committed or 0,
                s.async_save_compressed or 0, (s.async_save_bytes or 0) / (1024*1024), (s.async_save_compress_us_sum or 0) / 1000,
                s.async_save_flushes or 0, s.async_save_fallbacks or 0, s.async_save_errors or 0,
                (s.async_save_io_us_sum or 0) / 1000,
                tostring(s.opt_rpc_qos == true), s.rpc_qos_imm or 0, s.rpc_qos_off_ch0 or 0, s.rpc_qos_total or 0))
        else
            print(string.format("  [Native] ticks=%d | tick_us(avg=%.0f p95=%.0f p99=%.0f max=%.0f overruns=%d)",
                s.ticks_total, s.tick_avg_us, s.tick_p95_us, s.tick_p99_us, s.tick_max_us, s.tick_overruns))
            print(string.format("  [Native] GC: avg=%.0fus/t max=%.0fus full=%d kb=%d budget=%dus",
                s.gc_us_sum / ticks, s.gc_max_us, s.gc_full_count, s.gc_last_kb, s.gc_last_budget_us))
            print(string.format("  [Native] SpatialZC: FindEnts=%d (avg=%.0fus/t) | TotalZC=%d LightZC=%d cells=%d",
                s.find_entities_calls, s.find_entities_us_sum / ticks, s.spatial_zerocopy_hits, s.light_spatial_queries or 0, s.spatial_cells_visited))
            print(string.format("  [Native] FastAStar(%s): inits=%d steps=%d total=%.2fms ok=%d fail=%d fb=%d",
                tostring(s.opt_fast_astar == true), s.astar_inits or 0, s.astar_steps or 0, (s.astar_us_sum or 0) / 1000,
                s.astar_completed or 0, s.astar_failed or 0, s.astar_fallbacks or 0))
            print(string.format("  [Native] FastDump: calls=%d total=%.2fms MB=%.2f fb=%d | FastJson: enc=%d dec=%d fb=%d",
                s.fastdump_calls, s.fastdump_us_sum / 1000, s.fastdump_bytes_sum / (1024*1024), s.fastdump_fallbacks,
                s.fastjson_enc_calls or 0, s.fastjson_dec_calls or 0, s.fastjson_fallbacks or 0))
            print(string.format("  [Native] libdeflate: calls=%d total=%.2fms in=%.2fMB out=%.2fMB | uncomp=%d out=%.2fMB",
                s.deflate_calls, s.deflate_us_sum / 1000, s.deflate_in_bytes / (1024*1024), s.deflate_out_bytes / (1024*1024),
                s.uncomp_calls or 0, (s.uncomp_out_bytes or 0) / (1024*1024)))
            local luaC = math.max(1, s.prof_lua_update_calls or 1)
            local netC = math.max(1, s.prof_net_update_calls or 1)
            local repC = math.max(1, s.prof_replica_update_calls or 1)
            print(string.format("  [Native] SubPhase: luaUpd=%.0fus(max=%d) netUpd=%.0fus(max=%d) repUpd=%.0fus(max=%d)",
                (s.prof_lua_update_us_sum or 0) / luaC, s.prof_lua_update_us_max or 0,
                (s.prof_net_update_us_sum or 0) / netC, s.prof_net_update_us_max or 0,
                (s.prof_replica_update_us_sum or 0) / repC, s.prof_replica_update_us_max or 0))
            print(string.format("  [Native] RepFast(%s): budget=%d/t prox=%s | qrl=%d con=%d dst=%d | def=%d bursts=%d parentPro=%d nearByp=%d",
                tostring(s.opt_replica_fastpath == true), s.replica_construct_budget or 0,
                tostring(s.replica_proximity_active == true),
                s.replica_qrl_calls or 0, s.replica_constructed_total or 0, s.replica_destroyed_total or 0,
                s.replica_paced_deferred_total or 0, s.replica_paced_burst_ticks or 0,
                s.replica_paced_parent_promotes or 0, s.replica_paced_near_bypasses or 0))
            print(string.format("  [Native] FSNeg(%s): hits=%d misses=%d | AsyncSave(%s): enq=%d ok=%d comp=%d MB=%.2f flush=%d fb=%d err=%d comp=%.1fms io=%.2fms | RpcQoS(%s): imm=%d offCh0=%d tot=%d",
                tostring(s.opt_fs_neg_cache == true), s.fs_neg_hits or 0, s.fs_neg_misses or 0,
                tostring(s.opt_async_save == true), s.async_save_enqueued or 0, s.async_save_committed or 0,
                s.async_save_compressed or 0, (s.async_save_bytes or 0) / (1024*1024), s.async_save_flushes or 0,
                s.async_save_fallbacks or 0, s.async_save_errors or 0,
                (s.async_save_compress_us_sum or 0) / 1000, (s.async_save_io_us_sum or 0) / 1000,
                tostring(s.opt_rpc_qos == true), s.rpc_qos_imm or 0, s.rpc_qos_off_ch0 or 0, s.rpc_qos_total or 0))
        end
    else
        print(zh
            and "  [原生] 未安装原生插件 (当前以纯 Lua 优化模式运行)"
            or "  [Native] iphlpapi.dll / libhypercore.so not installed (running pure-Lua Layer 2/3 mode)")
    end
    print(zh
        and string.format("  [Lua优化] 鱼人短路=%d 过滤=%d 错峰苏醒=%d 惰性腐烂(暂停=%d 同步=%d) 极速存档=%d/%d 自动堆叠=%d 野牛拦截=%d",
            HC_LUA_STATS.merm_retarget_shortcircuits,
            HC_LUA_STATS.merm_retarget_filtered_calls,
            HC_LUA_STATS.staggered_wakes,
            HC_LUA_STATS.perish_paused_now,
            HC_LUA_STATS.perish_syncs,
            HC_LUA_STATS.fastdump_hits,
            HC_LUA_STATS.fastdump_fallbacks,
            HC_LUA_STATS.autostack_merges,
            HC_LUA_STATS.beefalo_cap_blocks)
        or string.format("  [Lua] MermShortCircuit=%d MermFiltered=%d StaggerWake=%d PerishPaused=%d PerishSyncs=%d FastDumpHits=%d/%d AutoStack=%d BeefaloCapBlocks=%d",
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

do
    local prof_active = false
    local _tonumber = G.tonumber
    local _type = G.type
    local _pairs = G.pairs
    local _print = G.print
    local _string = G.string
    local _table = G.table
    local _math = G.math
    local function ClassifyServerProfSource(src, zh)
        if type(src) ~= "string" then
            return zh and "[其他脚本]" or "[Other]"
        end
        local norm = _string.gsub(src, "\\", "/")
        local wdir = _string.match(norm, "(workshop%-%d+)/")
        if not wdir then
            wdir = _string.match(norm, "mods/([^/]+)/")
        end
        if wdir then
            if wdir == "hypercore_server" then
                return zh and "[HyperCore服务端]" or "[HyperCoreServer]"
            end
            local kmi = rawget(G, "KnownModIndex")
            if type(kmi) == "table" and type(kmi.GetModFancyName) == "function" then
                local ok_n, fname = pcall(kmi.GetModFancyName, kmi, wdir)
                if ok_n and type(fname) == "string" and #fname > 0 then
                    return fname .. " (" .. wdir .. ")"
                end
            end
            return wdir
        end
        if _string.find(norm, "^scripts/") or _string.find(norm, "/scripts/") then
            return zh and "[原版脚本]" or "[Vanilla]"
        end
        return zh and "[其他脚本]" or "[Other]"
    end

    rawset(G, "c_hyperprof", function(seconds)
        local zh = ResolveServerIsZh()
        local dbg = rawget(G, "debug")
        if _type(dbg) ~= "table" or _type(dbg.sethook) ~= "function" or _type(dbg.getinfo) ~= "function" then
            _print(zh and "[HyperCore 采样] 当前环境不支持 debug.sethook。" or "[HyperCore Prof] debug.sethook unavailable.")
            return false
        end
        if prof_active then
            _print(zh and "[HyperCore 采样] 正在采样中，请稍候。" or "[HyperCore Prof] Profiling already in progress.")
            return false
        end
        local dur = _tonumber(seconds) or 5
        if dur < 1 then dur = 1 end
        if dur > 30 then dur = 30 end

        local os_mod = rawget(G, "os")
        local get_time = (_type(os_mod) == "table" and _type(os_mod.clock) == "function")
            and os_mod.clock or (rawget(G, "GetTime") or function() return 0 end)
        local start_t = get_time()
        local stop_t = start_t + dur
        local counts = {}
        local mod_counts = {}
        local src_mod_cache = {}
        local total = 0
        prof_active = true

        local function FinishProf()
            if not prof_active then return end
            prof_active = false
            dbg.sethook()
            local fin_zh = ResolveServerIsZh()

            local mod_rows = {}
            for k, v in _pairs(mod_counts) do
                mod_rows[#mod_rows + 1] = { k = k, v = v }
            end
            _table.sort(mod_rows, function(a, b) return a.v > b.v end)

            local rows = {}
            for k, v in _pairs(counts) do
                rows[#rows + 1] = { k = k, v = v }
            end
            _table.sort(rows, function(a, b) return a.v > b.v end)

            _print(fin_zh
                and _string.format("=== HyperCore 服务端 Lua 热点与卡顿 Mod 归因 (耗时%.1fs, 样本=%d) ===",
                    _math.max(0.01, get_time() - start_t), total)
                or _string.format("=== HyperCore Server Lua Hotspot & Mod Culprit Profile (%.1fs, samples=%d) ===",
                    _math.max(0.01, get_time() - start_t), total))
            _print(fin_zh and "  --- Mod 占用排行 (Top Mods) ---" or "  --- Top Mods by Sample Share ---")
            local mod_limit = _math.min(10, #mod_rows)
            for i = 1, mod_limit do
                local pct = total > 0 and (100.0 * mod_rows[i].v / total) or 0.0
                _print(_string.format("  [M#%02d] %5.1f%% (%5d) %s", i, pct, mod_rows[i].v, mod_rows[i].k))
            end
            _print(fin_zh and "  --- 具体函数热点 Top 15 ---" or "  --- Top 15 Function Hotspots ---")
            local limit = _math.min(15, #rows)
            for i = 1, limit do
                local pct = total > 0 and (100.0 * rows[i].v / total) or 0.0
                _print(_string.format("  #%02d %5.1f%% (%5d) %s", i, pct, rows[i].v, rows[i].k))
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
                local raw_src = info.source or src
                local mlabel = src_mod_cache[raw_src]
                if not mlabel then
                    mlabel = ClassifyServerProfSource(raw_src, zh)
                    src_mod_cache[raw_src] = mlabel
                end
                mod_counts[mlabel] = (mod_counts[mlabel] or 0) + 1
                local line = info.linedefined or info.currentline or 0
                local name = info.name or "?"
                local key = _string.format("%s %s:%d (%s)", mlabel, src, line, name)
                counts[key] = (counts[key] or 0) + 1
                total = total + 1
            end
        end, "", 2000)

        local world = rawget(G, "TheWorld")
        if world and _type(world.DoTaskInTime) == "function" then
            world:DoTaskInTime(dur, FinishProf)
        elseif _type(rawget(G, "scheduler")) == "table" and _type(G.scheduler.ExecuteInTime) == "function" then
            G.scheduler:ExecuteInTime(dur, FinishProf)
        end
        _print(zh
            and _string.format("[HyperCore 采样] 正在采样 Lua 调用热点（%.0f秒，每 2000 条指令一次）...", dur)
            or _string.format("[HyperCore Prof] Sampling Lua callsites for %.0fs (every 2000 instructions)...", dur))
        return true
    end)
end

do
    local SRV_CULPRIT_ZH = {
        SRV_LUA         = "服务端Lua脚本",
        SRV_GC          = "服务端GC回收",
        SRV_NET         = "服务端网络更新",
        SRV_REPLICA     = "实体副本同步",
        SRV_SPATIAL     = "空间网格查询",
        SRV_ASTAR       = "寻路计算",
        SRV_SAVE_IO     = "存档序列化/IO",
        SRV_OTHER_OR_OS = "其他CPU或系统阻塞",
        SRV_MIXED       = "混合因素",
    }

    local function FormatEpochMsShort(epochMs)
        local em = G.tonumber(epochMs) or 0
        if em <= 0 then return "--:--:--.000" end
        local sec = math.floor(em / 1000)
        local ms = math.floor(em - sec * 1000)
        local os_mod = rawget(G, "os")
        if type(os_mod) == "table" and type(os_mod.date) == "function" then
            local okD, s = pcall(os_mod.date, "%H:%M:%S", sec)
            if okD and type(s) == "string" then
                return string.format("%s.%03d", s, ms)
            end
        end
        return string.format("%ds.%03d", sec % 86400, ms)
    end

    rawset(G, "c_hyperblackbox", function(arg)
        local zh = ResolveServerIsZh()
        if type(arg) == "string" then
            local cmd = arg:lower()
            if cmd == "clear" then
                local fnClr = rawget(G, "HyperCore_ClearBlackbox")
                if type(fnClr) ~= "function" then
                    print(zh and "[HyperCore 黑匣子] 未检测到原生黑匣子接口。"
                             or "[HyperCore Blackbox] Native blackbox API unavailable.")
                    return false
                end
                local ok = pcall(fnClr)
                print(zh and ("[HyperCore 黑匣子] 已清空服务端超刻缓存 -> " .. tostring(ok))
                         or ("[HyperCore Blackbox] Cleared server spike ring -> " .. tostring(ok)))
                return ok == true
            elseif cmd == "export" then
                local fnExp = rawget(G, "HyperCore_ExportBlackbox")
                if type(fnExp) ~= "function" then
                    print(zh and "[HyperCore 黑匣子] 未检测到原生黑匣子接口。"
                             or "[HyperCore Blackbox] Native blackbox API unavailable.")
                    return false
                end
                local okCall, okExp, outPath = pcall(fnExp)
                if okCall and okExp then
                    print(zh and string.format("[HyperCore 黑匣子] 已导出 JSON 快照 -> %s", tostring(outPath))
                             or string.format("[HyperCore Blackbox] Exported JSON snapshot -> %s", tostring(outPath)))
                    return true
                else
                    print(zh and string.format("[HyperCore 黑匣子] 导出失败 -> %s", tostring(outPath or ""))
                             or string.format("[HyperCore Blackbox] Export failed -> %s", tostring(outPath or "")))
                    return false
                end
            end
        end

        local fnGet = rawget(G, "HyperCore_GetBlackbox")
        if type(fnGet) ~= "function" then
            print(zh and "[HyperCore 黑匣子] 未检测到原生黑匣子接口。"
                     or "[HyperCore Blackbox] Native blackbox API unavailable.")
            return false
        end

        local limit = G.tonumber(arg) or 10
        limit = math.max(1, math.min(64, math.floor(limit)))
        local okGet, total, _, jsonStr = pcall(fnGet, limit)
        if not okGet or type(jsonStr) ~= "string" then
            print(zh and "[HyperCore 黑匣子] 读取原生缓存失败。"
                     or "[HyperCore Blackbox] Failed reading native ring.")
            return false
        end

        local json_mod = rawget(G, "json")
        if type(json_mod) ~= "table" and type(G.require) == "function" then
            local okJ, mJ = pcall(G.require, "json")
            if okJ and type(mJ) == "table" then json_mod = mJ end
        end
        if type(json_mod) ~= "table" or type(json_mod.decode) ~= "function" then
            print(jsonStr)
            return true
        end

        local okDec, tbl = pcall(json_mod.decode, jsonStr)
        if not okDec or type(tbl) ~= "table" then
            print(jsonStr)
            return true
        end

        local evs = type(tbl.events) == "table" and tbl.events or {}
        local threshMs = G.tonumber(tbl.threshold_ms) or 33
        local totCount = G.tonumber(tbl.total) or G.tonumber(total) or #evs
        print(zh
            and string.format("=== HyperCore 服务端超刻黑匣子 (累计=%d, 阈值=%dms, 显示=%d条) ===",
                totCount, threshMs, #evs)
            or string.format("=== HyperCore Server Spike Blackbox (total=%d, thresh=%dms, showing=%d) ===",
                totCount, threshMs, #evs))
        if #evs == 0 then
            print(zh and "  (暂无服务端超刻记录)" or "  (no server tick overrun events recorded yet)")
            return true
        end

        for i = 1, #evs do
            local e = evs[i]
            local ts = FormatEpochMsShort(e.epoch_ms)
            local tag = tostring(e.culprit or "SRV_MIXED")
            local culpritLbl = zh and ((SRV_CULPRIT_ZH[tag] or tag) .. "(" .. tag .. ")") or tag
            local gcTag = ((e.gc_full or 0) ~= 0) and (zh and " [全量GC]" or " [FULL_GC]") or ""
            print(zh
                and string.format(
                    "  #%02d [%s] 刻#%d (%dHz) [%s]%s | 总刻=%.2fms (预算=%.2f) | Lua=%.2f GC=%.2f(%dKB) 网络=%.2f 副本=%.2f 查询=%.2f 寻路=%.2f 存档=%.2f 其他=%.2f",
                    i, ts, G.tonumber(e.tick_seq) or 0, G.tonumber(e.sim_hz) or 30, culpritLbl, gcTag,
                    (G.tonumber(e.total_us) or 0) * 0.001,
                    (G.tonumber(e.budget_us) or 0) * 0.001,
                    (G.tonumber(e.lua_us) or 0) * 0.001,
                    (G.tonumber(e.gc_us) or 0) * 0.001, G.tonumber(e.gc_kb) or 0,
                    (G.tonumber(e.net_us) or 0) * 0.001,
                    (G.tonumber(e.replica_us) or 0) * 0.001,
                    (G.tonumber(e.find_ent_us) or 0) * 0.001,
                    (G.tonumber(e.astar_us) or 0) * 0.001,
                    (G.tonumber(e.save_us) or 0) * 0.001,
                    (G.tonumber(e.other_us) or 0) * 0.001)
                or string.format(
                    "  #%02d [%s] Tick#%d (%dHz) [%s]%s | Total=%.2fms (Budget=%.2f) | Lua=%.2f GC=%.2f(%dKB) Net=%.2f Rep=%.2f Find=%.2f AStar=%.2f Save=%.2f Other=%.2f",
                    i, ts, G.tonumber(e.tick_seq) or 0, G.tonumber(e.sim_hz) or 30, culpritLbl, gcTag,
                    (G.tonumber(e.total_us) or 0) * 0.001,
                    (G.tonumber(e.budget_us) or 0) * 0.001,
                    (G.tonumber(e.lua_us) or 0) * 0.001,
                    (G.tonumber(e.gc_us) or 0) * 0.001, G.tonumber(e.gc_kb) or 0,
                    (G.tonumber(e.net_us) or 0) * 0.001,
                    (G.tonumber(e.replica_us) or 0) * 0.001,
                    (G.tonumber(e.find_ent_us) or 0) * 0.001,
                    (G.tonumber(e.astar_us) or 0) * 0.001,
                    (G.tonumber(e.save_us) or 0) * 0.001,
                    (G.tonumber(e.other_us) or 0) * 0.001))
        end
        return true
    end)
end
